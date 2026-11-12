/*
 * showdown_vector.c - Sorted vector showdown with blockers (VEC-06)
 */

#include <poker_eval/solver/pe_showdown.h>

#include "finite_double.h"

#include <stdlib.h>
#include <string.h>

#define PE_SHOWDOWN_DECK 52u
#define PE_SHOWDOWN_MAX_PLAYERS 8u

typedef struct
{
    mask_t mask;
    int64_t strength;
    double reach;
} showdown_entry_t;

static int mask_card_count(mask_t mask, int *cards, int max_cards)
{
    int count = 0;
    int card;

    for (card = 0; card < (int)PE_SHOWDOWN_DECK; ++card)
    {
        if (mask_is_set(mask, card))
        {
            if (count < max_cards)
                cards[count] = card;
            ++count;
        }
    }
    return count;
}

static int entry_compare(const void *left, const void *right)
{
    const showdown_entry_t *a = (const showdown_entry_t *)left;
    const showdown_entry_t *b = (const showdown_entry_t *)right;

    if (a->strength < b->strength)
        return -1;
    if (a->strength > b->strength)
        return 1;
    return 0;
}

static int valid_values(const pe_value_vec_t *values, size_t n)
{
    return values && values->v && values->n == n;
}

static pe_solver_status_t validate_input(const mask_t *hero_masks,
                                         const int64_t *hero_strength,
                                         size_t hero_n,
                                         const mask_t *opp_masks,
                                         const int64_t *opp_strength,
                                         const double *opp_reach,
                                         size_t opp_n,
                                         double pot,
                                         const pe_value_vec_t *out_values)
{
    size_t i;

    if (!hero_masks || !hero_strength || !opp_masks || !opp_strength ||
        !opp_reach || !out_values)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (hero_n == 0 || opp_n == 0 || !valid_values(out_values, hero_n) ||
        pot < 0.0 || !pe_finite_double(pot))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    for (i = 0; i < opp_n; ++i)
        if (opp_reach[i] < 0.0 || !pe_finite_double(opp_reach[i]))
            return PE_SOLVER_ERR_INVALID_CONFIG;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_showdown_vector_pairwise(const mask_t *hero_masks,
                                               const int64_t *hero_strength,
                                               size_t hero_n,
                                               const mask_t *opp_masks,
                                               const int64_t *opp_strength,
                                               const double *opp_reach,
                                               size_t opp_n,
                                               mask_t dead,
                                               double pot,
                                               pe_value_vec_t *out_values)
{
    pe_solver_status_t status = validate_input(hero_masks, hero_strength, hero_n,
                                               opp_masks, opp_strength,
                                               opp_reach, opp_n, pot, out_values);
    size_t i;

    if (status != PE_SOLVER_OK)
        return status;

    for (i = 0; i < hero_n; ++i)
    {
        double value = 0.0;
        size_t j;

        if ((hero_masks[i] & dead) != 0)
        {
            out_values->v[i] = 0.0;
            continue;
        }
        for (j = 0; j < opp_n; ++j)
        {
            double share;
            if ((opp_masks[j] & dead) != 0 ||
                (opp_masks[j] & hero_masks[i]) != 0)
                continue;
            if (hero_strength[i] > opp_strength[j])
                share = 1.0;
            else if (hero_strength[i] == opp_strength[j])
                share = 0.5;
            else
                share = 0.0;
            value += share * opp_reach[j];
        }
        out_values->v[i] = value * pot;
    }
    return PE_SOLVER_OK;
}

static double prefix_range(const double *prefix,
                           const double *card_prefix,
                           const int64_t *pair_position,
                           const double *pair_reach,
                           size_t begin,
                           size_t end,
                           int first_card,
                           int second_card)
{
    double value;
    int64_t position;

    if (end <= begin)
        return 0.0;
    value = prefix[end] - prefix[begin];
    value -= card_prefix[end * PE_SHOWDOWN_DECK + (size_t)first_card] -
             card_prefix[begin * PE_SHOWDOWN_DECK + (size_t)first_card];
    value -= card_prefix[end * PE_SHOWDOWN_DECK + (size_t)second_card] -
             card_prefix[begin * PE_SHOWDOWN_DECK + (size_t)second_card];
    position = pair_position[first_card * (int)PE_SHOWDOWN_DECK + second_card];
    if (position >= (int64_t)begin && position < (int64_t)end)
        value += pair_reach[first_card * (int)PE_SHOWDOWN_DECK + second_card];
    return value > 0.0 ? value : 0.0;
}

static size_t lower_bound_strength(const showdown_entry_t *entries,
                                   size_t n, int64_t strength)
{
    size_t first = 0;
    size_t count = n;
    while (count > 0)
    {
        size_t step = count / 2u;
        size_t middle = first + step;
        if (entries[middle].strength < strength)
        {
            first = middle + 1u;
            count -= step + 1u;
        }
        else
            count = step;
    }
    return first;
}

static size_t upper_bound_strength(const showdown_entry_t *entries,
                                   size_t n, int64_t strength)
{
    size_t first = 0;
    size_t count = n;
    while (count > 0)
    {
        size_t step = count / 2u;
        size_t middle = first + step;
        if (entries[middle].strength <= strength)
        {
            first = middle + 1u;
            count -= step + 1u;
        }
        else
            count = step;
    }
    return first;
}

/* --------------------------------------------------------------------------
 * Four-card ranges: inclusion-exclusion sweep (PLO4)
 *
 * For one hero hand H and one strength threshold, the reach of the compatible
 * opponents is
 *
 *   total - sum_{c in H} A(c) + sum_{c<d in H} A(c,d)
 *         - sum_{c<d<e in H} A(c,d,e) + A(H)
 *
 * where A(S) is the accumulated reach of the opponents whose hand contains S.
 * Balaying opponent strengths from worst to best keeps one subset table for
 * "strictly below the current strength" and a second, rebuilt per tie group,
 * for "equal to it". No table has one entry per (opponent, hero) pair.
 * ------------------------------------------------------------------------ */

#define PE_SHOWDOWN_HOLE_CARDS 4u
#define PE_SHOWDOWN_SUBSET_COUNT 16u
/* C(52,1) + C(52,2) + C(52,3) + C(52,4): every subset a four-card hand can
   contain, and therefore the most distinct keys the table can ever hold. */
#define PE_SHOWDOWN_SUBSET_LIMIT 294203u
#define PE_SHOWDOWN_MAX_TABLE_CAPACITY ((size_t)1u << 20)

typedef struct
{
    uint64_t key;
    double sum;
} plo4_slot_t;

typedef struct
{
    plo4_slot_t *slots;
    size_t capacity;
    double total;
} plo4_subsets_t;

typedef struct
{
    int64_t strength;
    size_t index;
} plo4_hero_t;

static uint64_t plo4_mix(uint64_t key)
{
    key ^= key >> 30;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27;
    key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 31;
    return key;
}

static int plo4_subsets_reserve(plo4_subsets_t *subsets, size_t hint)
{
    size_t capacity = 16u;

    if (hint > PE_SHOWDOWN_SUBSET_LIMIT)
        hint = PE_SHOWDOWN_SUBSET_LIMIT;
    while (capacity < hint * 2u && capacity < PE_SHOWDOWN_MAX_TABLE_CAPACITY)
        capacity <<= 1u;
    subsets->slots = (plo4_slot_t *)calloc(capacity, sizeof(*subsets->slots));
    if (!subsets->slots)
        return -1;
    subsets->capacity = capacity;
    subsets->total = 0.0;
    return 0;
}

static void plo4_subsets_reset(plo4_subsets_t *subsets)
{
    if (subsets->slots)
        memset(subsets->slots, 0, subsets->capacity * sizeof(*subsets->slots));
    subsets->total = 0.0;
}

static void plo4_subsets_free(plo4_subsets_t *subsets)
{
    free(subsets->slots);
    subsets->slots = NULL;
    subsets->capacity = 0u;
    subsets->total = 0.0;
}

static uint64_t plo4_subset_key(const int *cards, unsigned subset)
{
    uint64_t key = 0u;
    unsigned bit;

    for (bit = 0u; bit < PE_SHOWDOWN_HOLE_CARDS; ++bit)
        if ((subset & (1u << bit)) != 0u)
            key |= UINT64_C(1) << cards[bit];
    return key;
}

static void plo4_subsets_add(plo4_subsets_t *subsets, mask_t mask, double weight)
{
    int cards[PE_SHOWDOWN_HOLE_CARDS];
    size_t index_mask;
    unsigned subset;

    if (mask_card_count(mask, cards, (int)PE_SHOWDOWN_HOLE_CARDS) !=
        (int)PE_SHOWDOWN_HOLE_CARDS)
        return;
    index_mask = subsets->capacity - 1u;
    for (subset = 1u; subset < PE_SHOWDOWN_SUBSET_COUNT; ++subset)
    {
        uint64_t key = plo4_subset_key(cards, subset);
        size_t slot = (size_t)plo4_mix(key) & index_mask;

        while (subsets->slots[slot].key != 0u &&
               subsets->slots[slot].key != key)
            slot = (slot + 1u) & index_mask;
        if (subsets->slots[slot].key == 0u)
        {
            subsets->slots[slot].key = key;
            subsets->slots[slot].sum = 0.0;
        }
        subsets->slots[slot].sum += weight;
    }
    subsets->total += weight;
}

static double plo4_subsets_sum(const plo4_subsets_t *subsets, uint64_t key)
{
    size_t index_mask;
    size_t slot;
    size_t probes;

    if (!subsets->slots || subsets->capacity == 0u)
        return 0.0;
    index_mask = subsets->capacity - 1u;
    slot = (size_t)plo4_mix(key) & index_mask;
    for (probes = 0u; probes < subsets->capacity; ++probes)
    {
        if (subsets->slots[slot].key == 0u)
            return 0.0;
        if (subsets->slots[slot].key == key)
            return subsets->slots[slot].sum;
        slot = (slot + 1u) & index_mask;
    }
    return 0.0;
}

/* Reach of the compatible opponents, with the compatible mass clamped when
   cancellation leaves a residue that is zero up to rounding. */
static double plo4_compatible_mass(const plo4_subsets_t *subsets,
                                   mask_t hero_mask, mask_t dead)
{
    int cards[PE_SHOWDOWN_HOLE_CARDS];
    double mass;
    unsigned subset;

    if ((hero_mask & dead) != 0)
        return 0.0;
    if (mask_card_count(hero_mask, cards, (int)PE_SHOWDOWN_HOLE_CARDS) !=
        (int)PE_SHOWDOWN_HOLE_CARDS)
        return 0.0;
    mass = subsets->total;
    for (subset = 1u; subset < PE_SHOWDOWN_SUBSET_COUNT; ++subset)
    {
        double part = plo4_subsets_sum(subsets, plo4_subset_key(cards, subset));
        unsigned bits = 0u;
        unsigned bit;

        for (bit = 0u; bit < PE_SHOWDOWN_HOLE_CARDS; ++bit)
            if ((subset & (1u << bit)) != 0u)
                ++bits;
        if ((bits & 1u) != 0u)
            mass -= part;
        else
            mass += part;
    }
    if (mass < 0.0 && mass > -1e-9 * (1.0 + subsets->total))
        mass = 0.0;
    return mass;
}

static int plo4_hero_compare(const void *left, const void *right)
{
    const plo4_hero_t *a = (const plo4_hero_t *)left;
    const plo4_hero_t *b = (const plo4_hero_t *)right;

    if (a->strength < b->strength)
        return -1;
    if (a->strength > b->strength)
        return 1;
    return 0;
}

static size_t plo4_table_hint(size_t entries)
{
    size_t hint = entries * (PE_SHOWDOWN_SUBSET_COUNT - 1u);

    return hint > PE_SHOWDOWN_SUBSET_LIMIT ? PE_SHOWDOWN_SUBSET_LIMIT : hint;
}

static pe_solver_status_t showdown_plo4(const showdown_entry_t *entries,
                                        size_t opp_n,
                                        const mask_t *hero_masks,
                                        const int64_t *hero_strength,
                                        size_t hero_n, mask_t dead, double pot,
                                        pe_value_vec_t *out_values)
{
    plo4_hero_t *heroes;
    double *wins;
    double *ties;
    plo4_subsets_t below;
    plo4_subsets_t equal;
    size_t hero_cursor = 0u;
    size_t entry_cursor = 0u;
    size_t max_tie_group = 0u;
    pe_solver_status_t status = PE_SOLVER_ERR_OUT_OF_MEMORY;

    heroes = (plo4_hero_t *)malloc(hero_n * sizeof(*heroes));
    wins = (double *)calloc(hero_n, sizeof(*wins));
    ties = (double *)calloc(hero_n, sizeof(*ties));
    below.slots = NULL;
    below.capacity = 0u;
    below.total = 0.0;
    equal.slots = NULL;
    equal.capacity = 0u;
    equal.total = 0.0;
    if (!heroes || !wins || !ties)
        goto done;
    if (plo4_subsets_reserve(&below, plo4_table_hint(opp_n)) != 0)
        goto done;
    /* The tie table is sized once, for the largest group of equal strengths:
       growing it mid-sweep would either overflow the probe sequence or force
       a rehash this path does not need. */
    for (size_t i = 0u; i < opp_n;)
    {
        size_t run = i + 1u;
        while (run < opp_n && entries[run].strength == entries[i].strength)
            ++run;
        if (run - i > max_tie_group)
            max_tie_group = run - i;
        i = run;
    }
    if (plo4_subsets_reserve(&equal, plo4_table_hint(max_tie_group)) != 0)
        goto done;

    for (size_t i = 0u; i < hero_n; ++i)
    {
        heroes[i].strength = hero_strength[i];
        heroes[i].index = i;
    }
    qsort(heroes, hero_n, sizeof(*heroes), plo4_hero_compare);

    while (hero_cursor < hero_n)
    {
        int64_t strength = heroes[hero_cursor].strength;
        size_t hero_end = hero_cursor;
        size_t tie_end;

        while (hero_end < hero_n && heroes[hero_end].strength == strength)
            ++hero_end;
        while (entry_cursor < opp_n &&
               entries[entry_cursor].strength < strength)
        {
            if (entries[entry_cursor].reach > 0.0)
                plo4_subsets_add(&below, entries[entry_cursor].mask,
                                 entries[entry_cursor].reach);
            ++entry_cursor;
        }
        tie_end = entry_cursor;
        while (tie_end < opp_n && entries[tie_end].strength == strength)
            ++tie_end;
        if (tie_end > entry_cursor)
        {
            plo4_subsets_reset(&equal);
            for (size_t i = entry_cursor; i < tie_end; ++i)
                if (entries[i].reach > 0.0)
                    plo4_subsets_add(&equal, entries[i].mask, entries[i].reach);
        }
        for (size_t i = hero_cursor; i < hero_end; ++i)
        {
            size_t index = heroes[i].index;

            if ((hero_masks[index] & dead) != 0)
                continue;
            wins[index] = plo4_compatible_mass(&below, hero_masks[index], dead);
            if (tie_end > entry_cursor)
                ties[index] = plo4_compatible_mass(&equal, hero_masks[index],
                                                   dead);
        }
        for (size_t i = entry_cursor; i < tie_end; ++i)
            if (entries[i].reach > 0.0)
                plo4_subsets_add(&below, entries[i].mask, entries[i].reach);
        entry_cursor = tie_end;
        hero_cursor = hero_end;
    }

    for (size_t i = 0u; i < hero_n; ++i)
        out_values->v[i] = (hero_masks[i] & dead) != 0
                               ? 0.0
                               : pot * (wins[i] + 0.5 * ties[i]);
    status = PE_SOLVER_OK;

done:
    plo4_subsets_free(&below);
    plo4_subsets_free(&equal);
    free(ties);
    free(wins);
    free(heroes);
    return status;
}

pe_solver_status_t pe_showdown_vector(const mask_t *hero_masks,
                                      const int64_t *hero_strength,
                                      size_t hero_n,
                                      const mask_t *opp_masks,
                                      const int64_t *opp_strength,
                                      const double *opp_reach,
                                      size_t opp_n,
                                      mask_t dead,
                                      double pot,
                                      pe_value_vec_t *out_values,
                                      pe_showdown_path_t *out_path)
{
    showdown_entry_t *entries = NULL;
    double *prefix = NULL;
    double *card_prefix = NULL;
    int64_t pair_position[PE_SHOWDOWN_DECK * PE_SHOWDOWN_DECK];
    double pair_reach[PE_SHOWDOWN_DECK * PE_SHOWDOWN_DECK];
    pe_solver_status_t status;
    size_t i;
    int two_card_only = 1;
    int four_card_only = 1;
    int hero_wide;

    status = validate_input(hero_masks, hero_strength, hero_n, opp_masks,
                            opp_strength, opp_reach, opp_n, pot, out_values);
    if (status != PE_SOLVER_OK)
        return status;

    for (i = 0; i < hero_n && (two_card_only || four_card_only); ++i)
    {
        int cards = mask_card_count(hero_masks[i], NULL, 0);
        if (cards != 2)
            two_card_only = 0;
        if (cards != 4)
            four_card_only = 0;
    }
    for (i = 0; i < opp_n && (two_card_only || four_card_only); ++i)
    {
        int cards = mask_card_count(opp_masks[i], NULL, 0);
        if (cards != 2)
            two_card_only = 0;
        if (cards != 4)
            four_card_only = 0;
    }
    hero_wide = four_card_only && opp_n >= PE_SHOWDOWN_PLO4_MIN_OPPONENTS;
    if (hero_wide)
    {
        showdown_entry_t *wide =
            (showdown_entry_t *)calloc(opp_n, sizeof(*wide));
        if (!wide)
            return PE_SOLVER_ERR_OUT_OF_MEMORY;
        for (i = 0; i < opp_n; ++i)
        {
            wide[i].mask = opp_masks[i];
            wide[i].strength = opp_strength[i];
            wide[i].reach = (opp_masks[i] & dead) == 0 ? opp_reach[i] : 0.0;
        }
        qsort(wide, opp_n, sizeof(*wide), entry_compare);
        status = showdown_plo4(wide, opp_n, hero_masks, hero_strength,
                               hero_n, dead, pot, out_values);
        free(wide);
        if (status == PE_SOLVER_OK && out_path)
            *out_path = PE_SHOWDOWN_PATH_PLO4;
        return status;
    }
    if (!two_card_only)
    {
        if (out_path)
            *out_path = PE_SHOWDOWN_PATH_PAIRWISE;
        return pe_showdown_vector_pairwise(hero_masks, hero_strength, hero_n,
                                           opp_masks, opp_strength, opp_reach,
                                           opp_n, dead, pot, out_values);
    }

    entries = (showdown_entry_t *)calloc(opp_n, sizeof(*entries));
    if (!entries)
        return PE_SOLVER_ERR_OUT_OF_MEMORY;
    for (i = 0; i < opp_n; ++i)
    {
        entries[i].mask = opp_masks[i];
        entries[i].strength = opp_strength[i];
        entries[i].reach = (opp_masks[i] & dead) == 0 ? opp_reach[i] : 0.0;
    }
    qsort(entries, opp_n, sizeof(*entries), entry_compare);

    if (opp_n > (SIZE_MAX / sizeof(double)) / PE_SHOWDOWN_DECK - 1u)
    {
        free(entries);
        return PE_SOLVER_ERR_OUT_OF_MEMORY;
    }
    prefix = (double *)calloc(opp_n + 1u, sizeof(*prefix));
    card_prefix = (double *)calloc((opp_n + 1u) * PE_SHOWDOWN_DECK,
                                   sizeof(*card_prefix));
    if (!prefix || !card_prefix)
    {
        free(card_prefix);
        free(prefix);
        free(entries);
        return PE_SOLVER_ERR_OUT_OF_MEMORY;
    }

    for (i = 0; i < PE_SHOWDOWN_DECK * PE_SHOWDOWN_DECK; ++i)
        pair_position[i] = -1;
    memset(pair_reach, 0, sizeof(pair_reach));

    for (i = 0; i < opp_n; ++i)
    {
        int cards[2];
        size_t card;
        double weight = entries[i].reach;

        prefix[i + 1u] = prefix[i] + weight;
        for (card = 0u; card < PE_SHOWDOWN_DECK; ++card)
            card_prefix[(i + 1u) * PE_SHOWDOWN_DECK + card] =
                card_prefix[i * PE_SHOWDOWN_DECK + card];
        if (!(weight > 0.0) && !(weight < 0.0))
            continue;
        mask_card_count(entries[i].mask, cards, 2);
        for (card = 0; card < 2u; ++card)
            card_prefix[(i + 1u) * PE_SHOWDOWN_DECK + (size_t)cards[card]] += weight;
        if (pair_position[cards[0] * (int)PE_SHOWDOWN_DECK + cards[1]] >= 0)
        {
            free(card_prefix);
            free(prefix);
            free(entries);
            if (out_path)
                *out_path = PE_SHOWDOWN_PATH_PAIRWISE;
            return pe_showdown_vector_pairwise(hero_masks, hero_strength, hero_n,
                                               opp_masks, opp_strength, opp_reach,
                                               opp_n, dead, pot, out_values);
        }
        pair_position[cards[0] * (int)PE_SHOWDOWN_DECK + cards[1]] = (int64_t)i;
        pair_position[cards[1] * (int)PE_SHOWDOWN_DECK + cards[0]] = (int64_t)i;
        pair_reach[cards[0] * (int)PE_SHOWDOWN_DECK + cards[1]] = weight;
        pair_reach[cards[1] * (int)PE_SHOWDOWN_DECK + cards[0]] = weight;
    }

    for (i = 0; i < hero_n; ++i)
    {
        int cards[2];
        size_t lower;
        size_t upper;
        double wins;
        double ties;

        if ((hero_masks[i] & dead) != 0)
        {
            out_values->v[i] = 0.0;
            continue;
        }
        mask_card_count(hero_masks[i], cards, 2);
        lower = lower_bound_strength(entries, opp_n, hero_strength[i]);
        upper = upper_bound_strength(entries, opp_n, hero_strength[i]);
        wins = prefix_range(prefix, card_prefix, pair_position, pair_reach,
                            0, lower, cards[0], cards[1]);
        ties = prefix_range(prefix, card_prefix, pair_position, pair_reach,
                            lower, upper, cards[0], cards[1]);
        out_values->v[i] = pot * (wins + 0.5 * ties);
    }

    free(card_prefix);
    free(prefix);
    free(entries);
    if (out_path)
        *out_path = PE_SHOWDOWN_PATH_SORTED;
    return PE_SOLVER_OK;
}

typedef struct
{
    const pe_showdown_player_t *players;
    uint8_t player_count;
    uint8_t hero_player;
    mask_t dead;
    mask_t used;
    const pe_showdown_sidepot_t *sidepots;
    size_t sidepot_count;
    double weight;
    double result;
    int fold_only;
    int64_t strength[PE_SHOWDOWN_MAX_PLAYERS];
} multiway_context_t;

static void multiway_visit(multiway_context_t *context, uint8_t player)
{
    const pe_showdown_player_t *current;
    size_t combo;

    if (player == context->player_count)
    {
        if (context->fold_only)
        {
            context->result += context->weight;
            return;
        }
        else
        {
            size_t pot_index;
            for (pot_index = 0; pot_index < context->sidepot_count; ++pot_index)
            {
                const pe_showdown_sidepot_t *pot = &context->sidepots[pot_index];
                uint8_t eligible = (uint8_t)(pot->eligible_players &
                                             ((1u << context->player_count) - 1u));
                int64_t best = 0;
                unsigned winners = 0u;
                uint8_t p;

                for (p = 0u; p < context->player_count; ++p)
                {
                    if ((eligible & (uint8_t)(1u << p)) == 0)
                        continue;
                    if (winners == 0u || context->strength[p] > best)
                    {
                        best = context->strength[p];
                        winners = 1u;
                    }
                    else if (context->strength[p] == best)
                        ++winners;
                }
                if ((eligible & (uint8_t)(1u << context->hero_player)) != 0 &&
                    context->strength[context->hero_player] == best &&
                    winners > 0u)
                    context->result += context->weight * pot->amount /
                                       (double)winners;
            }
            return;
        }
    }
    if (player == context->hero_player)
    {
        multiway_visit(context, (uint8_t)(player + 1u));
        return;
    }

    current = &context->players[player];
    for (combo = 0; combo < current->combo_count; ++combo)
    {
        mask_t mask = current->masks[combo];
        double reach = current->reach[combo];
        mask_t previous_used;
        double previous_weight;

        if ((mask & context->used) != 0 ||
            (!(reach > 0.0) && !(reach < 0.0)))
            continue;
        previous_used = context->used;
        previous_weight = context->weight;
        context->used |= mask;
        context->weight *= reach;
        context->strength[player] = current->strength[combo];
        multiway_visit(context, (uint8_t)(player + 1u));
        context->weight = previous_weight;
        context->used = previous_used;
    }
}

static pe_solver_status_t validate_multiway(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    const pe_value_vec_t *out_values)
{
    uint8_t player;

    if (!players || !out_values)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (player_count < 2u || player_count > PE_SHOWDOWN_MAX_PLAYERS)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    for (player = 0u; player < player_count; ++player)
    {
        size_t combo;
        if (!players[player].masks || !players[player].strength ||
            !players[player].reach || players[player].combo_count == 0 ||
            !valid_values(&out_values[player], players[player].combo_count))
            return PE_SOLVER_ERR_NULL_ARGUMENT;
        for (combo = 0; combo < players[player].combo_count; ++combo)
            if (players[player].reach[combo] < 0.0 ||
                !pe_finite_double(players[player].reach[combo]))
                return PE_SOLVER_ERR_INVALID_CONFIG;
    }
    return PE_SOLVER_OK;
}

static pe_solver_status_t multiway_values(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    mask_t dead,
    double pot,
    pe_value_vec_t *out_values,
    pe_showdown_path_t *out_path,
    int fold_only,
    uint8_t selected_player,
    double fold_pot,
    const pe_showdown_sidepot_t *sidepots,
    size_t sidepot_count)
{
    pe_solver_status_t status;
    uint8_t hero_player;

    status = validate_multiway(players, player_count, out_values);
    if (status != PE_SOLVER_OK)
        return status;
    if (fold_only && (fold_pot < 0.0 || !pe_finite_double(fold_pot)))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    if (!fold_only && (!sidepots || sidepot_count == 0))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    if (!fold_only)
    {
        size_t pot_index;
        for (pot_index = 0; pot_index < sidepot_count; ++pot_index)
            if (sidepots[pot_index].amount < 0.0 ||
                !pe_finite_double(sidepots[pot_index].amount) ||
                (sidepots[pot_index].eligible_players &
                 (uint8_t)((1u << player_count) - 1u)) == 0)
                return PE_SOLVER_ERR_INVALID_CONFIG;
    }
    if (fold_only && selected_player >= player_count)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    for (hero_player = 0u; hero_player < player_count; ++hero_player)
        for (size_t combo = 0; combo < players[hero_player].combo_count; ++combo)
            out_values[hero_player].v[combo] = 0.0;

    for (hero_player = 0u; hero_player < player_count; ++hero_player)
    {
        multiway_context_t context;
        size_t combo;

        if (fold_only && hero_player != selected_player)
            continue;
        for (combo = 0; combo < players[hero_player].combo_count; ++combo)
        {
            memset(&context, 0, sizeof(context));
            context.players = players;
            context.player_count = player_count;
            context.hero_player = hero_player;
            context.dead = dead;
            context.used = dead | players[hero_player].masks[combo];
            context.sidepots = sidepots;
            context.sidepot_count = sidepot_count;
            context.weight = 1.0;
            context.fold_only = fold_only;
            context.strength[hero_player] =
                players[hero_player].strength[combo];
            if ((players[hero_player].masks[combo] & dead) != 0)
                out_values[hero_player].v[combo] = 0.0;
            else
            {
                multiway_visit(&context, 0u);
                out_values[hero_player].v[combo] = fold_only
                    ? fold_pot * context.result : context.result;
            }
        }
    }
    if (out_path)
        *out_path = PE_SHOWDOWN_PATH_MULTIWAY;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_showdown_multiway_vector(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    mask_t dead,
    double pot,
    pe_value_vec_t *out_values,
    pe_showdown_path_t *out_path)
{
    pe_showdown_sidepot_t sidepot;
    sidepot.amount = pot;
    sidepot.eligible_players = UINT8_MAX;
    return multiway_values(players, player_count, dead, pot, out_values,
                            out_path, 0, 0u, pot, &sidepot, 1u);
}

pe_solver_status_t pe_showdown_multiway_sidepots(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    mask_t dead,
    const pe_showdown_sidepot_t *sidepots,
    size_t sidepot_count,
    pe_value_vec_t *out_values,
    pe_showdown_path_t *out_path)
{
    return multiway_values(players, player_count, dead, 0.0, out_values,
                           out_path, 0, 0u, 0.0, sidepots, sidepot_count);
}

pe_solver_status_t pe_fold_multiway_vector(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    uint8_t hero_player,
    mask_t dead,
    double pot,
    pe_value_vec_t *out_values,
    pe_showdown_path_t *out_path)
{
    return multiway_values(players, player_count, dead, pot, out_values,
                            out_path, 1, hero_player, pot, NULL, 0u);
}
