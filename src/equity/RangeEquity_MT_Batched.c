/*
 * RangeEquity_MT_Batched.c - Multithreaded version with batched Monte-Carlo
 *
 * This version combines:
 * - Thread pool for parallel matchup processing
 * - Batched Monte-Carlo for efficient board sampling
 * - Dynamic work distribution
 * - Thread-local accumulation
 * - Optimized memory access patterns
 */

#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/enumord.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/RangeEquity_internal.h>
#include <poker_eval/equity/range_combo_buffers.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "poker_eval/utils/omp_compat.h"

#ifndef TRACE_RE
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
#endif

static int range_mt_batched_combo_stats_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("PE_RANGE_COMBO_TRACE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

static void range_mt_batched_log_combo_stats(int player_idx, const range_combo_buffer_t *buffer)
{
    if (!range_mt_batched_combo_stats_enabled() || !buffer)
        return;
    TRACE_RE("RangeMT Batched combo stats P%d: total=%d playable=%d total_w=%.2f playable_w=%.2f\n",
             player_idx,
             buffer->stats.total_combos,
             buffer->stats.playable_combos,
             buffer->stats.total_weight,
             buffer->stats.playable_weight);
}

// Configuration for batched processing
#define MATCHUP_BATCH_SIZE 64    // Number of matchups to process per work unit
#define BOARD_BATCH_SIZE 256     // Size of board batches for Monte-Carlo

// Structure for a batch of matchups
typedef struct {
    StdDeck_CardMask hands[ENUM_MAXPLAYERS][MATCHUP_BATCH_SIZE];
    int count;  // Number of valid matchups in this batch
    double weights[MATCHUP_BATCH_SIZE];
} MatchupBatch;

// Work queue for dynamic scheduling
typedef struct {
    const PlayerRange *ranges;
    const range_combo_buffer_t *combo_buffers;
    bool use_prefilter;
    int num_players;
    int *current_indices;
    StdDeck_CardMask board_plus_dead;
    int total_combinations;
    int current_combination;
    omp_lock_t lock;
} WorkQueue;

// Thread-local results with batched processing support
typedef struct {
    WeightedAggregation agg;
    unsigned int valid_matchups;
    unsigned int skipped_hands;
    // Pre-allocated structures for batched evaluation
    BoardBatch board_batch;
    BatchEvalResults batch_eval_results;
} ThreadLocalResultsBatched;

// Initialize work queue
static WorkQueue* create_work_queue(
    const PlayerRange player_ranges[],
    const range_combo_buffer_t combo_buffers[],
    bool use_prefilter,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards
) {
    WorkQueue *queue = malloc(sizeof(WorkQueue));
    if (!queue) return NULL;
    
    queue->ranges = player_ranges;
    queue->combo_buffers = combo_buffers;
    queue->use_prefilter = use_prefilter;
    queue->num_players = num_players;
    queue->current_indices = calloc(num_players, sizeof(int));
    if (!queue->current_indices) {
        free(queue);
        return NULL;
    }
    
    StdDeck_CardMask_OR(queue->board_plus_dead, board, dead_cards);
    
    // Calculate total combinations
    queue->total_combinations = 1;
    for (int i = 0; i < num_players; i++) {
        int count = range_combo_active_count(use_prefilter, combo_buffers, player_ranges, i);
        if (count <= 0) {
            free(queue->current_indices);
            free(queue);
            return NULL;
        }
        queue->total_combinations *= count;
    }
    
    queue->current_combination = 0;
    omp_init_lock(&queue->lock);
    
    return queue;
}

// Get next batch of valid matchups (thread-safe)
static int get_matchup_batch(
    WorkQueue *queue,
    MatchupBatch *batch
) {
    batch->count = 0;
    
    omp_set_lock(&queue->lock);
    
    while (queue->current_combination < queue->total_combinations && 
           batch->count < MATCHUP_BATCH_SIZE) {
        
        // Check if current combination is valid
        int valid = 1;
        StdDeck_CardMask current_hands[ENUM_MAXPLAYERS];
        double matchup_weight = 1.0;
        
        // Get hands for current combination
        for (int p = 0; p < queue->num_players; p++) {
            current_hands[p] = range_combo_active_hand(queue->use_prefilter, queue->combo_buffers, queue->ranges, p, queue->current_indices[p]);
            double weight = range_combo_active_weight(queue->use_prefilter, queue->combo_buffers, queue->ranges, p, queue->current_indices[p]);
            if (weight == 0.0) {
                valid = 0;
                break;
            }
            matchup_weight *= weight;
            
            // Check conflict with board/dead
            if (!queue->use_prefilter && StdDeck_CardMask_ANY_SET(current_hands[p], queue->board_plus_dead)) {
                valid = 0;
                break;
            }
        }
        
        // Check conflicts between players
        if (valid) {
            for (int i = 0; i < queue->num_players - 1; i++) {
                for (int j = i + 1; j < queue->num_players; j++) {
                    if (StdDeck_CardMask_ANY_SET(current_hands[i], current_hands[j])) {
                        valid = 0;
                        break;
                    }
                }
                if (!valid) break;
            }
        }
        
        // Add to batch if valid
        if (valid && matchup_weight > 0.0) {
            for (int p = 0; p < queue->num_players; p++) {
                batch->hands[p][batch->count] = current_hands[p];
            }
            batch->weights[batch->count] = matchup_weight;
            batch->count++;
        }
        
        // Advance to next combination
        queue->current_combination++;
        int carry = 1;
        for (int p = 0; p < queue->num_players && carry; p++) {
            queue->current_indices[p]++;
            int limit = range_combo_active_count(queue->use_prefilter, queue->combo_buffers, queue->ranges, p);
            if (queue->current_indices[p] < limit) {
                carry = 0;
            } else {
                queue->current_indices[p] = 0;
            }
        }
    }
    
    omp_unset_lock(&queue->lock);
    
    return batch->count > 0;
}

// Destroy work queue
static void destroy_work_queue(WorkQueue *queue) {
    if (queue) {
        omp_destroy_lock(&queue->lock);
        free(queue->current_indices);
        free(queue);
    }
}

// Process a batch of matchups using batched Monte-Carlo
static void process_matchup_batch_batched(
    MatchupBatch *matchup_batch,
    enum_game_t game,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards_initial,
    int num_players,
    int use_montecarlo,
    int iterations_if_montecarlo,
    int orderflag,
    ThreadLocalResultsBatched *local_results
) {
    // Process each matchup in the batch
    for (int m = 0; m < matchup_batch->count; m++) {
        double weight = matchup_batch->weights[m];
        if (weight <= 0.0) {
            continue;
        }
        StdDeck_CardMask hands[ENUM_MAXPLAYERS];
        for (int p = 0; p < num_players; p++) {
            hands[p] = matchup_batch->hands[p][m];
        }
        
        // Create effective dead cards for this matchup
        StdDeck_CardMask effective_dead_cards;
        StdDeck_CardMask_RESET(effective_dead_cards);
        StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, dead_cards_initial);
        for (int p = 0; p < num_players; p++) {
            StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, hands[p]);
        }
        StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, board);
        
        enum_result_t matchup_result;
        if (enumResultAlloc(&matchup_result, num_players, enum_ordering_mode_hi) != 0) {
            continue;
        }
        
        int num_cards_on_board = StdDeck_numCards(board);
        int ret_eval;
        
        if (use_montecarlo && iterations_if_montecarlo >= BOARD_BATCH_SIZE * 2) {
            // Use batched Monte-Carlo for large iterations
            ret_eval = enumSampleBatched(game, hands, board, effective_dead_cards,
                                       num_players, num_cards_on_board, 
                                       iterations_if_montecarlo,
                                       orderflag, &matchup_result);
        } else if (use_montecarlo) {
            // Use regular Monte-Carlo for small iterations
            ret_eval = enumSample(game, hands, board, effective_dead_cards,
                                num_players, num_cards_on_board, 
                                iterations_if_montecarlo,
                                orderflag, &matchup_result);
        } else {
            // Use exhaustive enumeration
            ret_eval = enumExhaustive_dispatch(game, hands, board, effective_dead_cards,
                                               num_players, num_cards_on_board, 
                                               orderflag, &matchup_result);
        }
        
        if (ret_eval == 0) {
            weightedAggregationAccumulate(&local_results->agg, &matchup_result, weight, num_players);
            local_results->valid_matchups++;
        }
        
        enumResultFree(&matchup_result);
    }
}

