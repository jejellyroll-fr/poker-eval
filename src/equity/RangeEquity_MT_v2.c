/*
 * RangeEquity_MT_v2.c - Optimized multithreaded version of range equity calculations
 *
 * Cette version intègre :
 *   - chunk size adaptatif
 *   - scheduling guidé
 *   - prefetching pour la work queue
 */

#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/enumord.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/RangeEquity_internal.h>
#include <poker_eval/equity/range_combo_buffers.h>
#include <poker_eval/core/builtin_compat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "poker_eval/utils/omp_compat.h"

#ifndef TRACE_RE
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
#endif

#if defined(_OPENMP)
#include "poker_eval/utils/omp_compat.h"
#endif

static int range_mt2_combo_stats_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("PE_RANGE_COMBO_TRACE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

static void range_mt2_log_combo_stats(int player_idx, const range_combo_buffer_t *buffer)
{
    if (!range_mt2_combo_stats_enabled() || !buffer)
        return;
    TRACE_RE("RangeMTv2 combo stats P%d: total=%d playable=%d total_w=%.2f playable_w=%.2f\n",
             player_idx,
             buffer->stats.total_combos,
             buffer->stats.playable_combos,
             buffer->stats.total_weight,
             buffer->stats.playable_weight);
}

typedef struct {
    StdDeck_CardMask hands[ENUM_MAXPLAYERS];
    double weight;
    int valid;
} MatchupWorkItem;

typedef struct {
    WeightedAggregation agg;
    unsigned int valid_matchups;
} ThreadLocalResults;

static inline int is_hand_valid_mt(StdDeck_CardMask hand, StdDeck_CardMask board, StdDeck_CardMask dead_cards) {
    StdDeck_CardMask combined;
    StdDeck_CardMask_RESET(combined);
    StdDeck_CardMask_OR(combined, board, dead_cards);
    return !StdDeck_CardMask_ANY_SET(hand, combined);
}

static inline int hands_conflict(StdDeck_CardMask *hands, int num_players) {
    for (int i = 0; i < num_players - 1; i++) {
        for (int j = i + 1; j < num_players; j++) {
            if (StdDeck_CardMask_ANY_SET(hands[i], hands[j])) {
                return 1;
            }
        }
    }
    return 0;
}

static int generate_matchup_work_queue(
    const PlayerRange player_ranges[],
    const range_combo_buffer_t combo_buffers[],
    bool use_prefilter,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    MatchupWorkItem **work_queue,
    int *queue_capacity
) {
    int estimated_matchups = 1;
    for (int i = 0; i < num_players; i++) {
        int count = range_combo_active_count(use_prefilter, combo_buffers, player_ranges, i);
        if (count <= 0)
            return -1;
        estimated_matchups *= count;
    }
    *queue_capacity = estimated_matchups;
    *work_queue = malloc(sizeof(MatchupWorkItem) * (*queue_capacity));
    if (!*work_queue) {
        TRACE_RE("Error: Failed to allocate work queue\n");
        return -1;
    }
    int queue_size = 0;
    int *indices = calloc(num_players, sizeof(int));
    if (!indices) {
        free(*work_queue);
        return -1;
    }
    while (1) {
        MatchupWorkItem *item = &(*work_queue)[queue_size];
        item->valid = 1;
        StdDeck_CardMask board_plus_dead;
        StdDeck_CardMask_OR(board_plus_dead, board, dead_cards);
        double matchup_weight = 1.0;
        for (int p = 0; p < num_players; p++) {
            item->hands[p] = range_combo_active_hand(use_prefilter, combo_buffers, player_ranges, p, indices[p]);
            double weight = range_combo_active_weight(use_prefilter, combo_buffers, player_ranges, p, indices[p]);
            if (weight == 0.0) {
                item->valid = 0;
                break;
            }
            matchup_weight *= weight;
            if (!use_prefilter && StdDeck_CardMask_ANY_SET(item->hands[p], board_plus_dead)) {
                item->valid = 0;
                break;
            }
        }
        if (item->valid && hands_conflict(item->hands, num_players)) {
            item->valid = 0;
        }
        if (item->valid && matchup_weight > 0.0) {
            item->weight = matchup_weight;
            queue_size++;
            if (queue_size >= *queue_capacity) {
                *queue_capacity *= 2;
                MatchupWorkItem *new_queue = realloc(*work_queue, sizeof(MatchupWorkItem) * (*queue_capacity));
                if (!new_queue) {
                    free(indices);
                    return -1;
                }
                *work_queue = new_queue;
            }
        }
        int carry = 1;
        for (int p = 0; p < num_players && carry; p++) {
            indices[p]++;
            int limit = range_combo_active_count(use_prefilter, combo_buffers, player_ranges, p);
            if (indices[p] < limit) {
                carry = 0;
            } else {
                indices[p] = 0;
            }
        }
        if (carry) break;
    }
    free(indices);
    if (queue_size < *queue_capacity) {
        MatchupWorkItem *shrunk = realloc(*work_queue, sizeof(MatchupWorkItem) * queue_size);
        if (shrunk) {
            *work_queue = shrunk;
            *queue_capacity = queue_size;
        }
    }
    return queue_size;
}

int CalculateEquityForRanges_MT_v2(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results,
    int num_threads
) {
    if (num_players <= 0 || num_players > ENUM_MAXPLAYERS) {
        TRACE_RE("Error: Invalid number of players: %d\n", num_players);
        return -1;
    }
    if (!aggregated_results) {
        TRACE_RE("Error: aggregated_results pointer is NULL.\n");
        return -1;
    }
    for (int i = 0; i < num_players; i++) {
        if (player_ranges[i].count <= 0 || !player_ranges[i].hand_masks) {
            TRACE_RE("Error: Player %d has an empty or invalid range.\n", i);
            enumResultClear(aggregated_results);
            aggregated_results->nplayers = num_players;
            aggregated_results->game = game;
            return 0;
        }
    }
    enumResultClear(aggregated_results);
    aggregated_results->game = game;
    aggregated_results->nplayers = num_players;
    aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    range_combo_mode_t combo_mode = range_combo_resolve_mode();
    const bool use_prefilter = (combo_mode == RANGE_COMBO_MODE_PREFILTER);
    range_combo_buffer_t combo_buffers[ENUM_MAXPLAYERS];
    memset(combo_buffers, 0, sizeof(combo_buffers));
    if (use_prefilter)
    {
        if (range_combo_build_buffers(combo_buffers, player_ranges, num_players, board, dead_cards_initial) != 0)
        {
            TRACE_RE("RangeMTv2: failed to build combo buffers\n");
            return -1;
        }
        for (int i = 0; i < num_players; ++i)
            range_mt2_log_combo_stats(i, &combo_buffers[i]);
    }
    MatchupWorkItem *work_queue = NULL;
    int queue_capacity = 0;
    int valid_matchups_total = generate_matchup_work_queue(
        player_ranges, combo_buffers, use_prefilter, num_players, board, dead_cards_initial,
        &work_queue, &queue_capacity
    );
    if (valid_matchups_total <= 0) {
        TRACE_RE("Warning: No valid matchups found after filtering conflicts.\n");
        if (work_queue) free(work_queue);
        if (use_prefilter)
            range_combo_free_buffers(combo_buffers, num_players);
        return 0;
    }
    TRACE_RE("Info: Generated %d valid matchups to evaluate.\n", valid_matchups_total);
    if (num_threads <= 0) {
        num_threads = omp_get_max_threads();
    }
    omp_set_num_threads(num_threads);
    ThreadLocalResults *thread_results = calloc(num_threads, sizeof(ThreadLocalResults));
    if (!thread_results) {
        free(work_queue);
        if (use_prefilter)
            range_combo_free_buffers(combo_buffers, num_players);
        return -1;
    }
    for (int t = 0; t < num_threads; t++) {
        weightedAggregationInit(&thread_results[t].agg, num_players);
        thread_results[t].valid_matchups = 0;
    }
    // --- OPTIMISATION PHASE 1 ---
    int optimal_chunk_size = valid_matchups_total / (num_threads * 10);
    if (optimal_chunk_size < 1) optimal_chunk_size = 1;
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        ThreadLocalResults *local_results = &thread_results[thread_id];
        int i;
        #pragma omp for schedule(guided, optimal_chunk_size)
        for (i = 0; i < valid_matchups_total; i++) {
            // Prefetch next matchup for cache
            if (i + 1 < valid_matchups_total) {
                __builtin_prefetch(&work_queue[i + 1], 0, 1);
            }
            MatchupWorkItem *item = &work_queue[i];
            if (!item->valid) continue;
            enum_result_t matchup_result;
            if (enumResultAlloc(&matchup_result, num_players, enum_ordering_mode_hi) != 0) {
                continue;
            }
            int ret_eval;
            StdDeck_CardMask effective_dead_cards_mt;
            StdDeck_CardMask_RESET(effective_dead_cards_mt);
            StdDeck_CardMask_OR(effective_dead_cards_mt, effective_dead_cards_mt, dead_cards_initial);
            for (int p_idx = 0; p_idx < num_players; ++p_idx) {
                StdDeck_CardMask_OR(effective_dead_cards_mt, effective_dead_cards_mt, item->hands[p_idx]);
            }
            StdDeck_CardMask_OR(effective_dead_cards_mt, effective_dead_cards_mt, board);
            int num_cards_on_board_mt = StdDeck_numCards(board);
            if (use_montecarlo) {
                ret_eval = enumSample(game, item->hands, board, effective_dead_cards_mt,
                                    num_players, num_cards_on_board_mt, iterations_if_montecarlo,
                                    orderflag, &matchup_result);
            } else {
                ret_eval = enumExhaustive_dispatch(game, item->hands, board, effective_dead_cards_mt,
                                                   num_players, num_cards_on_board_mt, orderflag,
                                                   &matchup_result);
            }
            if (ret_eval == 0) {
                weightedAggregationAccumulate(&local_results->agg, &matchup_result, item->weight, num_players);
                local_results->valid_matchups++;
            }
            enumResultFree(&matchup_result);
        }
    }
    unsigned int total_valid_matchups = 0;
    WeightedAggregation combined_agg;
    weightedAggregationInit(&combined_agg, num_players);
    for (int t = 0; t < num_threads; t++) {
        ThreadLocalResults *local_results = &thread_results[t];
        weightedAggregationMerge(&combined_agg, &local_results->agg, num_players);
        total_valid_matchups += local_results->valid_matchups;
    }
    aggregated_results->nsamples = total_valid_matchups;
    unsigned int target_samples = weightedAggregationFinalize(&combined_agg, aggregated_results, num_players);
    if (combined_agg.total_weight > 0.0) {
        TRACE_RE("Info: Evaluated %u valid matchups using %d threads (v2, total matchup weight %.3f, weighted samples %.3f).\n", 
                 total_valid_matchups, num_threads, combined_agg.total_weight, combined_agg.total_weighted_samples);
    } else {
        TRACE_RE("Warning: No weight accumulated across evaluated matchups (v2).\n");
    }
    if (target_samples > 0) {
        aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    }
    free(thread_results);
    free(work_queue);
    if (use_prefilter)
        range_combo_free_buffers(combo_buffers, num_players);
    return total_valid_matchups;
}
