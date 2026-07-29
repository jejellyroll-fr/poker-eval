/*
 * RangeEquity_MT_v4.c - Optimized multithreaded version with persistent thread pool
 *
 * - Thread pool persistant (réutilisable)
 * - Génération paresseuse des matchups (faible empreinte mémoire)
 * - Scheduling dynamique et adaptatif
 * - Accumulation thread-local
 * - Synchronisation efficace des résultats
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

static int range_mt4_combo_stats_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("PE_RANGE_COMBO_TRACE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

static void range_mt4_log_combo_stats(int player_idx, const range_combo_buffer_t *buffer)
{
    if (!range_mt4_combo_stats_enabled() || !buffer)
        return;
    TRACE_RE("RangeMTv4 combo stats P%d: total=%d playable=%d total_w=%.2f playable_w=%.2f\n",
             player_idx,
             buffer->stats.total_combos,
             buffer->stats.playable_combos,
             buffer->stats.total_weight,
             buffer->stats.playable_weight);
}

#ifndef TRACE_RE
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
#endif

// Structure pour la génération paresseuse des matchups
typedef struct {
    const PlayerRange *ranges;
    const range_combo_buffer_t *combo_buffers;
    bool use_prefilter;
    int num_players;
    int *indices;
    StdDeck_CardMask board_plus_dead;
    int total_combinations;
    int current_combination;
    omp_lock_t lock;
} MatchupGenerator;

// Résultats locaux par thread
typedef struct {
    WeightedAggregation agg;
    unsigned int valid_matchups;
} ThreadLocalResults;

// Initialisation du générateur de matchups
static MatchupGenerator* create_matchup_generator(
    const PlayerRange player_ranges[],
    const range_combo_buffer_t combo_buffers[],
    bool use_prefilter,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards
) {
    MatchupGenerator *gen = malloc(sizeof(MatchupGenerator));
    if (!gen) return NULL;
    gen->ranges = player_ranges;
    gen->combo_buffers = combo_buffers;
    gen->use_prefilter = use_prefilter;
    gen->num_players = num_players;
    gen->indices = calloc(num_players, sizeof(int));
    if (!gen->indices) {
        free(gen);
        return NULL;
    }
    StdDeck_CardMask_OR(gen->board_plus_dead, board, dead_cards);
    gen->total_combinations = 1;
    for (int i = 0; i < num_players; i++) {
        int count = range_combo_active_count(use_prefilter, combo_buffers, player_ranges, i);
        if (count <= 0) {
            free(gen->indices);
            free(gen);
            return NULL;
        }
        gen->total_combinations *= count;
    }
    gen->current_combination = 0;
    omp_init_lock(&gen->lock);
    return gen;
}

// Récupère le prochain matchup valide (thread-safe)
static int get_next_valid_matchup(
    MatchupGenerator *gen,
    StdDeck_CardMask hands[ENUM_MAXPLAYERS],
    double *weight_out
) {
    if (weight_out)
        *weight_out = 0.0;
    int found = 0;
    omp_set_lock(&gen->lock);
    while (gen->current_combination < gen->total_combinations && !found) {
        int valid = 1;
        double matchup_weight = 1.0;
        for (int p = 0; p < gen->num_players; p++) {
            hands[p] = range_combo_active_hand(gen->use_prefilter, gen->combo_buffers, gen->ranges, p, gen->indices[p]);
            double weight = range_combo_active_weight(gen->use_prefilter, gen->combo_buffers, gen->ranges, p, gen->indices[p]);
            if (weight == 0.0) {
                valid = 0;
                break;
            }
            matchup_weight *= weight;
            if (!gen->use_prefilter && StdDeck_CardMask_ANY_SET(hands[p], gen->board_plus_dead)) {
                valid = 0;
                break;
            }
        }
        if (valid) {
            for (int i = 0; i < gen->num_players - 1; i++) {
                for (int j = i + 1; j < gen->num_players; j++) {
                    if (StdDeck_CardMask_ANY_SET(hands[i], hands[j])) {
                        valid = 0;
                        break;
                    }
                }
                if (!valid) break;
            }
        }
        if (valid) {
            found = 1;
            if (weight_out)
                *weight_out = matchup_weight;
        }
        gen->current_combination++;
        int carry = 1;
        for (int p = 0; p < gen->num_players && carry; p++) {
            gen->indices[p]++;
            int limit = range_combo_active_count(gen->use_prefilter, gen->combo_buffers, gen->ranges, p);
            if (gen->indices[p] < limit) {
                carry = 0;
            } else {
                gen->indices[p] = 0;
            }
        }
    }
    omp_unset_lock(&gen->lock);
    return found;
}

static void destroy_matchup_generator(MatchupGenerator *gen) {
    if (gen) {
        omp_destroy_lock(&gen->lock);
        free(gen->indices);
        free(gen);
    }
}

// Thread pool abstraction (OpenMP threads persistent sur la durée du calcul)
typedef struct {
    int num_threads;
    MatchupGenerator *gen;
    enum_game_t game;
    StdDeck_CardMask board;
    StdDeck_CardMask dead_cards_initial;
    int num_players;
    int use_montecarlo;
    int iterations_if_montecarlo;
    int orderflag;
    ThreadLocalResults *thread_results;
    int batch_size;
} ThreadPoolContext;

static void process_matchup_batch(
    ThreadPoolContext *ctx,
    ThreadLocalResults *local_results
) {
    StdDeck_CardMask hands[ENUM_MAXPLAYERS];
    enum_result_t matchup_result;
    if (enumResultAlloc(&matchup_result, ctx->num_players, enum_ordering_mode_hi) != 0) {
        return;
    }
    for (int b = 0; b < ctx->batch_size; b++) {
        double weight = 0.0;
        if (!get_next_valid_matchup(ctx->gen, hands, &weight)) {
            break;
        }
        if (weight <= 0.0) {
            continue;
        }
        enumResultClear(&matchup_result);
        StdDeck_CardMask effective_dead_cards;
        StdDeck_CardMask_RESET(effective_dead_cards);
        StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, ctx->dead_cards_initial);
        for (int p = 0; p < ctx->num_players; p++) {
            StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, hands[p]);
        }
        StdDeck_CardMask_OR(effective_dead_cards, effective_dead_cards, ctx->board);
        int num_cards_on_board = StdDeck_numCards(ctx->board);
        int ret_eval;
        if (ctx->use_montecarlo) {
            ret_eval = enumSample(ctx->game, hands, ctx->board, effective_dead_cards,
                                ctx->num_players, num_cards_on_board, ctx->iterations_if_montecarlo,
                                ctx->orderflag, &matchup_result);
        } else {
            ret_eval = enumExhaustive_dispatch(ctx->game, hands, ctx->board, effective_dead_cards,
                                               ctx->num_players, num_cards_on_board, ctx->orderflag,
                                               &matchup_result);
        }
        if (ret_eval == 0) {
            weightedAggregationAccumulate(&local_results->agg, &matchup_result, weight, ctx->num_players);
            local_results->valid_matchups++;
        }
    }
    enumResultFree(&matchup_result);
}

// Version finale avec thread pool persistant
int CalculateEquityForRanges_MT_v4(
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
            TRACE_RE("RangeMTv4: failed to build combo buffers\n");
            return -1;
        }
        for (int i = 0; i < num_players; ++i)
            range_mt4_log_combo_stats(i, &combo_buffers[i]);
    }

    MatchupGenerator *gen = create_matchup_generator(
        player_ranges, combo_buffers, use_prefilter, num_players, board, dead_cards_initial
    );
    if (!gen) {
        TRACE_RE("Error: Failed to create matchup generator\n");
        if (use_prefilter)
            range_combo_free_buffers(combo_buffers, num_players);
        return -1;
    }
    if (num_threads <= 0) {
        num_threads = omp_get_max_threads();
    }
    omp_set_num_threads(num_threads);
    ThreadLocalResults *thread_results = calloc(num_threads, sizeof(ThreadLocalResults));
    if (!thread_results) {
        destroy_matchup_generator(gen);
        return -1;
    }
    for (int t = 0; t < num_threads; t++) {
        weightedAggregationInit(&thread_results[t].agg, num_players);
        thread_results[t].valid_matchups = 0;
    }
    // Batch size adaptatif
    int batch_size = 100;
    if (gen->total_combinations > 10000) {
        batch_size = gen->total_combinations / (num_threads * 100);
        if (batch_size < 10) batch_size = 10;
        if (batch_size > 1000) batch_size = 1000;
    }
    ThreadPoolContext ctx = {
        .num_threads = num_threads,
        .gen = gen,
        .game = game,
        .board = board,
        .dead_cards_initial = dead_cards_initial,
        .num_players = num_players,
        .use_montecarlo = use_montecarlo,
        .iterations_if_montecarlo = iterations_if_montecarlo,
        .orderflag = orderflag,
        .thread_results = thread_results,
        .batch_size = batch_size
    };
    // Thread pool OpenMP (persistant sur la durée du calcul)
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        ThreadLocalResults *local_results = &thread_results[thread_id];
        while (1) {
            int start_comb;
            #pragma omp critical
            {
                start_comb = gen->current_combination;
            }
            if (start_comb >= gen->total_combinations) {
                break;
            }
            process_matchup_batch(&ctx, local_results);
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
        TRACE_RE("Info: Evaluated %u valid matchups using %d threads (v4 pool, total matchup weight %.3f, weighted samples %.3f).\n", 
                 total_valid_matchups, num_threads, combined_agg.total_weight, combined_agg.total_weighted_samples);
    } else {
        TRACE_RE("Warning: No weight accumulated across evaluated matchups (v4 pool).\n");
    }
    if (target_samples > 0) {
        aggregated_results->sampleType = use_montecarlo ? ENUM_SAMPLE : ENUM_EXHAUSTIVE;
    }
    free(thread_results);
    destroy_matchup_generator(gen);
    if (use_prefilter)
        range_combo_free_buffers(combo_buffers, num_players);
    return total_valid_matchups;
}
