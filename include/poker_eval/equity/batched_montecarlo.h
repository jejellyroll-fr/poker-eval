/*
 * batched_montecarlo.h - Batched Monte-Carlo implementation for poker evaluation
 *
 * This implementation batches multiple board samples together before evaluation
 * to improve cache efficiency and enable SIMD/GPU optimizations.
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

#ifndef BATCHED_MONTECARLO_H
#define BATCHED_MONTECARLO_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/pokereval_export.h>

/* Batch size for Monte-Carlo sampling
 * Chosen to fit in L1 cache and enable efficient SIMD processing
 * Can be adjusted based on architecture */
#define BATCH_SIZE 256

/* Structure to hold a batch of board samples */
typedef struct {
    StdDeck_CardMask boards[BATCH_SIZE];
    int count;  /* Number of valid boards in this batch */
} BoardBatch;

/* Structure to hold a batch of stub samples (for Stud/Draw games) */
/* Each sample contains the cards dealt to each player to complete their hand */
typedef struct {
    StdDeck_CardMask hands[BATCH_SIZE][ENUM_MAXPLAYERS];
    int count;
} StubBatch;

/* Structure to hold evaluation results for a batch */
typedef struct {
    HandVal hival[ENUM_MAXPLAYERS][BATCH_SIZE];
    LowHandVal loval[ENUM_MAXPLAYERS][BATCH_SIZE];
} BatchEvalResults;

/* Structure for batched Monte-Carlo results */
typedef struct {
    double equity[2];
    double tie_percentage;
    int samples_evaluated;
} BatchedMonteCarloResult;

/* Structure for batched evaluation */
typedef struct {
    int batch_size;
    int total_samples;
    double processing_time;
} BatchedEvaluation;

/* Batched version of enumSample for improved performance */
POKEREVAL_EXPORT int enumSampleBatched(enum_game_t game, StdDeck_CardMask pockets[],
                                       StdDeck_CardMask board, StdDeck_CardMask dead,
                                       int npockets, int nboard, int niter, int orderflag,
                                       enum_result_t *result);

/* Helper functions for batched processing */
POKEREVAL_EXPORT void generateBoardBatch(BoardBatch *batch, StdDeck_CardMask dead, 
                                        int numCards, int batchSize);

POKEREVAL_EXPORT void generateStubBatch(StubBatch *batch, StdDeck_CardMask dead,
                                       int numToDeal[], int npockets, int batchSize);

POKEREVAL_EXPORT void evaluateBatchHoldem(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                         StdDeck_CardMask board, BoardBatch *batch,
                                         BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchHoldem8(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                          StdDeck_CardMask board, BoardBatch *batch,
                                          BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchOmaha(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                        StdDeck_CardMask board, BoardBatch *batch,
                                        BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchStud(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                       StubBatch *batch, BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchStud8(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                        StubBatch *batch, BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchRazz(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                       StubBatch *batch, BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchStudNSQ(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                          StubBatch *batch, BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchLowball27(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                            StubBatch *batch, BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchTripleDraw27(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                               StubBatch *batch, BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchTripleDrawA5(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                               StubBatch *batch, BatchEvalResults *results);

POKEREVAL_EXPORT void processBatchResults(BatchEvalResults *results, int npockets,
                                         int batchSize, enum_result_t *result);

/* High-level batched Monte-Carlo functions */
POKEREVAL_EXPORT BatchedMonteCarloResult BatchedMonteCarlo_CalculateEquity(
    StdDeck_CardMask hand1, StdDeck_CardMask hand2, 
    StdDeck_CardMask board, StdDeck_CardMask dead,
    int num_samples, int batch_size);

POKEREVAL_EXPORT int BatchedMonteCarlo_GetOptimalBatchSize(void);

POKEREVAL_EXPORT void BatchedMonteCarlo_SetRandomSeed(unsigned int seed);

/* SIMD-optimized batch evaluation functions (if available) */
#ifdef USE_SIMD
POKEREVAL_EXPORT void evaluateBatchHoldemSIMD(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                              StdDeck_CardMask board, BoardBatch *batch,
                                              BatchEvalResults *results);
#endif

/* GPU-optimized batch evaluation functions (if available) */
#ifdef USE_GPU
POKEREVAL_EXPORT void evaluateBatchHoldemGPU(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                             StdDeck_CardMask board, BoardBatch *batch,
                                             BatchEvalResults *results);

POKEREVAL_EXPORT void evaluateBatchOmahaGPU(enum_game_t game, StdDeck_CardMask pockets[], int npockets,
                                            StdDeck_CardMask board, BoardBatch *batch,
                                            BatchEvalResults *results);
#endif

#endif /* BATCHED_MONTECARLO_H */
