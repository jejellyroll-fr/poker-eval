/*
 * batched_montecarlo.c - Batched Monte-Carlo implementation for poker evaluation
 *
 * Copyright (C) 2024
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/sampling_policies.h>
#include <poker_eval/core/eval_cache.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/games/eval_low27.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/sampling_policies.h>
#include <poker_eval/equity/range_combo_buffers.h>
#include <poker_eval/equity/simd_operations.h>

static int batched_mc_debug_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_BATCHED_MC");
        if (!env || !*env)
            env = getenv("POKER_DEBUG_RANGE");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

#define BATCH_DEBUG(...)                         \
    do                                           \
    {                                            \
        if (batched_mc_debug_enabled())          \
            fprintf(stderr, __VA_ARGS__);        \
    } while (0)
#define COMBO_TRACE_LOG(...)                      \
    do                                           \
    {                                            \
        if (batched_mc_combo_trace_enabled())     \
            fprintf(stderr, __VA_ARGS__);        \
    } while (0)

static bool batched_mc_combo_trace_enabled(void)
{
    static int enabled = -1;
    if (enabled == -1)
    {
        const char *env = getenv("PE_RANGE_COMBO_TRACE");
        enabled = (env && *env && env[0] != '0') ? 1 : 0;
    }
    return enabled;
}

static void batched_mc_log_range_combo(const StdDeck_CardMask board,
                                       const StdDeck_CardMask dead_cards,
                                       StdDeck_CardMask pockets[],
                                       int npockets)
{
    if (!batched_mc_combo_trace_enabled() || npockets <= 0)
        return;

    PlayerRange ranges[ENUM_MAXPLAYERS];
    range_combo_buffer_t buffers[ENUM_MAXPLAYERS];
    int count = (npockets > ENUM_MAXPLAYERS) ? ENUM_MAXPLAYERS : npockets;

    for (int p = 0; p < count; ++p) {
        ranges[p].hand_masks = &pockets[p];
        ranges[p].weights = NULL;
        ranges[p].count = 1;
        ranges[p].total_weight = 1.0;
        buffers[p].source = NULL;
        buffers[p].indices = NULL;
        buffers[p].count = 0;
        buffers[p].stats.total_combos = 0;
        buffers[p].stats.playable_combos = 0;
        buffers[p].stats.total_weight = 0.0;
        buffers[p].stats.playable_weight = 0.0;
    }

    if (range_combo_build_buffers(buffers, ranges, count, board, dead_cards) != 0) {
        range_combo_free_buffers(buffers, count);
        return;
    }

    COMBO_TRACE_LOG("PE_RANGE_COMBO_TRACE board cards=%d players=%d\n", (int)board.cards_n, count);
    for (int p = 0; p < count; ++p) {
        COMBO_TRACE_LOG("  player %d combos total=%d playable=%d weight=%.2f/%.2f\n",
                        p,
                        buffers[p].stats.total_combos,
                        buffers[p].stats.playable_combos,
                        buffers[p].stats.total_weight,
                        buffers[p].stats.playable_weight);
    }

    range_combo_free_buffers(buffers, count);
}
#ifdef USE_GPU
#include <poker_eval/gpu/eval_gpu.h>

static gpu_eval_context_t* g_gpu_eval_ctx = NULL;
static bool g_gpu_checked = false;
static int g_gpu_backend = -1;
static enum_game_t g_gpu_current_game = game_holdem;

static void batched_mc_gpu_shutdown(void)
{
    if (g_gpu_eval_ctx)
    {
        gpu_eval_cleanup(g_gpu_eval_ctx);
        g_gpu_eval_ctx = NULL;
    }
}

static int batched_mc_gpu_select_backend(void)
{
    if (g_gpu_backend >= 0)
    {
        return g_gpu_backend;
    }
    if (gpu_is_available(0))
    {
        g_gpu_backend = 0;
    }
    else if (gpu_is_available(1))
    {
        g_gpu_backend = 1;
    }
    else
    {
        g_gpu_backend = -1;
    }
    return g_gpu_backend;
}

static gpu_game_config_t batched_mc_get_gpu_config(enum_game_t game)
{
    switch (game)
    {
        case game_holdem:   return gpu_game_config_holdem();
        case game_holdem8:  return gpu_game_config_holdem8();
        case game_omaha:    return gpu_game_config_omaha(4);
        case game_omaha5:   return gpu_game_config_omaha(5);
        case game_omaha6:   return gpu_game_config_omaha(6);
        case game_omaha8:   return gpu_game_config_omaha8(4);
        /* Add other games as supported by GPU kernels */
        default:            return gpu_game_config_holdem(); /* Fallback */
    }
}

static bool batched_mc_gpu_try_backends(enum_game_t game)
{
    /* Temporarily disable GPU for Omaha 5/6 Hi/Lo until specific kernels are verified */
    if (game == game_omaha85 || game == game_omaha86) {
        return false;
    }

    gpu_game_config_t config = batched_mc_get_gpu_config(game);
    int preferred = batched_mc_gpu_select_backend();
    int candidates[2] = {preferred, preferred == 0 ? 1 : 0};
    for (int i = 0; i < 2; i++)
    {
        int backend = candidates[i];
        if (backend < 0 || backend > 1)
            continue;
        if (!gpu_is_available(backend))
            continue;

        if (g_gpu_eval_ctx) {
            gpu_eval_cleanup(g_gpu_eval_ctx);
            g_gpu_eval_ctx = NULL;
        }

        g_gpu_eval_ctx = gpu_eval_init_game(0, BATCH_SIZE, backend, config);
        if (g_gpu_eval_ctx)
        {
            g_gpu_backend = backend;
            g_gpu_current_game = game;
            return true;
        }
    }
    return false;
}

static bool batched_mc_gpu_ensure_context(enum_game_t game)
{
    /* If context exists but for different game, re-init */
    if (g_gpu_checked && g_gpu_eval_ctx && g_gpu_current_game != game)
    {
        if (!batched_mc_gpu_try_backends(game)) {
            /* If re-init fails, we are in bad state, but maybe next time it works */
            return false;
        }
        return true;
    }

    if (!g_gpu_checked)
    {
        g_gpu_checked = true;
        atexit(batched_mc_gpu_shutdown); /* Register once */
        if (!batched_mc_gpu_try_backends(game))
        {
            return false;
        }
    }
    return g_gpu_eval_ctx != NULL;
}

#endif

#ifndef RANDOM
#define RANDOM rand
#endif

/* Generate a batch of random boards */
void generateBoardBatch(BoardBatch *batch, StdDeck_CardMask dead,
                        int numCards, int batchSize)
{
    /* Delegate to policy-aware generator (MC/QMC/importance) */
    pe_sampling_generate_boards(batch, dead, numCards, batchSize);
}

/* Generate a batch of stub hands for Stud/Draw games */
void generateStubBatch(StubBatch *batch, StdDeck_CardMask dead,
                       int numToDeal[], int npockets, int batchSize)
{
    int i, p, j, c;

    batch->count = batchSize;

    /* For each sample in the batch */
    for (i = 0; i < batchSize; i++)
    {
        StdDeck_CardMask used = dead;

        /* Deal cards to each player */
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask_RESET(batch->hands[i][p]);
            int count = numToDeal[p];

            if (count <= 0) continue;

            /* Draw 'count' cards */
            for (j = 0; j < count; j++)
            {
                do {
                    c = RANDOM() % StdDeck_N_CARDS;
                } while (StdDeck_CardMask_CARD_IS_SET(used, c));

                StdDeck_CardMask_SET(batch->hands[i][p], c);
                StdDeck_CardMask_SET(used, c);
            }
        }
    }
}

/* Evaluate a batch of boards for Hold'em */
void evaluateBatchHoldem(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                         StdDeck_CardMask board, BoardBatch *batch,
                         BatchEvalResults *results)
{
    int i, p;
    StdDeck_CardMask empty_dead;
    StdDeck_CardMask_RESET(empty_dead);

    /* Process each board in the batch */
    for (i = 0; i < batch->count; i++)
    {
        StdDeck_CardMask finalBoard;
        StdDeck_CardMask_OR(finalBoard, board, batch->boards[i]);
        batched_mc_log_range_combo(finalBoard, empty_dead, pockets, npockets);

        /* Evaluate each player's hand */
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], finalBoard);
            results->hival[p][i] = StdDeck_StdRules_EVAL_N_Cached(hand, 7);
            results->loval[p][i] = LowHandVal_NOTHING;
            if (batched_mc_debug_enabled() && i < 3)
            {
                BATCH_DEBUG("BMC hold'em board #%d player %d hi=%u\n",
                            i, p, results->hival[p][i]);
            }
        }
    }
}

