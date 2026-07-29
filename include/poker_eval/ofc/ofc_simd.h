/*
 * ofc_simd.h - Open Face Chinese Poker SIMD optimizations
 *
 * Copyright (C) 2025
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 */

#ifndef __OFC_SIMD_H__
#define __OFC_SIMD_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/ofc/ofc.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* SIMD capability detection */
    typedef enum
    {
        OFC_SIMD_NONE = 0,
        OFC_SIMD_SSE2 = 1,
        OFC_SIMD_AVX2 = 2,
        OFC_SIMD_AVX512 = 4
    } ofc_simd_capability_t;

    /* SIMD processing mode selection - defined in ofc.h to avoid redefinition */
    #ifndef OFC_PROCESSING_MODE_DEFINED
    #define OFC_PROCESSING_MODE_DEFINED
    typedef enum
    {
        OFC_PROCESS_CPU = 0,    /* Standard CPU processing */
        OFC_PROCESS_SIMD_AUTO,  /* Automatic SIMD selection */
        OFC_PROCESS_SIMD_SSE2,  /* Force SSE2 */
        OFC_PROCESS_SIMD_AVX2,  /* Force AVX2 */
        OFC_PROCESS_SIMD_AVX512 /* Force AVX-512 */
    } ofc_processing_mode_t;
    #endif

/* SIMD batch sizes for different instruction sets */
#define OFC_SIMD_BATCH_SIZE_SSE2 4    /* 128-bit vectors */
#define OFC_SIMD_BATCH_SIZE_AVX2 8    /* 256-bit vectors */
#define OFC_SIMD_BATCH_SIZE_AVX512 16 /* 512-bit vectors */

/* Maximum batch size supported */
#define OFC_SIMD_MAX_BATCH_SIZE OFC_SIMD_BATCH_SIZE_AVX512

    /* SIMD hand evaluation results structure */
    typedef struct
    {
        HandVal values[OFC_SIMD_MAX_BATCH_SIZE * 3];  /* Hand values (Top/Middle/Bottom per hand) */
        int hierarchy_valid[OFC_SIMD_MAX_BATCH_SIZE]; /* Hierarchy validation results */
        int batch_size;                               /* Actual batch size used */
    } ofc_simd_eval_result_t;

    /* SIMD Monte Carlo batch structure */
    typedef struct
    {
        ofc_hand_t partial_hands[OFC_SIMD_MAX_BATCH_SIZE]; /* Input partial hands */
        int cards[OFC_SIMD_MAX_BATCH_SIZE];                /* Cards to place */
        ofc_position_t positions[OFC_SIMD_MAX_BATCH_SIZE]; /* Positions to test */
        float foul_risks[OFC_SIMD_MAX_BATCH_SIZE];         /* Output foul risks */
        int batch_size;                                    /* Number of hands in batch */
    } ofc_simd_monte_carlo_batch_t;

    /* ===== SIMD Capability Detection ===== */

    /**
     * Detect available SIMD instruction sets
     * Returns: Bitmask of available ofc_simd_capability_t flags
     */
    int OFC_DetectSIMDCapabilities(void);

    /**
     * Get optimal batch size for current SIMD capabilities
     * Returns: Batch size (4 for SSE2, 8 for AVX2, 16 for AVX-512)
     */
    int OFC_GetOptimalBatchSize(void);

    /**
     * Select best processing mode based on batch size and capabilities
     */
    ofc_processing_mode_t OFC_SelectProcessingMode(int batch_size);

    /* ===== Core SIMD Hand Evaluation Functions ===== */

    /**
     * Evaluate multiple OFC hands simultaneously using SIMD
     *
     * @param top_hands Array of top hands (3 cards each)
     * @param middle_hands Array of middle hands (5 cards each)
     * @param bottom_hands Array of bottom hands (5 cards each)
     * @param batch_size Number of hands to evaluate (max OFC_SIMD_MAX_BATCH_SIZE)
     * @param results Output structure with evaluation results
     * @param mode SIMD processing mode
     * @return 0 on success, -1 on error
     */
    int OFC_EvaluateHandsSIMD(
        const StdDeck_CardMask *top_hands,
        const StdDeck_CardMask *middle_hands,
        const StdDeck_CardMask *bottom_hands,
        int batch_size,
        ofc_simd_eval_result_t *results,
        ofc_processing_mode_t mode);

    /**
     * Validate hierarchy for multiple hands using SIMD
     * (declared in ofc.h)
     */

    /* ===== SIMD Monte Carlo Functions ===== */

    /**
     * Calculate foul risk for multiple hands using SIMD Monte Carlo
     *
     * @param batch Input batch with partial hands and cards to test
     * @param simulations_per_hand Number of Monte Carlo simulations per hand
     * @param mode SIMD processing mode
     * @return 0 on success, -1 on error
     */
    int OFC_CalculateFoulRiskSIMD(
        ofc_simd_monte_carlo_batch_t *batch,
        int simulations_per_hand,
        ofc_processing_mode_t mode);

    /* ===== SIMD Royalties Calculation ===== */

    /**
     * Batch multiple foul risk calculations for maximum SIMD efficiency
     * This is the function that should be used by applications wanting best performance
     */
    int OFC_CalculateMultipleFoulRisksSIMD(
        const ofc_hand_t *partial_hands,
        const int *cards,
        const ofc_position_t *positions,
        int num_hands,
        int simulations_per_hand,
        float *foul_risks /* Output array */
    );

    /**
     * Calculate royalties for multiple hands using SIMD
     *
     * @param hands Array of complete OFC hands
     * @param batch_size Number of hands
     * @param royalties Output array of royalties structures
     * @param mode SIMD processing mode
     * @return 0 on success, -1 on error
     */
    int OFC_CalculateRoyaltiesSIMD(
        const ofc_hand_t *hands,
        int batch_size,
        ofc_royalties_t *royalties,
        ofc_processing_mode_t mode);

    /* ===== SIMD Utility Functions ===== */

    /**
     * Get SIMD capability name for debugging/logging
     */
    const char *OFC_GetSIMDCapabilityName(ofc_simd_capability_t capability);

    /**
     * Get processing mode name for debugging/logging
     */
    const char *OFC_GetProcessingModeName(ofc_processing_mode_t mode);

    /**
     * Enable/disable SIMD optimizations at runtime
     * Useful for testing and benchmarking
     */
    void OFC_SetSIMDEnabled(int enabled);
    int OFC_IsSIMDEnabled(void);

    /* ===== Performance Measurement ===== */

    /**
     * SIMD Monte Carlo for single hand with automatic SIMD batching
     */
    float OFC_CalculateFoulRiskSIMDSingle(
        const ofc_hand_t *partial_hand,
        int card,
        ofc_position_t position,
        int simulations);

    /**
     * Benchmark Monte Carlo SIMD vs CPU performance
     */
    void OFC_BenchmarkMonteCarloSIMD(void);

    /**
     * Benchmark SIMD vs CPU performance for different batch sizes
     * Used for performance validation and optimal batch size determination
     */
    void OFC_BenchmarkSIMDPerformance(void);

#ifdef __cplusplus
}
#endif

#endif /* __OFC_SIMD_H__ */
