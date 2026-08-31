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

    status = validate_input(hero_masks, hero_strength, hero_n, opp_masks,
                            opp_strength, opp_reach, opp_n, pot, out_values);
    if (status != PE_SOLVER_OK)
        return status;

    for (i = 0; i < hero_n && two_card_only; ++i)
        if (mask_card_count(hero_masks[i], NULL, 0) != 2)
            two_card_only = 0;
    for (i = 0; i < opp_n && two_card_only; ++i)
        if (mask_card_count(opp_masks[i], NULL, 0) != 2)
            two_card_only = 0;
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