/* Evaluate a batch of stub hands for 7-Card Stud */
void evaluateBatchStud(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                       StubBatch *batch, BatchEvalResults *results)
{
    int i, p;

#ifdef USE_SIMD
    if (simd_detect_capability() != SIMD_NONE) {
        StdDeck_CardMask hands[BATCH_SIZE];
        HandVal hvals[BATCH_SIZE];

        for (p = 0; p < npockets; p++) {
            for (i = 0; i < batch->count; i++) {
                StdDeck_CardMask_OR(hands[i], pockets[p], batch->hands[i][p]);
            }
            simd_eval_multiple_hands(hands, batch->count, hvals);
            for (i = 0; i < batch->count; i++) {
                results->hival[p][i] = hvals[i];
                results->loval[p][i] = LowHandVal_NOTHING;
            }
        }
        return;
    }
#endif

    /* Process each sample in the batch */
    for (i = 0; i < batch->count; i++)
    {
        /* Evaluate each player's hand */
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], batch->hands[i][p]);
            results->hival[p][i] = StdDeck_StdRules_EVAL_N(hand, 7);
            results->loval[p][i] = LowHandVal_NOTHING;
        }
    }
}

/* Evaluate a batch of stub hands for 7-Card Stud Hi/Lo */
void evaluateBatchStud8(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                        StubBatch *batch, BatchEvalResults *results)
{
    int i, p;

#ifdef USE_SIMD
    if (simd_detect_capability() != SIMD_NONE) {
        StdDeck_CardMask hands[BATCH_SIZE];
        HandVal hvals[BATCH_SIZE];
        LowHandVal lvals[BATCH_SIZE];

        for (p = 0; p < npockets; p++) {
            for (i = 0; i < batch->count; i++) {
                StdDeck_CardMask_OR(hands[i], pockets[p], batch->hands[i][p]);
            }
            simd_eval_multiple_hands(hands, batch->count, hvals);
            simd_eval_low8_multiple_hands(hands, batch->count, lvals);
            for (i = 0; i < batch->count; i++) {
                results->hival[p][i] = hvals[i];
                results->loval[p][i] = pe_low_qualify5(lvals[i], LOW_QUALIFIER_8) ? lvals[i] : LowHandVal_NOTHING;
            }
        }
        return;
    }
#endif

    /* Process each sample in the batch */
    for (i = 0; i < batch->count; i++)
    {
        /* Evaluate each player's hand */
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], batch->hands[i][p]);
            results->hival[p][i] = StdDeck_StdRules_EVAL_N(hand, 7);
            LowHandVal lo = StdDeck_Lowball8_EVAL(hand, 7);
            results->loval[p][i] = pe_low_qualify5(lo, LOW_QUALIFIER_8) ? lo : LowHandVal_NOTHING;
        }
    }
}

/* Evaluate a batch of stub hands for Razz */
void evaluateBatchRazz(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                       StubBatch *batch, BatchEvalResults *results)
{
    int i, p;

#ifdef USE_SIMD
    if (simd_detect_capability() != SIMD_NONE) {
        StdDeck_CardMask hands[BATCH_SIZE];
        LowHandVal lvals[BATCH_SIZE];

        for (p = 0; p < npockets; p++) {
            for (i = 0; i < batch->count; i++) {
                StdDeck_CardMask_OR(hands[i], pockets[p], batch->hands[i][p]);
            }
            simd_eval_low8_multiple_hands(hands, batch->count, lvals);
            for (i = 0; i < batch->count; i++) {
                results->hival[p][i] = HandVal_NOTHING;
                results->loval[p][i] = lvals[i];
            }
        }
        return;
    }
#endif

    /* Process each sample in the batch */
    for (i = 0; i < batch->count; i++)
    {
        /* Evaluate each player's hand */
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], batch->hands[i][p]);
            results->hival[p][i] = HandVal_NOTHING;
            results->loval[p][i] = pe_eval_low_a5(hand);
        }
    }
}

/* Evaluate a batch of stub hands for 7-Card Stud NSQ (Low is A-5) */
/* Note: 7studnsq in enumerate.c uses pe_eval_low_a5 without qualification */
void evaluateBatchStudNSQ(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                          StubBatch *batch, BatchEvalResults *results)
{
    int i, p;

#ifdef USE_SIMD
    if (simd_detect_capability() != SIMD_NONE) {
        StdDeck_CardMask hands[BATCH_SIZE];
        HandVal hvals[BATCH_SIZE];
        LowHandVal lvals[BATCH_SIZE];

        for (p = 0; p < npockets; p++) {
            for (i = 0; i < batch->count; i++) {
                StdDeck_CardMask_OR(hands[i], pockets[p], batch->hands[i][p]);
            }
            simd_eval_multiple_hands(hands, batch->count, hvals);
            simd_eval_low8_multiple_hands(hands, batch->count, lvals);
            for (i = 0; i < batch->count; i++) {
                results->hival[p][i] = hvals[i];
                results->loval[p][i] = lvals[i];
            }
        }
        return;
    }
#endif

    /* Process each sample in the batch */
    for (i = 0; i < batch->count; i++)
    {
        /* Evaluate each player's hand */
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], batch->hands[i][p]);
            results->hival[p][i] = StdDeck_StdRules_EVAL_N(hand, 7);
            results->loval[p][i] = pe_eval_low_a5(hand);
        }
    }
}

/* Evaluate a batch of stub hands for 2-7 Lowball (5 cards) */
void evaluateBatchLowball27(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                            StubBatch *batch, BatchEvalResults *results)
{
    int i, p;

#ifdef USE_SIMD
    if (simd_detect_capability() != SIMD_NONE) {
        for (p = 0; p < npockets; ++p) {
            int processed = 0;
            while (processed < batch->count) {
                int chunk = batch->count - processed;
                if (chunk > SIMD_AVX512_BATCH_SIZE) chunk = SIMD_AVX512_BATCH_SIZE;

                StdDeck_CardMask hands[SIMD_AVX512_BATCH_SIZE];
                for (int k=0; k<chunk; ++k) {
                    StdDeck_CardMask_OR(hands[k], pockets[p], batch->hands[processed+k][p]);
                }

                LowHandVal lvals[SIMD_AVX512_BATCH_SIZE];
                simd_eval_low27_multiple_hands(hands, chunk, lvals);

                for (int k=0; k<chunk; ++k) {
                    results->hival[p][processed+k] = HandVal_NOTHING;
                    results->loval[p][processed+k] = lvals[k];
                }
                processed += chunk;
            }
        }
        return;
    }
#endif

    for (i = 0; i < batch->count; i++)
    {
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], batch->hands[i][p]);
            results->hival[p][i] = HandVal_NOTHING;
            /* StdDeck_Lowball27_EVAL_N returns a High value where lower is better for 2-7 */
            /* But we store it in loval to use enumResult LO logic which minimizes value */
            results->loval[p][i] = StdDeck_Lowball27_EVAL_N(hand, 5);
        }
    }
}

/* Evaluate a batch of stub hands for 2-7 Triple Draw (5 cards) */
void evaluateBatchTripleDraw27(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                               StubBatch *batch, BatchEvalResults *results)
{
    evaluateBatchLowball27(game, pockets, npockets, batch, results);
}

/* Evaluate a batch of stub hands for A-5 Triple Draw (5 cards) */
void evaluateBatchTripleDrawA5(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                               StubBatch *batch, BatchEvalResults *results)
{
    int i, p;

    for (i = 0; i < batch->count; i++)
    {
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], batch->hands[i][p]);
            results->hival[p][i] = HandVal_NOTHING;
            results->loval[p][i] = pe_eval_low_a5(hand);
        }
    }
}

