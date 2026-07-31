/*
 * RangeEquity_MT.c - Multithreaded version of range equity calculations
 * 
 * This implementation uses OpenMP to parallelize the computation of range vs range
 * equity calculations. The main optimization is to distribute independent matchups
 * across multiple threads.
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

// Define TRACE_RE if not already defined
#ifndef TRACE_RE
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
#endif

static int range_mt_debug_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_RANGE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

static int range_mt_combo_stats_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("PE_RANGE_COMBO_TRACE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached || range_mt_debug_enabled();
}

static void range_mt_log_combo_stats(int player_idx, const range_combo_buffer_t *buffer)
{
    if (!range_mt_combo_stats_enabled() || !buffer)
        return;
    TRACE_RE("RangeMT combo stats P%d: total=%d playable=%d total_w=%.2f playable_w=%.2f\n",
             player_idx,
             buffer->stats.total_combos,
             buffer->stats.playable_combos,
             buffer->stats.total_weight,
             buffer->stats.playable_weight);
}

#define RANGE_MT_DEBUG(...)                         \
    do                                              \
    {                                               \
        if (range_mt_debug_enabled())               \
            fprintf(stderr, __VA_ARGS__);           \
    } while (0)

// Structure to hold matchup work item
typedef struct {
    StdDeck_CardMask hands[ENUM_MAXPLAYERS];
    double weight;
    int valid;
} MatchupWorkItem;

// Structure to hold thread-local results
typedef struct {
    WeightedAggregation agg;
    unsigned int valid_matchups;
} ThreadLocalResults;

// Helper function to validate a hand against board and dead cards
static inline int is_hand_valid_mt(StdDeck_CardMask hand, StdDeck_CardMask board, StdDeck_CardMask dead_cards) {
    StdDeck_CardMask combined;
    StdDeck_CardMask_RESET(combined);
    StdDeck_CardMask_OR(combined, board, dead_cards);
    return !StdDeck_CardMask_ANY_SET(hand, combined);
}

// Helper function to check if hands conflict with each other
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

// Generate all valid matchups into a work queue
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
    // Estimate initial capacity
    int estimated_matchups = 1;
    for (int i = 0; i < num_players; i++) {
        int count = range_combo_active_count(use_prefilter, combo_buffers, player_ranges, i);
        if (count <= 0)
            return -1;
        estimated_matchups *= count;
    }
    
    RANGE_MT_DEBUG("RangeMT: generating work queue (%d players, est=%d combos)\n",
                   num_players, estimated_matchups);
    // Allocate work queue
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
    
    // Generate all combinations
    while (1) {
        MatchupWorkItem *item = &(*work_queue)[queue_size];
        item->valid = 1;
        
        // Build current matchup
        StdDeck_CardMask board_plus_dead;
        StdDeck_CardMask_OR(board_plus_dead, board, dead_cards);
        
        double matchup_weight = 1.0;
        for (int p = 0; p < num_players; p++) {
            item->hands[p] = range_combo_active_hand(use_prefilter, combo_buffers, player_ranges, p, indices[p]);
            double weight = range_combo_active_weight(use_prefilter, combo_buffers, player_ranges, p, indices[p]);
            if (weight == 0.0) {
                item->valid = 0;
                RANGE_MT_DEBUG("RangeMT: skip player %d hand %d (weight=0)\n", p, indices[p]);
                break;
            }
            matchup_weight *= weight;
            
            // Check if hand conflicts with board/dead
            if (!use_prefilter && StdDeck_CardMask_ANY_SET(item->hands[p], board_plus_dead)) {
                item->valid = 0;
                RANGE_MT_DEBUG("RangeMT: skip player %d hand %d (conflicts with board/dead)\n",
                               p, indices[p]);
                break;
            }
        }
        
        // Check for inter-hand conflicts if valid so far
        if (item->valid && hands_conflict(item->hands, num_players)) {
            item->valid = 0;
            RANGE_MT_DEBUG("RangeMT: skip matchup %d (inter-player conflict)\n", queue_size);
        }
        
        if (item->valid && matchup_weight > 0.0) {
            item->weight = matchup_weight;
            queue_size++;
            if ((queue_size & 0x3FF) == 0 && range_mt_debug_enabled()) {
                RANGE_MT_DEBUG("RangeMT: queued %d matchups so far...\n", queue_size);
            }
            
            // Reallocate if needed
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
        
        // Increment indices
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
        
        if (carry) break; // All combinations generated
    }
    
    free(indices);
    
    RANGE_MT_DEBUG("RangeMT: final work queue size=%d (capacity=%d)\n",
                   queue_size, *queue_capacity);
    // Shrink to actual size
    if (queue_size < *queue_capacity) {
        MatchupWorkItem *shrunk = realloc(*work_queue, sizeof(MatchupWorkItem) * queue_size);
        if (shrunk) {
            *work_queue = shrunk;
            *queue_capacity = queue_size;
        }
    }
    
    return queue_size;
}

// Multithreaded version of CalculateEquityForRanges
int CalculateEquityForRanges_MT(
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
    RANGE_MT_DEBUG("RangeMT: CalculateEquityForRanges_MT(game=%d, players=%d, montecarlo=%d, iter=%d, board_to_deal=%d, threads=%d)\n",
                   game, num_players, use_montecarlo, iterations_if_montecarlo, nboard_cards_to_deal, num_threads);
    // Validate inputs
    if (num_players <= 0 || num_players > ENUM_MAXPLAYERS) {
        TRACE_RE("Error: Invalid number of players: %d\n", num_players);
        return -1;
    }
    if (!aggregated_results) {
        TRACE_RE("Error: aggregated_results pointer is NULL.\n");
        return -1;
    }
    
    // Validate ranges
    for (int i = 0; i < num_players; i++) {
        if (player_ranges[i].count <= 0 || !player_ranges[i].hand_masks) {
            TRACE_RE("Error: Player %d has an empty or invalid range.\n", i);
            enumResultClear(aggregated_results);
            aggregated_results->nplayers = num_players;
            aggregated_results->game = game;
            return 0;
        }
    }
    
    // Clear aggregated results
    enumResultClear(aggregated_results);
    aggregated_results->game = game;
    aggregated_results->nplayers = num_players;
    aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    
    // Generate work queue
    range_combo_mode_t combo_mode = range_combo_resolve_mode();
    const bool use_prefilter = (combo_mode == RANGE_COMBO_MODE_PREFILTER);
    range_combo_buffer_t combo_buffers[ENUM_MAXPLAYERS];
    memset(combo_buffers, 0, sizeof(combo_buffers));

    if (use_prefilter)
    {
        for (int i = 0; i < num_players; ++i)
        {
            if (range_combo_buffer_build(&combo_buffers[i], &player_ranges[i], board, dead_cards_initial) != 0)
            {
                TRACE_RE("RangeMT: unable to build combo buffer for player %d\n", i);
                for (int j = 0; j <= i; ++j)
                    range_combo_buffer_free(&combo_buffers[j]);
                return -1;
            }
            range_mt_log_combo_stats(i, &combo_buffers[i]);
            if (combo_buffers[i].count == 0)
            {
                TRACE_RE("RangeMT: Player %d has no valid combos after filtering.\n", i);
                for (int j = 0; j < num_players; ++j)
                    range_combo_buffer_free(&combo_buffers[j]);
                return 0;
            }
        }
    }

    MatchupWorkItem *work_queue = NULL;
    int queue_capacity = 0;
    int valid_matchups_total = generate_matchup_work_queue(
        player_ranges, combo_buffers, use_prefilter,
        num_players, board, dead_cards_initial,
        &work_queue, &queue_capacity
    );
    
    if (valid_matchups_total <= 0) {
        TRACE_RE("Warning: No valid matchups found after filtering conflicts.\n");
        if (work_queue) free(work_queue);
        if (use_prefilter)
        {
            for (int i = 0; i < num_players; ++i)
                range_combo_buffer_free(&combo_buffers[i]);
        }
        return 0;
    }
    
    TRACE_RE("Info: Generated %d valid matchups to evaluate.\n", valid_matchups_total);
    RANGE_MT_DEBUG("RangeMT: evaluating %d matchups (queue capacity=%d)\n",
                   valid_matchups_total, queue_capacity);
    
    // Set number of threads
    if (num_threads <= 0) {
        num_threads = omp_get_max_threads();
    }
    omp_set_num_threads(num_threads);
    
    // Allocate thread-local results
    ThreadLocalResults *thread_results = calloc(num_threads, sizeof(ThreadLocalResults));
    if (!thread_results) {
        free(work_queue);
        if (use_prefilter)
        {
            for (int i = 0; i < num_players; ++i)
                range_combo_buffer_free(&combo_buffers[i]);
        }
        return -1;
    }
    
    // Initialize thread-local results
    for (int t = 0; t < num_threads; t++) {
        weightedAggregationInit(&thread_results[t].agg, num_players);
        thread_results[t].valid_matchups = 0;
    }
    
    // Parallel evaluation of matchups
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        RANGE_MT_DEBUG("RangeMT: thread %d starting evaluation chunk\n", thread_id);
        ThreadLocalResults *local_results = &thread_results[thread_id];
        
        int i;
        #pragma omp for schedule(dynamic, 100)
        for (i = 0; i < valid_matchups_total; i++) {
            MatchupWorkItem *item = &work_queue[i];
            
            if (!item->valid) continue;
            
            enum_result_t matchup_result;
            /* Cleared, not allocated: the enumeration below starts with
             * enumResultClear(), which would drop an ordering allocated here. */
            enumResultClear(&matchup_result);
                continue;
            }
            
            int ret_eval;
            StdDeck_CardMask effective_dead_cards_mt;
            StdDeck_CardMask_RESET(effective_dead_cards_mt);
            StdDeck_CardMask_OR(effective_dead_cards_mt, effective_dead_cards_mt, dead_cards_initial);
            for (int p_idx = 0; p_idx < num_players; ++p_idx) {
                StdDeck_CardMask_OR(effective_dead_cards_mt, effective_dead_cards_mt, item->hands[p_idx]);
            }
            StdDeck_CardMask_OR(effective_dead_cards_mt, effective_dead_cards_mt, board); // Restore adding board to dead cards

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
                if (range_mt_debug_enabled()) {
                    if (local_results->valid_matchups <= 3 || (local_results->valid_matchups % 1024) == 0) {
                        RANGE_MT_DEBUG("RangeMT: thread %d processed matchup %u (weight=%.6f nsamples=%u)\n",
                                       thread_id,
                                       local_results->valid_matchups,
                                       item->weight,
                                       matchup_result.nsamples);
                    }
                }
            } else {
                RANGE_MT_DEBUG("RangeMT: thread %d evaluation failed (code=%d)\n",
                               thread_id, ret_eval);
            }
            
            enumResultFree(&matchup_result);
        }
    }
    
    // Aggregate results from all threads
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
        TRACE_RE("Info: Evaluated %u valid matchups using %d threads (total matchup weight %.3f, weighted samples %.3f).\n",
                 total_valid_matchups, num_threads, combined_agg.total_weight, combined_agg.total_weighted_samples);
    } else {
        TRACE_RE("Warning: No weight accumulated across evaluated matchups.\n");
    }
    RANGE_MT_DEBUG("RangeMT: combined weight=%.6f weighted_samples=%.6f target_samples=%u\n",
                   combined_agg.total_weight, combined_agg.total_weighted_samples, target_samples);
    if (target_samples > 0) {
        aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    }
    
    // Cleanup
    free(thread_results);
    free(work_queue);
    if (use_prefilter)
    {
        for (int i = 0; i < num_players; ++i)
            range_combo_buffer_free(&combo_buffers[i]);
    }
    
    return total_valid_matchups;
}

// Wrapper function that automatically chooses between MT and single-threaded
int CalculateEquityForRanges_Auto(
    enum_game_t game,
    const PlayerRange player_ranges[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int nboard_cards_to_deal,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    enum_result_t* aggregated_results
) {
    // Estimate number of matchups
    int estimated_matchups = 1;
    for (int i = 0; i < num_players; i++) {
        estimated_matchups *= player_ranges[i].count;
    }
    
    // Use multithreading if we have enough work
    RANGE_MT_DEBUG("RangeMT: Auto selector estimated_matchups=%d threshold=1000\n", estimated_matchups);
    if (estimated_matchups > 1000) {
        return CalculateEquityForRanges_MT(
            game, player_ranges, num_players, board, dead_cards_initial,
            nboard_cards_to_deal, use_montecarlo, iterations_if_montecarlo,
            orderflag, aggregated_results, 0  // 0 = use all available threads
        );
    } else {
        // Fall back to single-threaded version for small workloads
        return CalculateEquityForRanges(
            game, player_ranges, num_players, board, dead_cards_initial,
            nboard_cards_to_deal, use_montecarlo, iterations_if_montecarlo,
            orderflag, aggregated_results
        );
    }
}
