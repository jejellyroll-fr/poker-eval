#ifndef POKER_EVAL_RANGE_COMBO_BUFFERS_H
#define POKER_EVAL_RANGE_COMBO_BUFFERS_H

#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/deck/deck_std.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    RANGE_COMBO_MODE_PREFILTER = 0,
    RANGE_COMBO_MODE_LEGACY
} range_combo_mode_t;

typedef struct
{
    int total_combos;
    int playable_combos;
    double total_weight;
    double playable_weight;
} range_combo_stats_t;

typedef struct
{
    const PlayerRange *source;
    int *indices;
    int count;
    range_combo_stats_t stats;
} range_combo_buffer_t;

int range_combo_buffer_build(range_combo_buffer_t *buffer,
                             const PlayerRange *range,
                             StdDeck_CardMask board,
                             StdDeck_CardMask dead_cards);

void range_combo_buffer_free(range_combo_buffer_t *buffer);

int range_combo_build_buffers(range_combo_buffer_t buffers[],
                              const PlayerRange ranges[],
                              int num_players,
                              StdDeck_CardMask board,
                              StdDeck_CardMask dead_cards);

static inline void range_combo_free_buffers(range_combo_buffer_t buffers[], int num_players)
{
    for (int i = 0; i < num_players; ++i)
        range_combo_buffer_free(&buffers[i]);
}

StdDeck_CardMask range_combo_buffer_hand(const range_combo_buffer_t *buffer, int idx);

double range_combo_buffer_weight(const range_combo_buffer_t *buffer, int idx);

range_combo_mode_t range_combo_resolve_mode(void);

static inline int range_combo_active_count(bool use_prefilter,
                                           const range_combo_buffer_t buffers[],
                                           const PlayerRange ranges[],
                                           int player)
{
    return use_prefilter ? buffers[player].count : ranges[player].count;
}

static inline StdDeck_CardMask range_combo_active_hand(bool use_prefilter,
                                                       const range_combo_buffer_t buffers[],
                                                       const PlayerRange ranges[],
                                                       int player,
                                                       int idx)
{
    if (use_prefilter)
        return range_combo_buffer_hand(&buffers[player], idx);
    return ranges[player].hand_masks[idx];
}

static inline double range_combo_active_weight(bool use_prefilter,
                                               const range_combo_buffer_t buffers[],
                                               const PlayerRange ranges[],
                                               int player,
                                               int idx)
{
    if (use_prefilter)
        return range_combo_buffer_weight(&buffers[player], idx);
    if (!ranges[player].weights)
        return 1.0;
    return ranges[player].weights[idx];
}

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_RANGE_COMBO_BUFFERS_H */