#ifdef USE_SIMD
/* SIMD-optimized version for Omaha evaluation */
static void evaluateBatchOmahaSIMD(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                   StdDeck_CardMask board, BoardBatch *batch,
                                   BatchEvalResults *results)
{
    simd_capability_t cap = simd_detect_capability();
    if (cap == SIMD_NONE)
    {
        evaluateBatchOmaha(game, pockets, npockets, board, batch, results);
        return;
    }

    int hole_combos[15][2]; /* Max PLO6 = 15 combos */
    int n_hole_combos = 0;
    int board_combos[10][3]; /* 5C3 = 10 combos */
    int n_board_combos = 0;

    /* Iterate over players */
    for (int p = 0; p < npockets; ++p)
    {
        /* Extract hole cards for player p */
        int p_cards[6];
        int n_p_cards = 0;
        for (int c=0; c<52; ++c) {
            if (StdDeck_CardMask_CARD_IS_SET(pockets[p], c)) {
                if (n_p_cards < 6) p_cards[n_p_cards++] = c;
            }
        }

        if (n_p_cards < 4) {
             for(int i=0; i<batch->count; ++i) {
                 results->hival[p][i] = HandVal_NOTHING;
                 results->loval[p][i] = LowHandVal_NOTHING;
             }
             continue;
        }

        /* Generate hole combos indices */
        n_hole_combos = 0;
        for (int i=0; i<n_p_cards-1; ++i) {
            for (int j=i+1; j<n_p_cards; ++j) {
                hole_combos[n_hole_combos][0] = p_cards[i];
                hole_combos[n_hole_combos][1] = p_cards[j];
                n_hole_combos++;
            }
        }

        int total = batch->count;
        for (int b_idx = 0; b_idx < total; ++b_idx)
        {
            /* Extract board cards */
            StdDeck_CardMask finalBoard;
            StdDeck_CardMask_OR(finalBoard, board, batch->boards[b_idx]);
            int b_cards[5];
            int n_b_cards = 0;
            for (int c=0; c<52; ++c) {
                if (StdDeck_CardMask_CARD_IS_SET(finalBoard, c)) {
                    if (n_b_cards < 5) b_cards[n_b_cards++] = c;
                }
            }

            if (n_b_cards < 3) {
                results->hival[p][b_idx] = HandVal_NOTHING;
                results->loval[p][b_idx] = LowHandVal_NOTHING;
                continue;
            }

            /* Generate board combos indices */
            n_board_combos = 0;
            for (int i=0; i<n_b_cards-2; ++i) {
                for (int j=i+1; j<n_b_cards-1; ++j) {
                    for (int k=j+1; k<n_b_cards; ++k) {
                        board_combos[n_board_combos][0] = b_cards[i];
                        board_combos[n_board_combos][1] = b_cards[j];
                        board_combos[n_board_combos][2] = b_cards[k];
                        n_board_combos++;
                    }
                }
            }

            int total_combos = n_hole_combos * n_board_combos;
            StdDeck_CardMask hands[150];
            int idx = 0;

            for (int hc=0; hc < n_hole_combos; ++hc) {
                for (int bc=0; bc < n_board_combos; ++bc) {
                    StdDeck_CardMask_RESET(hands[idx]);
                    StdDeck_CardMask_SET(hands[idx], hole_combos[hc][0]);
                    StdDeck_CardMask_SET(hands[idx], hole_combos[hc][1]);
                    StdDeck_CardMask_SET(hands[idx], board_combos[bc][0]);
                    StdDeck_CardMask_SET(hands[idx], board_combos[bc][1]);
                    StdDeck_CardMask_SET(hands[idx], board_combos[bc][2]);
                    idx++;
                }
            }

            HandVal hvals[150];
            simd_eval_multiple_hands(hands, total_combos, hvals);

            HandVal best_hi = HandVal_NOTHING;
            for (int i=0; i<total_combos; ++i) {
                if (hvals[i] > best_hi || best_hi == HandVal_NOTHING) {
                    best_hi = hvals[i];
                }
            }
            results->hival[p][b_idx] = best_hi;

            LowHandVal best_lo = LowHandVal_NOTHING;
            /* Simple heuristic: if board has < 3 low cards, no low possible */
            int low_cards_on_board = 0;
            for (int i=0; i<n_b_cards; ++i) {
                if (StdDeck_RANK(b_cards[i]) <= StdDeck_Rank_8) low_cards_on_board++;
            }

            if (low_cards_on_board >= 3) {
                LowHandVal lvals[150];
                simd_eval_low8_multiple_hands(hands, total_combos, lvals);
                for (int i=0; i<total_combos; ++i) {
                    LowHandVal lo = lvals[i];
                    if (pe_low_qualify5(lo, LOW_QUALIFIER_8)) {
                        if (lo < best_lo || best_lo == LowHandVal_NOTHING) {
                            best_lo = lo;
                        }
                    }
                }
            }
            results->loval[p][b_idx] = best_lo;
        }
    }
}

/* Wrapper for Omaha dispatch logic */
static void evaluateBatchOmahaDispatch(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                       StdDeck_CardMask board, BoardBatch *batch,
                                       BatchEvalResults *results)
{
#ifdef USE_GPU
    if (batched_mc_gpu_ensure_context(game))
    {
        evaluateBatchOmahaGPU(game, pockets, npockets, board, batch, results);
        return;
    }
#endif
    int simd_ok = (simd_detect_capability() != SIMD_NONE);
    const char *pe_disable = getenv("PE_DISABLE_SIMD");
    if (pe_disable && (strcmp(pe_disable, "1") == 0 || strcmp(pe_disable, "true") == 0 || strcmp(pe_disable, "TRUE") == 0 || strcmp(pe_disable, "yes") == 0))
    {
        simd_ok = 0;
    }
    if (simd_ok)
    {
        evaluateBatchOmahaSIMD(game, pockets, npockets, board, batch, results);
        return;
    }
    evaluateBatchOmaha(game, pockets, npockets, board, batch, results);
}
#endif

/* Wrapper for Stud dispatch logic (StubBatch adapter) */
/* Note: This signature matches BatchEvaluator but uses void* for batch
   because the function pointer type expects BoardBatch.
   We will cast inside. Ideally we should update BatchEvaluator definition
   or use a union. For now, we will adapt in enumSampleBatched.
*/

/* Evaluate a batch of boards for Hold'em Hi/Lo */
void evaluateBatchHoldem8(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                          StdDeck_CardMask board, BoardBatch *batch,
                          BatchEvalResults *results)
{
    int i, p;
    StdDeck_CardMask empty_dead;
    StdDeck_CardMask_RESET(empty_dead);

    /* Process each board in the batch */
    for (i = 0; i < batch->count; i++)
    {
        StdDeck_CardMask finalBoard;
        StdDeck_CardMask_OR(finalBoard, board, batch->boards[i]);
        batched_mc_log_range_combo(finalBoard, empty_dead, pockets, npockets);

        /* Evaluate each player's hand */
        for (p = 0; p < npockets; p++)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_OR(hand, pockets[p], finalBoard);
            results->hival[p][i] = StdDeck_StdRules_EVAL_N_Cached(hand, 7);
            LowHandVal lo = StdDeck_Lowball8_EVAL(hand, 7);
            results->loval[p][i] = pe_low_qualify5(lo, LOW_QUALIFIER_8) ? lo : LowHandVal_NOTHING;
            if (batched_mc_debug_enabled() && i < 3)
            {
                BATCH_DEBUG("BMC holdem8 board #%d player %d hi=%u lo=%u qualifies=%d\n",
                            i, p, results->hival[p][i], results->loval[p][i], results->loval[p][i] != LowHandVal_NOTHING);
            }
        }
    }
}

/* Evaluate a batch of boards for Omaha */
void evaluateBatchOmaha(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                        StdDeck_CardMask board, BoardBatch *batch,
                        BatchEvalResults *results)
{
    int i, p;
    StdDeck_CardMask empty_dead;
    StdDeck_CardMask_RESET(empty_dead);

    /* Process each board in the batch */
    for (i = 0; i < batch->count; i++)
    {
        StdDeck_CardMask finalBoard;
        StdDeck_CardMask_OR(finalBoard, board, batch->boards[i]);
        batched_mc_log_range_combo(finalBoard, empty_dead, pockets, npockets);
        int nboard = StdDeck_numCards(finalBoard);

        /* Evaluate each player's hand */
        for (p = 0; p < npockets; p++)
        {
            if (nboard < 3)
            {
                /* Not enough board cards for Omaha evaluation */
                results->hival[p][i] = HandVal_NOTHING;
                results->loval[p][i] = LowHandVal_NOTHING;
            }
            else
            {
                LowHandVal low_val = LowHandVal_NOTHING;
                int err = StdDeck_OmahaHiLow8_EVAL(pockets[p], finalBoard,
                                                   &results->hival[p][i], &low_val);
                if (err != 0)
                {
                    results->hival[p][i] = HandVal_NOTHING;
                    results->loval[p][i] = LowHandVal_NOTHING;
                }
                else if (pe_low_qualify5(low_val, LOW_QUALIFIER_8))
                {
                    results->loval[p][i] = low_val;
                }
                else
                {
                    results->loval[p][i] = LowHandVal_NOTHING;
                }
                if (batched_mc_debug_enabled() && i < 3)
                {
                    BATCH_DEBUG("BMC omaha board #%d player %d hi=%u lo=%u\n",
                                i, p, results->hival[p][i], results->loval[p][i]);
                }
            }
        }
    }
}