// Main multithreaded function with batched Monte-Carlo
int CalculateEquityForRanges_MT_Batched(
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
    
    // Initialize results
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
            TRACE_RE("RangeMT Batched: failed to build combo buffers\n");
            return -1;
        }
        for (int i = 0; i < num_players; ++i)
            range_mt_batched_log_combo_stats(i, &combo_buffers[i]);
    }

    // Create work queue
    WorkQueue *queue = create_work_queue(player_ranges, combo_buffers, use_prefilter, num_players, board, dead_cards_initial);
    if (!queue) {
        TRACE_RE("Error: Failed to create work queue\n");
        if (use_prefilter)
            range_combo_free_buffers(combo_buffers, num_players);
        return -1;
    }
    
    // Determine number of threads
    if (num_threads <= 0) {
        num_threads = omp_get_max_threads();
    }
    omp_set_num_threads(num_threads);

    /* Total Monte-Carlo budget, divided across matchups. queue->total_combinations
     * is an upper bound on the number of valid matchups (product of per-player
     * combo counts); dividing the caller's iteration budget by it keeps the whole
     * call within budget (see range_equity_per_matchup_budget). */
    int per_matchup_iterations = range_equity_per_matchup_budget(
        iterations_if_montecarlo, (double)queue->total_combinations);
    
    // Allocate thread-local results
    ThreadLocalResultsBatched *thread_results = calloc(num_threads, sizeof(ThreadLocalResultsBatched));
    if (!thread_results) {
        destroy_work_queue(queue);
        return -1;
    }
    
    // Initialize thread-local results
    for (int t = 0; t < num_threads; t++) {
        weightedAggregationInit(&thread_results[t].agg, num_players);
        thread_results[t].valid_matchups = 0;
        thread_results[t].skipped_hands = 0;
    }
    
    // Process matchups in parallel
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        ThreadLocalResultsBatched *local_results = &thread_results[thread_id];
        MatchupBatch batch;
        
        // Process batches until work queue is empty
        while (get_matchup_batch(queue, &batch)) {
            process_matchup_batch_batched(
                &batch, game, board, dead_cards_initial,
                num_players, use_montecarlo, per_matchup_iterations,
                orderflag, local_results
            );
        }
    }
    
    // Aggregate results from all threads
    unsigned int total_valid_matchups = 0;
    WeightedAggregation combined_agg;
    weightedAggregationInit(&combined_agg, num_players);
    for (int t = 0; t < num_threads; t++) {
        ThreadLocalResultsBatched *local_results = &thread_results[t];
        weightedAggregationMerge(&combined_agg, &local_results->agg, num_players);
        total_valid_matchups += local_results->valid_matchups;
    }
    
    aggregated_results->nsamples = total_valid_matchups;
    unsigned int target_samples = weightedAggregationFinalize(&combined_agg, aggregated_results, num_players);
    if (combined_agg.total_weight > 0.0) {
        TRACE_RE("Info: Evaluated %u valid matchups using %d threads (MT+Batched, total matchup weight %.3f, weighted samples %.3f).\n", 
                 total_valid_matchups, num_threads, combined_agg.total_weight, combined_agg.total_weighted_samples);
    } else {
        TRACE_RE("Warning: No weight accumulated across evaluated matchups (MT+Batched).\n");
    }
    if (target_samples > 0) {
        aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    }
    
    // Clean up
    free(thread_results);
    destroy_work_queue(queue);
    if (use_prefilter)
        range_combo_free_buffers(combo_buffers, num_players);
    
    return total_valid_matchups;
}
