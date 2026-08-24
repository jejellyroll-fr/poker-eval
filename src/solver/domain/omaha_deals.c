#include <poker_eval/solver/pe_omaha_deals.h>

#include <math.h>
#include <string.h>

#include <poker_eval/solver/pe_combinations.h>

#define PE_OMAHA_MAX_PLAYERS 8u

static int valid_hole_count(uint8_t hole_cards)
{
    return hole_cards >= 4u && hole_cards <= 6u;
}

size_t pe_omaha_combo_count(mask_t dead_cards, uint8_t hole_cards)
{
    uint64_t count;
    if (!mask_is_valid(dead_cards) || !valid_hole_count(hole_cards))
        return 0u;
    count = pe_comb_count((unsigned)(MODERN_DECK_SIZE -
                                     mask_popcount(dead_cards)), hole_cards);
    return count > (uint64_t)SIZE_MAX ? 0u : (size_t)count;
}

int pe_omaha_enumerate_combos(mask_t dead_cards, uint8_t hole_cards,
                              mask_t *out, size_t capacity,
                              size_t *out_count)
{
    size_t count;
    size_t index;
    unsigned values[8];
    int card;
    int actual[52];
    size_t available_count = 0u;
    if (!out_count || !mask_is_valid(dead_cards) ||
        !valid_hole_count(hole_cards) || (capacity > 0u && !out))
        return -1;
    count = pe_omaha_combo_count(dead_cards, hole_cards);
    *out_count = count;
    if (capacity < count)
        return -2;
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (!mask_is_set(dead_cards, card))
            actual[available_count++] = card;
    for (index = 0u; index < count; ++index)
    {
        size_t ordinal = 0u;
        mask_t combo = MASK_EMPTY;
        if (pe_comb_unrank((unsigned)(MODERN_DECK_SIZE -
                                      mask_popcount(dead_cards)),
                           hole_cards, (uint64_t)index, values) !=
            PE_SOLVER_OK)
            return -1;
        for (ordinal = 0u; ordinal < hole_cards; ++ordinal)
        {
            if (values[ordinal] >= available_count)
                return -1;
            combo = mask_set(combo, actual[values[ordinal]]);
        }
        out[index] = combo;
    }
    return 0;
}

static int valid_inputs(mask_t board, const pe_omaha_range_t *ranges,
                        uint8_t player_count, uint8_t hole_cards)
{
    uint8_t player;
    if (!mask_is_valid(board) || mask_popcount(board) > 5 || !ranges ||
        player_count == 0u || player_count > PE_OMAHA_MAX_PLAYERS ||
        !valid_hole_count(hole_cards))
        return 0;
    for (player = 0u; player < player_count; ++player)
    {
        size_t combo;
        if (!ranges[player].combos && ranges[player].count != 0u)
            return 0;
        for (combo = 0u; combo < ranges[player].count; ++combo)
        {
            const pe_omaha_combo_t *entry = &ranges[player].combos[combo];
            if (!mask_is_valid(entry->cards) ||
                mask_popcount(entry->cards) != hole_cards ||
                (entry->cards & board) != 0 || !isfinite(entry->weight) ||
                entry->weight < 0.0)
                return 0;
        }
    }
    return 1;
}

typedef struct
{
    mask_t holes[PE_OMAHA_MAX_PLAYERS];
    size_t deal_count;
    double weight_sum;
    pe_omaha_deal_callback callback;
    void *user;
    int stopped;
} deal_walk_t;

static void walk_player(const pe_omaha_range_t *ranges, uint8_t player,
                        uint8_t player_count, mask_t used, double weight,
                        deal_walk_t *walk)
{
    size_t combo;
    if (walk->stopped)
        return;
    if (player == player_count)
    {
        ++walk->deal_count;
        walk->weight_sum += weight;
        if (walk->callback && walk->callback(walk->holes, player_count, weight,
                                             walk->user) != 0)
            walk->stopped = 1;
        return;
    }
    for (combo = 0u; combo < ranges[player].count; ++combo)
    {
        const pe_omaha_combo_t *entry = &ranges[player].combos[combo];
        if (entry->weight <= 0.0 || (entry->cards & used) != 0)
            continue;
        walk->holes[player] = entry->cards;
        walk_player(ranges, (uint8_t)(player + 1u), player_count,
                    used | entry->cards, weight * entry->weight, walk);
        if (walk->stopped)
            return;
    }
}

static int walk_deals(mask_t board, const pe_omaha_range_t *ranges,
                      uint8_t player_count, pe_omaha_deal_callback callback,
                      void *user, size_t *deal_count, double *weight_sum)
{
    deal_walk_t walk;
    memset(&walk, 0, sizeof(walk));
    walk.callback = callback;
    walk.user = user;
    walk_player(ranges, 0u, player_count, board, 1.0, &walk);
    *deal_count = walk.deal_count;
    *weight_sum = walk.weight_sum;
    return walk.stopped && callback ? 1 : 0;
}

int pe_omaha_deals_measure(mask_t board, const pe_omaha_range_t *ranges,
                           uint8_t player_count, uint8_t hole_cards,
                           size_t *deal_count, double *weight_sum)
{
    if (!deal_count || !weight_sum ||
        !valid_inputs(board, ranges, player_count, hole_cards))
        return -1;
    return walk_deals(board, ranges, player_count, NULL, NULL, deal_count,
                      weight_sum);
}

int pe_omaha_deals_enumerate(mask_t board, const pe_omaha_range_t *ranges,
                             uint8_t player_count, uint8_t hole_cards,
                             pe_omaha_deal_callback callback, void *user,
                             size_t *deal_count, double *weight_sum)
{
    if (!callback || !deal_count || !weight_sum ||
        !valid_inputs(board, ranges, player_count, hole_cards))
        return -1;
    return walk_deals(board, ranges, player_count, callback, user, deal_count,
                      weight_sum);
}