/* Process batch results and update the main result structure */
void processBatchResults(BatchEvalResults *results, int npockets,
                         int batchSize, enum_result_t *result)
{
    int i, p;

    /* Process each board result in the batch */
    for (i = 0; i < batchSize; i++)
    {
        HandVal besthi = HandVal_NOTHING;
        LowHandVal bestlo = LowHandVal_NOTHING;
        int hishare = 0;
        int loshare = 0;
        double hipot, lopot;

        /* Find winning hands for high and low */
        for (p = 0; p < npockets; p++)
        {
            if (results->hival[p][i] != HandVal_NOTHING)
            {
                if (results->hival[p][i] > besthi)
                {
                    besthi = results->hival[p][i];
                    hishare = 1;
                }
                else if (results->hival[p][i] == besthi)
                {
                    hishare++;
                }
            }
            if (results->loval[p][i] != LowHandVal_NOTHING)
            {
                if (results->loval[p][i] < bestlo)
                {
                    bestlo = results->loval[p][i];
                    loshare = 1;
                }
                else if (results->loval[p][i] == bestlo)
                {
                    loshare++;
                }
            }
        }

        /* Award pot fractions to winning hands */
        if (bestlo != LowHandVal_NOTHING && besthi != HandVal_NOTHING)
        {
            hipot = (hishare != 0) ? 0.5 / hishare : 0;
            lopot = (loshare != 0) ? 0.5 / loshare : 0;
        }
        else if (bestlo == LowHandVal_NOTHING && besthi != HandVal_NOTHING)
        {
            hipot = (hishare != 0) ? 1.0 / hishare : 0;
            lopot = 0;
        }
        else if (bestlo != LowHandVal_NOTHING && besthi == HandVal_NOTHING)
        {
            hipot = 0;
            lopot = (loshare != 0) ? 1.0 / loshare : 0;
        }
        else
        {
            hipot = lopot = 0;
        }

        /* Update statistics for each player */
        for (p = 0; p < npockets; p++)
        {
            double potfrac = 0;
            int H = 0, L = 0;

            if (results->hival[p][i] != HandVal_NOTHING)
            {
                if (results->hival[p][i] == besthi)
                {
                    H = hishare;
                    potfrac += hipot;
                    if (hishare == 1)
                        result->nwinhi[p]++;
                    else
                        result->ntiehi[p]++;
                }
                else
                {
                    result->nlosehi[p]++;
                }
            }

            if (results->loval[p][i] != LowHandVal_NOTHING)
            {
                if (results->loval[p][i] == bestlo)
                {
                    L = loshare;
                    potfrac += lopot;
                    if (loshare == 1)
                        result->nwinlo[p]++;
                    else
                        result->ntielo[p]++;
                }
                else
                {
                    result->nloselo[p]++;
                }
            }

            result->nsharehi[p][H]++;
            result->nsharelo[p][L]++;
            result->nshare[p][H][L]++;

            if (bestlo != LowHandVal_NOTHING && besthi != HandVal_NOTHING &&
                results->hival[p][i] == besthi && results->loval[p][i] == bestlo &&
                hishare == 1 && loshare == 1)
            {
                result->nscoop[p]++;
            }

            result->ev[p] += potfrac;
        }

        result->nsamples++;
    }
}

#ifdef USE_SIMD
/* SIMD-optimized version for Hold'em Hi/Lo (high via SIMD, low via scalar) */
static void evaluateBatchHoldem8SIMD(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                     StdDeck_CardMask board, BoardBatch *batch,
                                     BatchEvalResults *results)
{
    simd_capability_t cap = simd_detect_capability();
    if (cap == SIMD_NONE)
    {
        evaluateBatchHoldem8(game, pockets, npockets, board, batch, results);
        return;
    }
    /* For each player, evaluate all boards using SIMD batching for high, scalar for low */
    for (int p = 0; p < npockets; ++p)
    {
        int total = batch->count;
        int processed = 0;
        while (processed < total)
        {
            int chunk = total - processed;
            if (chunk > SIMD_AVX512_BATCH_SIZE)
                chunk = SIMD_AVX512_BATCH_SIZE;
            StdDeck_CardMask hands[SIMD_AVX512_BATCH_SIZE];
            for (int i = 0; i < chunk; ++i)
            {
                StdDeck_CardMask finalBoard;
                StdDeck_CardMask_OR(finalBoard, board, batch->boards[processed + i]);
                StdDeck_CardMask_OR(hands[i], pockets[p], finalBoard);
            }
            HandVal hvals[SIMD_AVX512_BATCH_SIZE];
            LowHandVal lvals[SIMD_AVX512_BATCH_SIZE];
            int hi_err = simd_eval_multiple_hands(hands, chunk, hvals);
            int lo_err = simd_eval_low8_multiple_hands(hands, chunk, lvals);

            if (hi_err != 0 || lo_err != 0)
            {
                /* Fallback on any error */
                for (int i = 0; i < chunk; ++i)
                {
                    results->hival[p][processed + i] = StdDeck_StdRules_EVAL_N(hands[i], 7);
                    LowHandVal lo = StdDeck_Lowball8_EVAL(hands[i], 7);
                    results->loval[p][processed + i] = pe_low_qualify5(lo, LOW_QUALIFIER_8) ? lo : LowHandVal_NOTHING;
                }
            }
            else
            {
                for (int i = 0; i < chunk; ++i)
                {
                    results->hival[p][processed + i] = hvals[i];
                    results->loval[p][processed + i] = pe_low_qualify5(lvals[i], LOW_QUALIFIER_8) ? lvals[i] : LowHandVal_NOTHING;
                }
            }
            processed += chunk;
        }
    }
}
#endif

/* Prototype branchless-like post-processing for Hold'em, N joueurs (high only) */
static void processBatchResultsHoldemN_Branchless(const BatchEvalResults *results,
                                                  int npockets, int batchSize,
                                                  enum_result_t *result)
{
    for (int i = 0; i < batchSize; ++i)
    {
        int besthi = HandVal_NOTHING;
        for (int p = 0; p < npockets; ++p)
        {
            int v = results->hival[p][i];
            if (v > besthi)
                besthi = v;
        }
        /* Count shares */
        int hishare = 0;
        for (int p = 0; p < npockets; ++p)
        {
            hishare += (results->hival[p][i] == besthi);
        }
        double hipot = (besthi != HandVal_NOTHING && hishare != 0) ? (1.0 / hishare) : 0.0;
        /* Update EV and stats */
        for (int p = 0; p < npockets; ++p)
        {
            int win = (results->hival[p][i] == besthi);
            double potfrac = win ? hipot : 0.0;
            if (win)
            {
                if (hishare == 1)
                    result->nwinhi[p]++;
                else
                    result->ntiehi[p]++;
                result->nsharehi[p][hishare]++;
                result->nshare[p][hishare][0]++;
            }
            else
            {
                result->nlosehi[p]++;
                result->nsharehi[p][0]++;
                result->nshare[p][0][0]++;
            }
            result->ev[p] += potfrac;
        }
        result->nsamples++;
    }
}

