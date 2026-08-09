/*
 * RangeEquity_MT_v5.c - Adaptive multithreaded version with work-stealing deque
 *
 * - Work-stealing scheduler for dynamic/adaptive load balancing
 * - Suitable for unbalanced ranges and heterogeneous architectures
 * - Interface compatible with previous MT versions
 */

#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/enumord.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/RangeEquity_internal.h>
#include <poker_eval/equity/range_combo_buffers.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "poker_eval/utils/omp_compat.h"

#ifndef TRACE_RE
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
#endif

static int range_mt5_combo_stats_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("PE_RANGE_COMBO_TRACE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

static void range_mt5_log_combo_stats(int player_idx, const range_combo_buffer_t *buffer)
{
    if (!range_mt5_combo_stats_enabled() || !buffer)
        return;
    TRACE_RE("RangeMTv5 combo stats P%d: total=%d playable=%d total_w=%.2f playable_w=%.2f\n",
             player_idx,
             buffer->stats.total_combos,
             buffer->stats.playable_combos,
             buffer->stats.total_weight,
             buffer->stats.playable_weight);
}

#define MAX_THREADS 64
#define TASK_BATCH_SIZE 32

typedef struct {
    int start;
    int end;
} Task;

typedef struct {
    Task *tasks;
    int head;
    int tail;
    omp_lock_t lock;
    int capacity;
} TaskDeque;

static int taskdeque_init(TaskDeque *dq, int capacity) {
    dq->tasks = malloc(sizeof(Task) * capacity);
    if (!dq->tasks)
        return -1;
    dq->head = dq->tail = 0;
    dq->capacity = capacity;
    omp_init_lock(&dq->lock);
    return 0;
}

static void taskdeque_destroy(TaskDeque *dq) {
    omp_destroy_lock(&dq->lock);
    free(dq->tasks);
}

static int taskdeque_push(TaskDeque *dq, Task t) {
    omp_set_lock(&dq->lock);
    int next_tail = (dq->tail + 1) % dq->capacity;
    if (next_tail == dq->head) {
        omp_unset_lock(&dq->lock);
        return 0; // full
    }
    dq->tasks[dq->tail] = t;
    dq->tail = next_tail;
    omp_unset_lock(&dq->lock);
    return 1;
}

static int taskdeque_pop(TaskDeque *dq, Task *t) {
    omp_set_lock(&dq->lock);
    if (dq->head == dq->tail) {
        omp_unset_lock(&dq->lock);
        return 0; // empty
    }
    *t = dq->tasks[dq->head];
    dq->head = (dq->head + 1) % dq->capacity;
    omp_unset_lock(&dq->lock);
    return 1;
}

// Steal from the tail (other thread)
static int taskdeque_steal(TaskDeque *dq, Task *t) {
    omp_set_lock(&dq->lock);
    if (dq->head == dq->tail) {
        omp_unset_lock(&dq->lock);
        return 0; // empty
    }
    dq->tail = (dq->tail - 1 + dq->capacity) % dq->capacity;
    *t = dq->tasks[dq->tail];
    omp_unset_lock(&dq->lock);
    return 1;
}

// Helper to generate a matchup for a given index
static int generate_matchup_from_index(
    int idx,
    const PlayerRange player_ranges[],
    const range_combo_buffer_t combo_buffers[],
    bool use_prefilter,
    int num_players,
    StdDeck_CardMask hands[ENUM_MAXPLAYERS],
    double *weight_out
) {
    if (weight_out)
        *weight_out = 0.0;
    int counts[ENUM_MAXPLAYERS];
    for (int i = 0; i < num_players; i++)
        counts[i] = range_combo_active_count(use_prefilter, combo_buffers, player_ranges, i);
    double matchup_weight = 1.0;
    for (int p = num_players - 1; p >= 0; p--) {
        int c = idx % counts[p];
        hands[p] = range_combo_active_hand(use_prefilter, combo_buffers, player_ranges, p, c);
        double weight = range_combo_active_weight(use_prefilter, combo_buffers, player_ranges, p, c);
        if (weight == 0.0) {
            return 0;
        }
        matchup_weight *= weight;
        idx /= counts[p];
    }
    if (weight_out)
        *weight_out = matchup_weight;
    // Check for overlap
    for (int i = 0; i < num_players - 1; i++) {
        for (int j = i + 1; j < num_players; j++) {
            if (StdDeck_CardMask_ANY_SET(hands[i], hands[j]))
                return 0;
        }
    }
    return 1;
}

typedef struct {
    WeightedAggregation agg;
    unsigned int valid_matchups;
} ThreadLocalResults;

int CalculateEquityForRanges_MT_v5(
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
            TRACE_RE("RangeMTv5: failed to build combo buffers\n");
            return -1;
        }
        for (int i = 0; i < num_players; ++i)
            range_mt5_log_combo_stats(i, &combo_buffers[i]);
    }
    // Compute total combinations
    int total_combinations = 1;
    for (int i = 0; i < num_players; i++)
    {
        int count = range_combo_active_count(use_prefilter, combo_buffers, player_ranges, i);
        if (count <= 0)
        {
            if (use_prefilter)
                range_combo_free_buffers(combo_buffers, num_players);
            return 0;
        }
        total_combinations *= count;
    }
    if (num_threads <= 0 || num_threads > MAX_THREADS)
        num_threads = omp_get_max_threads();
    omp_set_num_threads(num_threads);

    // Create per-thread deques
    TaskDeque deques[MAX_THREADS];
    for (int t = 0; t < num_threads; t++) {
        if (taskdeque_init(&deques[t], 1024) != 0) {
            for (int t2 = 0; t2 < t; t2++)
                taskdeque_destroy(&deques[t2]);
            if (use_prefilter)
                range_combo_free_buffers(combo_buffers, num_players);
            return -1;
        }
    }

    // Split work into batches
    int batch_size = TASK_BATCH_SIZE;
    for (int i = 0; i < total_combinations; i += batch_size) {
        Task task = { .start = i, .end = (i + batch_size < total_combinations) ? i + batch_size : total_combinations };
        taskdeque_push(&deques[i % num_threads], task);
    }

    ThreadLocalResults thread_results[MAX_THREADS];
    for (int t = 0; t < num_threads; t++) {
        weightedAggregationInit(&thread_results[t].agg, num_players);
        thread_results[t].valid_matchups = 0;
    }

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        Task task;
        StdDeck_CardMask hands[ENUM_MAXPLAYERS];
        enum_result_t matchup_result;
        enumResultAlloc(&matchup_result, num_players, enum_ordering_mode_hi);
        while (1) {
            int stolen = 0;
            int got = taskdeque_pop(&deques[tid], &task);
            if (!got) {
                // Try to steal
                for (int t2 = 0; t2 < num_threads; t2++) {
                    if (t2 == tid) continue;
                    if (taskdeque_steal(&deques[t2], &task)) {
                        stolen = 1;
                        break;
                    }
                }
                if (!stolen) break; // No work left
            }
            if (got || stolen) {
                for (int i = task.start; i < task.end; i++) {
                    double weight = 0.0;
                    if (!generate_matchup_from_index(i, player_ranges, combo_buffers, use_prefilter, num_players, hands, &weight))
                        continue;
                    if (weight <= 0.0)
                        continue;
                    // Check dead/board overlap
                    int valid = 1;
                    if (!use_prefilter)
                    {
                        for (int p = 0; p < num_players; p++) {
                            if (StdDeck_CardMask_ANY_SET(hands[p], board) || StdDeck_CardMask_ANY_SET(hands[p], dead_cards_initial)) {
                                valid = 0;
                                break;
                            }
                        }
                    }
                    if (!valid) continue;
                    enumResultClear(&matchup_result);
                    StdDeck_CardMask effective_dead_cards;
                    StdDeck_CardMask_RESET(effective_dead_cards);
                    StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, dead_cards_initial);
                    for (int p = 0; p < num_players; p++)
                        StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, hands[p]);
                    StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, board);
                    int num_cards_on_board = StdDeck_numCards(board);
                    int ret_eval;
                    if (use_montecarlo) {
                        ret_eval = enumSample(game, hands, board, effective_dead_cards,
                            num_players, num_cards_on_board, iterations_if_montecarlo,
                            orderflag, &matchup_result);
                    } else {
                        ret_eval = enumExhaustive_dispatch(game, hands, board, effective_dead_cards,
                            num_players, num_cards_on_board, orderflag, &matchup_result);
                    }
                    if (ret_eval == 0) {
                        weightedAggregationAccumulate(&thread_results[tid].agg, &matchup_result, weight, num_players);
                        thread_results[tid].valid_matchups++;
                    }
                }
            }
        }
        enumResultFree(&matchup_result);
    }

    unsigned int total_valid_matchups = 0;
    WeightedAggregation combined_agg;
    weightedAggregationInit(&combined_agg, num_players);
    for (int t = 0; t < num_threads; t++) {
        weightedAggregationMerge(&combined_agg, &thread_results[t].agg, num_players);
        total_valid_matchups += thread_results[t].valid_matchups;
        taskdeque_destroy(&deques[t]);
    }
    aggregated_results->nsamples = total_valid_matchups;
    unsigned int target_samples = weightedAggregationFinalize(&combined_agg, aggregated_results, num_players);
    if (combined_agg.total_weight > 0.0) {
        TRACE_RE("Info: Evaluated %u valid matchups using %d threads (v5 work-stealing, total matchup weight %.3f, weighted samples %.3f).\n",
                 total_valid_matchups, num_threads, combined_agg.total_weight, combined_agg.total_weighted_samples);
    } else {
        TRACE_RE("Warning: No weight accumulated across evaluated matchups (v5 work-stealing).\n");
    }
    if (target_samples > 0) {
        aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    }
    if (use_prefilter)
        range_combo_free_buffers(combo_buffers, num_players);
    return total_valid_matchups;
}
