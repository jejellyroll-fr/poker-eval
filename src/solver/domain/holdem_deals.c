#include <poker_eval/solver/pe_holdem_deals.h>

#include <math.h>
#include <string.h>

#define PE_HOLDEM_MAX_PLAYERS 8u

static int valid_combo(mask_t cards)
{
    return mask_is_valid(cards) && mask_popcount(cards) == 2;
}

size_t pe_holdem_combo_count(mask_t dead_cards)
{
    size_t available;
    if (!mask_is_valid(dead_cards))
        return 0u;
    available = (size_t)(MODERN_DECK_SIZE - mask_popcount(dead_cards));
    return available < 2u ? 0u : available * (available - 1u) / 2u;
}

int pe_holdem_enumerate_combos(mask_t dead_cards, mask_t *out,
                               size_t capacity, size_t *out_count)
{
    size_t count = 0u;
    int first;
    if (!out_count || !mask_is_valid(dead_cards) ||
        (capacity > 0u && !out))
        return -1;
    for (first = 0; first < MODERN_DECK_SIZE; ++first)
    {
        int second;
        if (mask_is_set(dead_cards, first))
            continue;
        for (second = first + 1; second < MODERN_DECK_SIZE; ++second)
        {
            if (mask_is_set(dead_cards, second))
                continue;
            if (count < capacity)
                out[count] = mask_set(mask_set(MASK_EMPTY, first), second);
            ++count;
        }
    }
    *out_count = count;
    return capacity < count ? -2 : 0;
}

static int valid_inputs(mask_t board, const pe_holdem_range_t *ranges,
                        uint8_t player_count)
{
    uint8_t player;
    if (!mask_is_valid(board) || mask_popcount(board) > 5 || !ranges ||
        player_count == 0u || player_count > PE_HOLDEM_MAX_PLAYERS)
        return 0;
    for (player = 0u; player < player_count; ++player)
    {
        size_t combo;
        if (!ranges[player].combos && ranges[player].count != 0u)
            return 0;
        for (combo = 0u; combo < ranges[player].count; ++combo)
        {
            const pe_holdem_combo_t *entry = &ranges[player].combos[combo];
            if (!valid_combo(entry->cards) ||
                (entry->cards & board) != 0 ||
                !isfinite(entry->weight) || entry->weight < 0.0)
                return 0;
        }
    }
    return 1;
}

typedef struct
{
    mask_t holes[PE_HOLDEM_MAX_PLAYERS];
    size_t deal_count;
    double weight_sum;
    pe_holdem_deal_callback callback;
    void *user;
    int stopped;
} deal_walk_t;

static void walk_player(const pe_holdem_range_t *ranges, uint8_t player,
                        uint8_t player_count, mask_t used, double weight,
                        deal_walk_t *walk)
{
    size_t combo;
    if (walk->stopped)
        return;
    if (player == player_count)
    {
        walk->deal_count++;
        walk->weight_sum += weight;
        if (walk->callback && walk->callback(walk->holes, player_count, weight,
                                             walk->user) != 0)
            walk->stopped = 1;
        return;
    }
    for (combo = 0u; combo < ranges[player].count; ++combo)
    {
        const pe_holdem_combo_t *entry = &ranges[player].combos[combo];
        if (entry->weight <= 0.0 || (entry->cards & used) != 0)
            continue;
        walk->holes[player] = entry->cards;
        walk_player(ranges, (uint8_t)(player + 1u), player_count,
                    used | entry->cards, weight * entry->weight, walk);
        if (walk->stopped)
            return;
    }
}

static int walk_deals(mask_t board, const pe_holdem_range_t *ranges,
                      uint8_t player_count, pe_holdem_deal_callback callback,
                      void *user, size_t *deal_count, double *weight_sum)
{
    deal_walk_t walk;
    memset(&walk, 0, sizeof(walk));
    walk.callback = callback;
    walk.user = user;
    walk.holes[0] = board;
    walk_player(ranges, 0u, player_count, board, 1.0, &walk);
    if (deal_count)
        *deal_count = walk.deal_count;
    if (weight_sum)
        *weight_sum = walk.weight_sum;
    return walk.stopped && callback ? 1 : 0;
}

int pe_holdem_deals_measure(mask_t board, const pe_holdem_range_t *ranges,
                            uint8_t player_count, size_t *deal_count,
                            double *weight_sum)
{
    if (!deal_count || !weight_sum ||
        !valid_inputs(board, ranges, player_count))
        return -1;
    return walk_deals(board, ranges, player_count, NULL, NULL, deal_count,
                      weight_sum);
}

int pe_holdem_deals_enumerate(mask_t board, const pe_holdem_range_t *ranges,
                              uint8_t player_count,
                              pe_holdem_deal_callback callback, void *user,
                              size_t *deal_count, double *weight_sum)
{
    if (!callback || !deal_count || !weight_sum ||
        !valid_inputs(board, ranges, player_count))
        return -1;
    return walk_deals(board, ranges, player_count, callback, user, deal_count,
                      weight_sum);
}