#ifdef USE_SIMD
#if defined(__AVX2__)
#include <immintrin.h>
static void processBatchResultsHoldem2P_SIMD(const BatchEvalResults *results,
                                             int batchSize, enum_result_t *result)
{
    /* Two players, high-only (Hold'em), award: winner 1.0, tie 0.5 each */
    const int lane = 8; /* AVX2: 8x int32 */
    int i = 0;
    while (i + lane <= batchSize)
    {
        __m256i v0 = _mm256_loadu_si256((const __m256i *)&results->hival[0][i]);
        __m256i v1 = _mm256_loadu_si256((const __m256i *)&results->hival[1][i]);
        __m256i gt01 = _mm256_cmpgt_epi32(v0, v1);
        __m256i gt10 = _mm256_cmpgt_epi32(v1, v0);
        __m256i eq = _mm256_cmpeq_epi32(v0, v1);
        /* Extract masks per lane */
        int m_gt01 = _mm256_movemask_ps(_mm256_castsi256_ps(gt01));
        int m_gt10 = _mm256_movemask_ps(_mm256_castsi256_ps(gt10));
        int m_eq = _mm256_movemask_ps(_mm256_castsi256_ps(eq));
        for (int k = 0; k < lane; ++k)
        {
            int bit = 1 << k;
            int H = 0;
            if (m_eq & bit)
            {
                /* tie: both share 2 */
                H = 2;
                result->ntiehi[0]++;
                result->ntiehi[1]++;
                result->nsharehi[0][H]++;
                result->nsharehi[1][H]++;
                result->nshare[0][H][0]++;
                result->nshare[1][H][0]++;
                result->ev[0] += 0.5;
                result->ev[1] += 0.5;
            }
            else if (m_gt01 & bit)
            {
                /* p0 wins */
                H = 1;
                result->nwinhi[0]++;
                result->nlosehi[1]++;
                result->nsharehi[0][H]++;
                result->nsharehi[1][0]++;
                result->nshare[0][H][0]++;
                result->nshare[1][0][0]++;
                result->ev[0] += 1.0;
            }
            else if (m_gt10 & bit)
            {
                /* p1 wins */
                H = 1;
                result->nwinhi[1]++;
                result->nlosehi[0]++;
                result->nsharehi[1][H]++;
                result->nsharehi[0][0]++;
                result->nshare[1][H][0]++;
                result->nshare[0][0][0]++;
                result->ev[1] += 1.0;
            }
            else
            {
                /* No valid high? shouldn't happen; skip */
            }
            result->nsamples++;
        }
        i += lane;
    }
    /* Tail */
    for (; i < batchSize; ++i)
    {
        int v0s = results->hival[0][i];
        int v1s = results->hival[1][i];
        if (v0s == v1s)
        {
            result->ntiehi[0]++;
            result->ntiehi[1]++;
            result->nsharehi[0][2]++;
            result->nsharehi[1][2]++;
            result->nshare[0][2][0]++;
            result->nshare[1][2][0]++;
            result->ev[0] += 0.5;
            result->ev[1] += 0.5;
        }
        else if (v0s > v1s)
        {
            result->nwinhi[0]++;
            result->nlosehi[1]++;
            result->nsharehi[0][1]++;
            result->nsharehi[1][0]++;
            result->nshare[0][1][0]++;
            result->nshare[1][0][0]++;
            result->ev[0] += 1.0;
        }
        else
        {
            result->nwinhi[1]++;
            result->nlosehi[0]++;
            result->nsharehi[1][1]++;
            result->nsharehi[0][0]++;
            result->nshare[1][1][0]++;
            result->nshare[0][0][0]++;
            result->ev[1] += 1.0;
        }
        result->nsamples++;
    }
}
#endif /* __AVX2__ */
#endif /* USE_SIMD */

/* ----------------------------------------------------------------------------
 * Pineapple board-game evaluators
 *
 * game_pineapple and game_pineapple_lazy play the best two of each player's
 * three hole cards against the full board. game_pineapple8 splits the pot
 * high and A-5 low (with an 8 qualifier) using the same discarded pair, and
 * game_pineapple_crazy commits its discard on the flop so the surviving pair
 * plays as a plain Hold'em pocket.
 *
 * Batched would-be-Pineapple boards are evaluated through the runtime-gated
 * SIMD kernels so a default (non-USE_SIMD) build still accelerates.
 * ------------------------------------------------------------------------- */

static int bmc_engine_simd_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *d = getenv("PE_DISABLE_SIMD");
        int disabled = d && (!strcmp(d, "1") || !strcmp(d, "true") ||
                             !strcmp(d, "TRUE") || !strcmp(d, "yes"));
        cached = (!disabled && simd_detect_capability() != SIMD_NONE) ? 1 : 0;
    }
    return cached;
}

/* Build the 7-card hand produced by keeping all of `pocket` except the card
 * at triple-position `discard` and adding `board`. */
static void bmc_pineapple_hand(StdDeck_CardMask *hand, const StdDeck_CardMask *pocket,
                               const StdDeck_CardMask *board, int discard)
{
    int hole[3];
    int nhole = 0;

    StdDeck_CardMask_RESET(*hand);
    for (int card = 0; card < StdDeck_N_CARDS && nhole < 3; ++card)
        if (StdDeck_CardMask_CARD_IS_SET(*pocket, card))
            hole[nhole++] = card;
    for (int id = 0; id < nhole; ++id)
        if (id != discard)
            StdDeck_CardMask_SET(*hand, hole[id]);
    StdDeck_CardMask_OR(*hand, *hand, *board);
}

/* Evaluate the three candidates of every player against one completed board.
 * `with_low` also fills results->loval with qualified A-5 lows. */
static void bmc_eval_pineapple_sample(StdDeck_CardMask *cands, int npockets, int sample,
                                      int with_low, BatchEvalResults *results)
{
    HandVal hi[ENUM_MAXPLAYERS * 3];
    LowHandVal lo[ENUM_MAXPLAYERS * 3];
    int count = npockets * 3;
    int sim_ok = bmc_engine_simd_enabled();

    if (sim_ok)
    {
        simd_eval_multiple_hands(cands, count, hi);
        if (with_low)
            simd_eval_low8_multiple_hands(cands, count, lo);
    }
    else
    {
        for (int p = 0; p < npockets; ++p)
            for (int d = 0; d < 3; ++d)
            {
                int idx = p * 3 + d;
                hi[idx] = StdDeck_StdRules_EVAL_N_Cached(cands[idx], 7);
                if (with_low)
                    lo[idx] = pe_eval_low_a5(cands[idx]);
            }
    }

    for (int p = 0; p < npockets; ++p)
    {
        HandVal besthi = HandVal_NOTHING;
        LowHandVal bestlo = LowHandVal_NOTHING;
        int bestd = 0;
        for (int d = 0; d < 3; ++d)
        {
            int idx = p * 3 + d;
            if (hi[idx] > besthi)
            {
                besthi = hi[idx];
                bestd = d;
            }
            if (with_low)
            {
                /* Mirror the classic pineapple8 rule: each candidate is
                 * qualified separately and the best remaining low pairs up
                 * with the best high, as in Omaha Hi/Lo. */
                LowHandVal qualified = pe_low_qualify5(lo[idx], LOW_QUALIFIER_8)
                                           ? lo[idx] : LowHandVal_NOTHING;
                if (qualified != LowHandVal_NOTHING && qualified < bestlo)
                    bestlo = qualified;
            }
        }
        results->hival[p][sample] = besthi;
        results->loval[p][sample] = with_low ? bestlo : LowHandVal_NOTHING;
        if (batched_mc_debug_enabled() && sample < 3)
            BATCH_DEBUG("BMC pineapple board #%d player %d hi=%u lo=%u discard=%d\n",
                        sample, p, besthi, results->loval[p][sample], (int)bestd);
    }
}

static void evaluateBatchPineappleImpl(enum_game_t game, StdDeck_CardMask pockets[],
                                       int npockets, StdDeck_CardMask board,
                                       BoardBatch *batch, BatchEvalResults *results)
{
    int with_low = (game == game_pineapple8);
    StdDeck_CardMask empty_dead;
    StdDeck_CardMask_RESET(empty_dead);

    for (int i = 0; i < batch->count; ++i)
    {
        StdDeck_CardMask finalBoard;
        StdDeck_CardMask cands[ENUM_MAXPLAYERS * 3];

        StdDeck_CardMask_OR(finalBoard, board, batch->boards[i]);
        batched_mc_log_range_combo(finalBoard, empty_dead, pockets, npockets);

        for (int p = 0; p < npockets; ++p)
            for (int d = 0; d < 3; ++d)
                bmc_pineapple_hand(&cands[p * 3 + d], &pockets[p], &finalBoard, d);

        bmc_eval_pineapple_sample(cands, npockets, i, with_low, results);
    }
}

/* Crazy Pineapple: the two surviving hole cards are committed by
 * enumSampleBatched once per call (with access to the flop and dead), then
 * played against the completing board like a plain Hold'em pocket. */
static StdDeck_CardMask g_committed_pineapple[ENUM_MAXPLAYERS];

static void evaluateBatchPineappleCrazy(enum_game_t game, StdDeck_CardMask pockets[],
                                        int npockets, StdDeck_CardMask board,
                                        BoardBatch *batch, BatchEvalResults *results)
{
    StdDeck_CardMask empty_dead;
    StdDeck_CardMask_RESET(empty_dead);

    for (int i = 0; i < batch->count; ++i)
    {
        StdDeck_CardMask finalBoard;

        StdDeck_CardMask_OR(finalBoard, board, batch->boards[i]);
        batched_mc_log_range_combo(finalBoard, empty_dead, g_committed_pineapple, npockets);

        for (int p = 0; p < npockets; ++p)
        {
            StdDeck_CardMask hand;
            StdDeck_CardMask_RESET(hand);
            StdDeck_CardMask_OR(hand, g_committed_pineapple[p], finalBoard);
            results->hival[p][i] = StdDeck_StdRules_EVAL_N_Cached(hand, 7);
            results->loval[p][i] = LowHandVal_NOTHING;
        }
    }
}

/* Internal type for batch evaluator function */
typedef void (*BatchEvaluator)(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                               StdDeck_CardMask board, BoardBatch *batch,
                               BatchEvalResults *results);

static BatchEvaluator evaluator_registry[game_NUMGAMES] = {0};

/* Wrapper for Hold'em dispatch logic */
static void evaluateBatchHoldemDispatch(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                        StdDeck_CardMask board, BoardBatch *batch,
                                        BatchEvalResults *results)
{
#ifdef USE_GPU
    if (batched_mc_gpu_ensure_context(game))
    {
        evaluateBatchHoldemGPU(game, pockets, npockets, board, batch, results);
        return;
    }
#endif
#ifdef USE_SIMD
    /* Runtime gate: disable SIMD if PE_DISABLE_SIMD is set */
    int simd_ok = (simd_detect_capability() != SIMD_NONE);
    const char *pe_disable = getenv("PE_DISABLE_SIMD");
    if (pe_disable && (strcmp(pe_disable, "1") == 0 || strcmp(pe_disable, "true") == 0 || strcmp(pe_disable, "TRUE") == 0 || strcmp(pe_disable, "yes") == 0))
    {
        simd_ok = 0;
    }
    if (simd_ok)
    {
        evaluateBatchHoldemSIMD(game, pockets, npockets, board, batch, results);
        return;
    }
#endif
    evaluateBatchHoldem(game, pockets, npockets, board, batch, results);
}

/* Wrapper for Hold'em 8 dispatch logic */
static void evaluateBatchHoldem8Dispatch(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                         StdDeck_CardMask board, BoardBatch *batch,
                                         BatchEvalResults *results)
{
#ifdef USE_SIMD
    int simd_ok = (simd_detect_capability() != SIMD_NONE);
    const char *pe_disable = getenv("PE_DISABLE_SIMD");
    if (pe_disable && (strcmp(pe_disable, "1") == 0 || strcmp(pe_disable, "true") == 0 || strcmp(pe_disable, "TRUE") == 0 || strcmp(pe_disable, "yes") == 0))
    {
        simd_ok = 0;
    }
    if (simd_ok)
    {
        evaluateBatchHoldem8SIMD(game, pockets, npockets, board, batch, results);
        return;
    }
#endif
    evaluateBatchHoldem8(game, pockets, npockets, board, batch, results);
}

static void init_registry_impl(void)
{
    memset(evaluator_registry, 0, sizeof(evaluator_registry));

    evaluator_registry[game_holdem] = evaluateBatchHoldemDispatch;
    evaluator_registry[game_holdem8] = evaluateBatchHoldem8Dispatch;

#ifdef USE_SIMD
    evaluator_registry[game_omaha] = evaluateBatchOmahaDispatch;
    evaluator_registry[game_omaha5] = evaluateBatchOmahaDispatch;
    evaluator_registry[game_omaha6] = evaluateBatchOmahaDispatch;
    evaluator_registry[game_omaha8] = evaluateBatchOmahaDispatch;
    evaluator_registry[game_omaha85] = evaluateBatchOmahaDispatch;
    evaluator_registry[game_omaha86] = evaluateBatchOmahaDispatch;
#else
    evaluator_registry[game_omaha] = evaluateBatchOmaha;
    evaluator_registry[game_omaha5] = evaluateBatchOmaha;
    evaluator_registry[game_omaha6] = evaluateBatchOmaha;
    evaluator_registry[game_omaha8] = evaluateBatchOmaha;
    evaluator_registry[game_omaha85] = evaluateBatchOmaha;
    evaluator_registry[game_omaha86] = evaluateBatchOmaha;
#endif

    /* Pineapple family: best two of three hole cards against the board. The
     * impl dispatches on game for the hi/lo split; the crazy variant needs
     * the flop-committed pockets stashed by enumSampleBatched. */
    evaluator_registry[game_pineapple] = evaluateBatchPineappleImpl;
    evaluator_registry[game_pineapple_lazy] = evaluateBatchPineappleImpl;
    evaluator_registry[game_pineapple8] = evaluateBatchPineappleImpl;
    evaluator_registry[game_pineapple_crazy] = evaluateBatchPineappleCrazy;
}

/* Main batched enumSample function */
int enumSampleBatched(enum_game_t game, StdDeck_CardMask pockets[],
                      StdDeck_CardMask board, StdDeck_CardMask dead,
                      int npockets, int nboard, int niter, int orderflag,
                      enum_result_t *result)
{
    BoardBatch batch;
    BatchEvalResults batchResults;
    int numCards;
    int remaining;
    int currentBatchSize;

    /* Ensure registry is initialized exactly once in a thread-safe way if possible,
       or at least safely for this translation unit.
       Using a static flag here is standard for single-threaded initialization or idempotent writes.
       For strict thread safety, we use pthread_once if available or atomic.
       Since we don't want to force pthread dependency if not already present, we use a simple static flag.
       The writes are idempotent (same function pointers), so race conditions are benign. */
    static int registry_initialized = 0;
    if (!registry_initialized) {
        init_registry_impl();
        registry_initialized = 1;
    }

    /* Clear the result structure */
    enumResultClear(result);

    if (npockets > ENUM_MAXPLAYERS)
        return 1;

    /* Ordering is not yet implemented for batched version */
    if (orderflag)
    {
        /* Fall back to regular enumSample for now */
        return enumSample(game, pockets, board, dead, npockets, nboard,
                          niter, orderflag, result);
    }

    /* Special handling for Stud games which use StubBatch */
    if (game == game_7stud || game == game_7stud8 || game == game_7studnsq ||
        game == game_razz || game == game_lowball || game == game_lowball27)
    {
        /* Stud logic using StubBatch */
        StubBatch stubBatch;
        int numToDeal[ENUM_MAXPLAYERS];
        enum_gameparams_t *params = enumGameParams(game);
        if (!params) return 1;

        for (int p = 0; p < npockets; ++p) {
            numToDeal[p] = params->maxpocket - StdDeck_numCards(pockets[p]);
            if (numToDeal[p] < 0) numToDeal[p] = 0;
        }

        remaining = niter;
        while (remaining > 0)
        {
            currentBatchSize = (remaining > BATCH_SIZE) ? BATCH_SIZE : remaining;

            generateStubBatch(&stubBatch, dead, numToDeal, npockets, currentBatchSize);

            if (game == game_7stud) {
                evaluateBatchStud(game, pockets, npockets, &stubBatch, &batchResults);
            } else if (game == game_7stud8) {
                evaluateBatchStud8(game, pockets, npockets, &stubBatch, &batchResults);
            } else if (game == game_razz) {
                evaluateBatchRazz(game, pockets, npockets, &stubBatch, &batchResults);
            } else if (game == game_7studnsq) {
                evaluateBatchStudNSQ(game, pockets, npockets, &stubBatch, &batchResults);
            } else if (game == game_lowball27) {
                evaluateBatchLowball27(game, pockets, npockets, &stubBatch, &batchResults);
            } else if (game == game_27_triple_draw) {
                evaluateBatchTripleDraw27(game, pockets, npockets, &stubBatch, &batchResults);
            } else if (game == game_a5_triple_draw) {
                evaluateBatchTripleDrawA5(game, pockets, npockets, &stubBatch, &batchResults);
            } else {
                /* Fallback for other stud variants until vectorized */
                 return enumSample(game, pockets, board, dead, npockets, nboard,
                          niter, orderflag, result);
            }

            processBatchResults(&batchResults, npockets, currentBatchSize, result);
            remaining -= currentBatchSize;
        }
    }
    else
    {
        /* Board games logic */

        /* Check if we have a batched evaluator for this game */
        BatchEvaluator evaluator = (game < game_NUMGAMES) ? evaluator_registry[game] : NULL;
        if (!evaluator) {
            /* Fall back to regular enumSample for unsupported games */
            return enumSample(game, pockets, board, dead, npockets, nboard,
                            niter, orderflag, result);
        }

        /* Crazy Pineapple commits its discard once the flop is known; without
         * one the discard is unknowable, so mirror the classic sample path and
         * reject. The commit itself is computed once here, not per sample. */
        if (game == game_pineapple_crazy)
        {
            if (nboard < 3)
                return 1;
            if (pe_crazy_pineapple_commit(pockets, npockets, board, dead,
                                          g_committed_pineapple))
                return 1;
        }

        /* Calculate number of cards to deal */
        numCards = 5 - nboard;
        if (numCards <= 0)
            return 1;

        /* Sampled boards must never intersect a pocket or the fixed board.
         * The Monte-Carlo enumeration in enumerate.c computes the same
         * effective dead set; doing it here keeps direct callers that only
         * pass their game dead cards correct (RangeEquity already bakes the
         * pockets in, so this is idempotent for it). */
        StdDeck_CardMask effective_dead;
        StdDeck_CardMask_RESET(effective_dead);
        StdDeck_CardMask_OR(effective_dead, effective_dead, dead);
        StdDeck_CardMask_OR(effective_dead, effective_dead, board);
        for (int p = 0; p < npockets; ++p)
            StdDeck_CardMask_OR(effective_dead, effective_dead, pockets[p]);

        /* Process iterations in batches */
        remaining = niter;
        while (remaining > 0)
        {
            currentBatchSize = (remaining > BATCH_SIZE) ? BATCH_SIZE : remaining;

            /* Generate a batch of random boards */
            generateBoardBatch(&batch, effective_dead, numCards, currentBatchSize);

            /* Evaluate the batch using registered evaluator */
            evaluator(game, pockets, npockets, board, &batch, &batchResults);

            /* Process the batch results */
            {
    #if defined(USE_SIMD) && defined(__AVX2__)
                if (game == game_holdem)
                {
                    if (npockets == 2)
                    {
                        processBatchResultsHoldem2P_SIMD(&batchResults, currentBatchSize, result);
                    }
                    else
                    {
                        processBatchResultsHoldemN_SIMDBoards(&batchResults, npockets, currentBatchSize, result);
                    }
                }
                else
                {
                    processBatchResults(&batchResults, npockets, currentBatchSize, result);
                }
    #else
                if (game == game_holdem)
                {
                    processBatchResultsHoldemN_Branchless(&batchResults, npockets, currentBatchSize, result);
                }
                else
                {
                    processBatchResults(&batchResults, npockets, currentBatchSize, result);
                }
    #endif
            }

            remaining -= currentBatchSize;
        }
    }

    result->game = game;
    result->nplayers = npockets;
    result->sampleType = ENUM_SAMPLE;

    return 0;
}

/* AVX2 block over boards for N players (Hold'em, high only) */
#ifdef USE_SIMD
#if defined(__AVX2__)
static void processBatchResultsHoldemN_SIMDBoards(const BatchEvalResults *results,
                                                  int npockets, int batchSize,
                                                  enum_result_t *result)
{
    const int lane = 8;
    const __m256i ones = _mm256_set1_epi32(1);
    const __m256 onesf = _mm256_set1_ps(1.0f);
    int i = 0;
    while (i < batchSize)
    {
        int blk = batchSize - i;
        if (blk > lane)
            blk = lane;
        /* best per lane */
        __m256i best = _mm256_set1_epi32(HandVal_NOTHING);
        for (int p = 0; p < npockets; ++p)
        {
            __m256i v = _mm256_loadu_si256((const __m256i *)&results->hival[p][i]);
            best = _mm256_max_epi32(best, v);
        }
        /* share per lane */
        __m256i share = _mm256_setzero_si256();
        for (int p = 0; p < npockets; ++p)
        {
            __m256i v = _mm256_loadu_si256((const __m256i *)&results->hival[p][i]);
            __m256i eq = _mm256_cmpeq_epi32(v, best);
            __m256i add = _mm256_and_si256(eq, ones);
            share = _mm256_add_epi32(share, add);
        }
        int bestArr[lane];
        int shareArr[lane];
        _mm256_storeu_si256((__m256i *)bestArr, best);
        _mm256_storeu_si256((__m256i *)shareArr, share);
        /* Build potfrac float vector per lane */
        float pfArr[lane];
        for (int k = 0; k < blk; ++k)
        {
            int hs = shareArr[k];
            pfArr[k] = (bestArr[k] != HandVal_NOTHING && hs > 0) ? (1.0f / (float)hs) : 0.0f;
        }
        for (int k = blk; k < lane; ++k)
            pfArr[k] = 0.0f;
        __m256 pfv = _mm256_loadu_ps(pfArr);
        /* Award per player using vector eq masks (accumulate EV in float, reduce) */
        for (int p = 0; p < npockets; ++p)
        {
            __m256i v = _mm256_loadu_si256((const __m256i *)&results->hival[p][i]);
            __m256i eq = _mm256_cmpeq_epi32(v, best);
            int m_eq = _mm256_movemask_ps(_mm256_castsi256_ps(eq));
            /* Update counts (scalar per lane) */
            for (int k = 0; k < blk; ++k)
            {
                int win = (m_eq >> k) & 1;
                if (win)
                {
                    int hs = shareArr[k];
                    if (hs == 1)
                        result->nwinhi[p]++;
                    else
                        result->ntiehi[p]++;
                    result->nsharehi[p][hs]++;
                    result->nshare[p][hs][0]++;
                }
                else
                {
                    result->nlosehi[p]++;
                    result->nsharehi[p][0]++;
                    result->nshare[p][0][0]++;
                }
            }
            /* Vector EV accumulation */
            __m256 maskf = _mm256_and_ps(_mm256_castsi256_ps(eq), onesf);
            __m256 contrib = _mm256_mul_ps(pfv, maskf);
            /* Horizontal sum of 8 floats */
            __m128 low = _mm256_castps256_ps128(contrib);
            __m128 high = _mm256_extractf128_ps(contrib, 1);
            __m128 sum4 = _mm_add_ps(low, high);
            __m128 sum2 = _mm_hadd_ps(sum4, sum4);
            __m128 sum1 = _mm_hadd_ps(sum2, sum2);
            float add;
            _mm_store_ss(&add, sum1);
            result->ev[p] += (double)add;
        }
        result->nsamples += blk;
        i += blk;
    }
}
#endif /* __AVX2__ */
#endif /* USE_SIMD */
#ifdef USE_SIMD
/* SIMD-optimized version for Hold'em evaluation */
void evaluateBatchHoldemSIMD(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                             StdDeck_CardMask board, BoardBatch *batch,
                             BatchEvalResults *results)
{
    /* Prepare hands per player across boards and evaluate via simd_operations */
    simd_capability_t cap = simd_detect_capability();
    if (cap == SIMD_NONE)
    {
        evaluateBatchHoldem(game, pockets, npockets, board, batch, results);
        return;
    }

    /* For each player, evaluate all boards using SIMD batching */
    for (int p = 0; p < npockets; ++p)
    {
        /* Assemble hands for this player across the batch */
        int total = batch->count;
        int processed = 0;
        while (processed < total)
        {
            int chunk = total - processed;
            if (chunk > SIMD_AVX512_BATCH_SIZE)
                chunk = SIMD_AVX512_BATCH_SIZE;
            StdDeck_CardMask hands[SIMD_AVX512_BATCH_SIZE];
            for (int i = 0; i < chunk; ++i)
            {
                StdDeck_CardMask finalBoard;
                StdDeck_CardMask_OR(finalBoard, board, batch->boards[processed + i]);
                StdDeck_CardMask_OR(hands[i], pockets[p], finalBoard);
            }
            HandVal hvals[SIMD_AVX512_BATCH_SIZE];
            if (simd_eval_multiple_hands(hands, chunk, hvals) != 0)
            {
                /* Fallback on any error */
                for (int i = 0; i < chunk; ++i)
                {
                    results->hival[p][processed + i] = StdDeck_StdRules_EVAL_N(hands[i], 7);
                    results->loval[p][processed + i] = LowHandVal_NOTHING;
                }
            }
            else
            {
                for (int i = 0; i < chunk; ++i)
                {
                    results->hival[p][processed + i] = hvals[i];
                    results->loval[p][processed + i] = LowHandVal_NOTHING;
                }
            }
            processed += chunk;
        }
    }
}
#endif

#ifdef USE_GPU
/* Helper for GPU evaluation (Hold'em/Omaha) */
static void evaluateBatchGPU_Impl(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                  StdDeck_CardMask board, BoardBatch *batch,
                                  BatchEvalResults *results,
                                  void (*fallback_func)(enum_game_t, StdDeck_CardMask[], int, StdDeck_CardMask, BoardBatch*, BatchEvalResults*))
{
    if (!batched_mc_gpu_ensure_context(game))
    {
        fallback_func(game, pockets, npockets, board, batch, results);
        return;
    }

    const int boards = batch->count;
    if (boards <= 0) return;

    size_t total_hands = (size_t)npockets * (size_t)boards;
    StdDeck_CardMask* gpu_boards = (StdDeck_CardMask*)malloc(boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* gpu_holes = (StdDeck_CardMask*)malloc(total_hands * sizeof(StdDeck_CardMask));

    if (!gpu_boards || !gpu_holes) {
        free(gpu_boards);
        free(gpu_holes);
        fallback_func(game, pockets, npockets, board, batch, results);
        return;
    }

    for (int i = 0; i < boards; ++i) {
        StdDeck_CardMask finalBoard;
        StdDeck_CardMask_OR(finalBoard, board, batch->boards[i]);
        gpu_boards[i] = finalBoard;
        for (int p = 0; p < npockets; ++p) {
            gpu_holes[i * npockets + p] = pockets[p];
        }
    }

    gpu_eval_result_hilo_t gpu_result = {0};
    int status = gpu_eval_batch_boards_hilo(
        g_gpu_eval_ctx,
        gpu_boards,
        gpu_holes,
        boards,
        npockets,
        &gpu_result
    );

    if (status != 0 || !gpu_result.hand_values_hi) {
        free(gpu_boards);
        free(gpu_holes);
        free(gpu_result.hand_values_hi);
        free(gpu_result.hand_values_lo);
        free(gpu_result.lo_qualifies);
        fallback_func(game, pockets, npockets, board, batch, results);
        return;
    }

    gpu_game_config_t config = g_gpu_eval_ctx->game_config;
    bool has_lo_result = config.eval_low && gpu_result.hand_values_lo && gpu_result.lo_qualifies;

    for (int i = 0; i < boards; ++i) {
        for (int p = 0; p < npockets; ++p) {
            size_t idx = (size_t)i * (size_t)npockets + (size_t)p;
            results->hival[p][i] = gpu_result.hand_values_hi[idx];
            LowHandVal lo_val = LowHandVal_NOTHING;
            if (has_lo_result && gpu_result.lo_qualifies[idx]) {
                lo_val = gpu_result.hand_values_lo[idx];
            }
            results->loval[p][i] = lo_val;
        }
    }

    free(gpu_boards);
    free(gpu_holes);
    free(gpu_result.hand_values_hi);
    free(gpu_result.hand_values_lo);
    free(gpu_result.lo_qualifies);
}

/* GPU-optimized version for Omaha evaluation */
void evaluateBatchOmahaGPU(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                           StdDeck_CardMask board, BoardBatch *batch,
                           BatchEvalResults *results)
{
    evaluateBatchGPU_Impl(game, pockets, npockets, board, batch, results, evaluateBatchOmaha);
}

/* GPU-optimized version for Hold'em evaluation */
void evaluateBatchHoldemGPU(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                            StdDeck_CardMask board, BoardBatch *batch,
                            BatchEvalResults *results)
{
    evaluateBatchGPU_Impl(game, pockets, npockets, board, batch, results, evaluateBatchHoldem);
}
#endif

/* High-level batched Monte-Carlo equity calculation */
BatchedMonteCarloResult BatchedMonteCarlo_CalculateEquity(
    StdDeck_CardMask hand1, StdDeck_CardMask hand2,
    StdDeck_CardMask board, StdDeck_CardMask dead,
    int num_samples, int batch_size)
{

    BatchedMonteCarloResult result = {0};
    StdDeck_CardMask pockets[2];
    enum_result_t enum_result;

    pockets[0] = hand1;
    pockets[1] = hand2;

    /* Use optimal batch size if not specified */
    if (batch_size <= 0)
    {
        batch_size = BatchedMonteCarlo_GetOptimalBatchSize();
    }

    if (pe_sampling_get_policy() == PE_SAMPLING_IMPORTANCE_BOARD_AWARE ||
        pe_sampling_get_policy() == PE_SAMPLING_STRATIFIED_BOARD_AWARE)
    {
        /* Weighted MC importance sampling (Hold'em) */
        int nboard = StdDeck_numCards(board);
        int numCards = 5 - nboard;
        if (numCards <= 0)
            return result;
        int remaining = num_samples;
        double wsum = 0.0;
        double ev_accum[2] = {0.0, 0.0};
        while (remaining > 0)
        {
            int currentBatchSize = (remaining > BATCH_SIZE) ? BATCH_SIZE : remaining;
            BoardBatch batch;
            BatchEvalResults batchResults;
            /* dead includes only dead; board known is added when evaluating */
            pe_sampling_generate_boards(&batch, dead, numCards, currentBatchSize);
            evaluateBatchHoldem(game_holdem, pockets, 2, board, &batch, &batchResults);
            for (int i = 0; i < currentBatchSize; i++)
            {
                /* Recompute pot fractions */
                HandVal besthi = HandVal_NOTHING;
                int hishare = 0;
                /* No low in this path */
                for (int p = 0; p < 2; p++)
                {
                    if (batchResults.hival[p][i] != HandVal_NOTHING)
                    {
                        if (batchResults.hival[p][i] > besthi)
                        {
                            besthi = batchResults.hival[p][i];
                            hishare = 1;
                        }
                        else if (batchResults.hival[p][i] == besthi)
                        {
                            hishare++;
                        }
                    }
                }
                double hipot = (besthi != HandVal_NOTHING && hishare != 0) ? 1.0 / hishare : 0.0;
                double w = pe_sampling_importance_weight(batch.boards[i], numCards);
                wsum += w;
                for (int p = 0; p < 2; p++)
                {
                    double potfrac = 0.0;
                    if (batchResults.hival[p][i] != HandVal_NOTHING && batchResults.hival[p][i] == besthi)
                    {
                        potfrac += hipot;
                    }
                    ev_accum[p] += w * potfrac;
                }
            }
            remaining -= currentBatchSize;
        }
        result.samples_evaluated = num_samples;
        if (wsum > 0.0)
        {
            result.equity[0] = ev_accum[0] / wsum;
            result.equity[1] = ev_accum[1] / wsum;
            result.tie_percentage = 1.0 - result.equity[0] - result.equity[1];
        }
    }
    else
    {
        /* Use batched enumeration (uniform or QMC) */
        int err = enumSampleBatched(game_holdem, pockets, board, dead, 2,
                                    StdDeck_numCards(board), num_samples, 0, &enum_result);
        if (err == 0)
        {
            result.samples_evaluated = enum_result.nsamples;
            if (enum_result.nsamples > 0)
            {
                result.equity[0] = enum_result.ev[0] / (double)enum_result.nsamples;
                result.equity[1] = enum_result.ev[1] / (double)enum_result.nsamples;
                result.tie_percentage = 1.0 - result.equity[0] - result.equity[1];
            }
        }
    }

    return result;
}

/* Get optimal batch size based on system capabilities */
int BatchedMonteCarlo_GetOptimalBatchSize(void)
{
    /* For now, return a reasonable default */
    /* In a real implementation, this would detect CPU features */
    return 16;
}

/* Set random seed for reproducible results */
void BatchedMonteCarlo_SetRandomSeed(unsigned int seed)
{
    srand(seed);
}
