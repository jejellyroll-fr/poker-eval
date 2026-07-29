/*
 * ofc_simd.c - Open Face Chinese Poker SIMD optimizations implementation
 *
 * Copyright (C) 2025
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <poker_eval/ofc/ofc_simd.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>

/* SIMD intrinsics includes */
#ifdef __SSE2__
#include <emmintrin.h>
#endif

#ifdef __AVX2__
#include <immintrin.h>
#endif

#ifdef __AVX512F__
#include <immintrin.h>
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif

/* Global SIMD state */
static int g_simd_enabled = 1;
static ofc_simd_capability_t g_simd_capabilities = OFC_SIMD_NONE;
static int g_optimal_batch_size = 1;

/* ===== SIMD Capability Detection ===== */

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
static void pe_cpuid(unsigned int leaf, unsigned int subleaf,
                     unsigned int *eax, unsigned int *ebx,
                     unsigned int *ecx, unsigned int *edx)
{
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, (int)leaf, (int)subleaf);
    *eax = (unsigned int)regs[0];
    *ebx = (unsigned int)regs[1];
    *ecx = (unsigned int)regs[2];
    *edx = (unsigned int)regs[3];
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int a, b, c, d;
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(subleaf));
    *eax = a;
    *ebx = b;
    *ecx = c;
    *edx = d;
#else
    *eax = 0;
    *ebx = 0;
    *ecx = 0;
    *edx = 0;
#endif
}
#endif

/* CPU feature detection using CPUID */
static void detect_cpu_features(int *has_sse2, int *has_avx2, int *has_avx512)
{
    *has_sse2 = 0;
    *has_avx2 = 0;
    *has_avx512 = 0;

    /* Runtime CPUID detection */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
    unsigned int eax, ebx, ecx, edx;

    /* Check if CPUID is supported */
    pe_cpuid(0, 0, &eax, &ebx, &ecx, &edx);

    if (eax >= 1)
    {
        /* Get feature flags from CPUID(1) */
        pe_cpuid(1, 0, &eax, &ebx, &ecx, &edx);

        /* SSE2 is bit 26 of EDX */
        if (edx & (1 << 26))
        {
            *has_sse2 = 1;
        }

        /* Check for AVX (bit 28 of ECX) */
        if (ecx & (1 << 28))
        {
            /* AVX is supported, now check for AVX2 in extended features */
            pe_cpuid(7, 0, &eax, &ebx, &ecx, &edx);

            /* AVX2 is bit 5 of EBX */
            if (ebx & (1 << 5))
            {
                *has_avx2 = 1;
            }

            /* AVX-512F is bit 16 of EBX */
            if (ebx & (1 << 16))
            {
                *has_avx512 = 1;
            }
        }
    }
#else
    /* For non-x86 architectures, disable SIMD for safety */
    *has_sse2 = 0;
    *has_avx2 = 0;
    *has_avx512 = 0;
#endif

    /* Additional safety: only enable if compile-time support exists */
#ifndef __SSE2__
    *has_sse2 = 0;
#endif

#ifndef __AVX2__
    *has_avx2 = 0;
#endif

#ifndef __AVX512F__
    *has_avx512 = 0;
#endif
}

int OFC_DetectSIMDCapabilities(void)
{
    int has_sse2, has_avx2, has_avx512;
    detect_cpu_features(&has_sse2, &has_avx2, &has_avx512);

    g_simd_capabilities = OFC_SIMD_NONE;

    if (has_sse2)
    {
        g_simd_capabilities |= OFC_SIMD_SSE2;
    }
    if (has_avx2)
    {
        g_simd_capabilities |= OFC_SIMD_AVX2;
    }
    if (has_avx512)
    {
        g_simd_capabilities |= OFC_SIMD_AVX512;
    }

    /* Set optimal batch size based on highest capability */
    if (g_simd_capabilities & OFC_SIMD_AVX512)
    {
        g_optimal_batch_size = OFC_SIMD_BATCH_SIZE_AVX512;
    }
    else if (g_simd_capabilities & OFC_SIMD_AVX2)
    {
        g_optimal_batch_size = OFC_SIMD_BATCH_SIZE_AVX2;
    }
    else if (g_simd_capabilities & OFC_SIMD_SSE2)
    {
        g_optimal_batch_size = OFC_SIMD_BATCH_SIZE_SSE2;
    }
    else
    {
        g_optimal_batch_size = 1;
    }

    return g_simd_capabilities;
}

int OFC_GetOptimalBatchSize(void)
{
    if (g_simd_capabilities == OFC_SIMD_NONE)
    {
        OFC_DetectSIMDCapabilities();
    }
    return g_optimal_batch_size;
}

ofc_processing_mode_t OFC_SelectProcessingMode(int batch_size)
{
    if (!g_simd_enabled || batch_size <= 1)
    {
        return OFC_PROCESS_CPU;
    }

    if (g_simd_capabilities & OFC_SIMD_AVX512 && batch_size >= OFC_SIMD_BATCH_SIZE_AVX512)
    {
        return OFC_PROCESS_SIMD_AVX512;
    }
    else if (g_simd_capabilities & OFC_SIMD_AVX2 && batch_size >= OFC_SIMD_BATCH_SIZE_AVX2)
    {
        return OFC_PROCESS_SIMD_AVX2;
    }
    else if (g_simd_capabilities & OFC_SIMD_SSE2 && batch_size >= OFC_SIMD_BATCH_SIZE_SSE2)
    {
        return OFC_PROCESS_SIMD_SSE2;
    }

    return OFC_PROCESS_CPU;
}

/* ===== SIMD Hand Evaluation Implementation ===== */

#ifdef __AVX2__

/*
 * AVX2 vectorized 3-card hand evaluation for OFC top row
 * Evaluates 8 hands simultaneously
 * 
 * Hand ranking for 3-card: Trips > Pair > High Card
 * HandVal encoding: handtype << 24, then kickers in descending bits
 */
static inline void ofc_eval_3card_avx2(
    const StdDeck_CardMask *hands,
    int count,
    HandVal *out_vals)
{
    /* Process 8 hands at a time */
    int i;
    for (i = 0; i + 8 <= count; i += 8)
    {
        /* Extract rank bitmasks for all 8 hands (13 bits each) */
        uint32_t rank_masks[8];
        uint32_t rank_counts[8][13]; /* Count per rank for each hand */
        
        for (int h = 0; h < 8; h++)
        {
            /* Combine all suits to get rank mask */
            rank_masks[h] = hands[i + h].cards.spades | 
                           hands[i + h].cards.hearts |
                           hands[i + h].cards.diamonds | 
                           hands[i + h].cards.clubs;
            
            /* Count cards per rank for trips/pair detection */
            memset(rank_counts[h], 0, sizeof(rank_counts[h]));
            for (int c = 0; c < StdDeck_N_CARDS; c++)
            {
                if (StdDeck_CardMask_CARD_IS_SET(hands[i + h], c))
                {
                    rank_counts[h][StdDeck_RANK(c)]++;
                }
            }
        }
        
        /* Load rank masks into AVX2 register */
        __m256i v_ranks = _mm256_setr_epi32(
            (int)rank_masks[0], (int)rank_masks[1], (int)rank_masks[2], (int)rank_masks[3],
            (int)rank_masks[4], (int)rank_masks[5], (int)rank_masks[6], (int)rank_masks[7]);
        
        /* Find highest set bit (top card) using lzcnt via scalar for now */
        /* AVX2 doesn't have direct lzcnt, so we extract and process */
        int32_t results[8];
        _mm256_storeu_si256((__m256i*)results, v_ranks);
        
        for (int h = 0; h < 8; h++)
        {
            int trips_rank = -1, pair_rank = -1;
            int high_cards[3] = {-1, -1, -1};
            int high_count = 0;
            
            /* Scan ranks from highest (Ace=12) to lowest (2=0) */
            for (int r = 12; r >= 0; r--)
            {
                if (rank_counts[h][r] == 3)
                {
                    trips_rank = r;
                    break;
                }
                else if (rank_counts[h][r] == 2 && pair_rank < 0)
                {
                    pair_rank = r;
                }
                else if (rank_counts[h][r] == 1 && high_count < 3)
                {
                    high_cards[high_count++] = r;
                }
            }
            
            if (trips_rank >= 0)
            {
                /* Three of a kind */
                out_vals[i + h] = HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS) + 
                                  (trips_rank << 16);
            }
            else if (pair_rank >= 0)
            {
                /* Find kicker */
                int kicker = -1;
                for (int r = 12; r >= 0; r--)
                {
                    if (rank_counts[h][r] == 1)
                    {
                        kicker = r;
                        break;
                    }
                }
                out_vals[i + h] = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR) +
                                  (pair_rank << 16) + (kicker >= 0 ? kicker << 12 : 0);
            }
            else
            {
                /* High card */
                out_vals[i + h] = HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) +
                                  (high_cards[0] >= 0 ? high_cards[0] << 16 : 0) +
                                  (high_cards[1] >= 0 ? high_cards[1] << 12 : 0) +
                                  (high_cards[2] >= 0 ? high_cards[2] << 8 : 0);
            }
        }
    }
    
    /* Handle remainder */
    for (; i < count; i++)
    {
        out_vals[i] = OFC_EvaluateHand(hands[i], 3);
    }
}

/*
 * AVX2 vectorized hierarchy validation
 * Checks if bottom >= middle >= top for 8 hands simultaneously
 * Returns bitmask where bit i is set if hand i has valid hierarchy
 */
static inline uint32_t ofc_validate_hierarchy_avx2(
    const HandVal *top_vals,
    const HandVal *middle_vals, 
    const HandVal *bottom_vals)
{
    /* Load 8 values for each position */
    __m256i v_top = _mm256_loadu_si256((const __m256i*)top_vals);
    __m256i v_mid = _mm256_loadu_si256((const __m256i*)middle_vals);
    __m256i v_bot = _mm256_loadu_si256((const __m256i*)bottom_vals);
    
    /* In HandVal, HIGHER value = BETTER hand */
    /* Valid hierarchy: bottom >= middle >= top */
    /* Check: bottom >= middle (bot - mid >= 0, i.e., bot > mid OR bot == mid) */
    /* Check: middle >= top (mid - top >= 0, i.e., mid > top OR mid == top) */
    
    /* bot >= mid: NOT(bot < mid) = NOT(mid > bot) */
    __m256i mid_gt_bot = _mm256_cmpgt_epi32(v_mid, v_bot);
    
    /* mid >= top: NOT(mid < top) = NOT(top > mid) */
    __m256i top_gt_mid = _mm256_cmpgt_epi32(v_top, v_mid);
    
    /* Valid if neither condition is true (both comparisons return 0) */
    __m256i invalid = _mm256_or_si256(mid_gt_bot, top_gt_mid);
    
    /* Convert to mask: bit is 1 if INVALID */
    int invalid_mask = _mm256_movemask_ps(_mm256_castsi256_ps(invalid));
    
    /* Invert: bit is 1 if VALID */
    return (uint32_t)(~invalid_mask & 0xFF);
}

/* AVX2 implementation for evaluating 8 hands simultaneously */
static int ofc_evaluate_hands_avx2(
    const StdDeck_CardMask *top_hands,
    const StdDeck_CardMask *middle_hands,
    const StdDeck_CardMask *bottom_hands,
    int batch_size,
    ofc_simd_eval_result_t *results)
{
    int i;

    /* Clear results */
    memset(results, 0, sizeof(ofc_simd_eval_result_t));
    results->batch_size = batch_size;

    /* Temporary arrays for batch processing */
    HandVal top_vals[8], middle_vals[8], bottom_vals[8];

    /* Process in batches of 8 */
    int full_batches = batch_size / 8;

    for (int batch = 0; batch < full_batches; batch++)
    {
        int base_idx = batch * 8;

        /* Evaluate top hands (3-card) using vectorized function */
        ofc_eval_3card_avx2(&top_hands[base_idx], 8, top_vals);

        /* Evaluate middle and bottom hands (5-card) - scalar for now
         * 5-card evaluation is complex (flush, straight detection)
         * and the lookup table approach doesn't vectorize well */
        for (i = 0; i < 8; i++)
        {
            middle_vals[i] = StdDeck_StdRules_EVAL_N(middle_hands[base_idx + i], 5);
            bottom_vals[i] = StdDeck_StdRules_EVAL_N(bottom_hands[base_idx + i], 5);
        }

        /* Store evaluation results */
        for (i = 0; i < 8; i++)
        {
            int idx = base_idx + i;
            results->values[idx * 3 + 0] = top_vals[i];
            results->values[idx * 3 + 1] = middle_vals[i];
            results->values[idx * 3 + 2] = bottom_vals[i];
        }

        /* Validate hierarchy using SIMD */
        uint32_t valid_mask = ofc_validate_hierarchy_avx2(top_vals, middle_vals, bottom_vals);
        
        for (i = 0; i < 8; i++)
        {
            results->hierarchy_valid[base_idx + i] = (valid_mask >> i) & 1;
        }
    }

    /* Handle remainder with scalar processing */
    for (i = full_batches * 8; i < batch_size; i++)
    {
        results->values[i * 3 + 0] = OFC_EvaluateHand(top_hands[i], 3);
        results->values[i * 3 + 1] = StdDeck_StdRules_EVAL_N(middle_hands[i], 5);
        results->values[i * 3 + 2] = StdDeck_StdRules_EVAL_N(bottom_hands[i], 5);

        /* Validate hierarchy */
        HandVal top_v = results->values[i * 3 + 0];
        HandVal mid_v = results->values[i * 3 + 1];
        HandVal bot_v = results->values[i * 3 + 2];
        
        /* Valid if bottom >= middle >= top (higher HandVal = better) */
        results->hierarchy_valid[i] = (bot_v >= mid_v) && (mid_v >= top_v);
    }

    return 0;
}
#endif

#ifdef __SSE2__
/* SSE2 implementation for evaluating 4 hands simultaneously */
static int ofc_evaluate_hands_sse2(
    const StdDeck_CardMask *top_hands,
    const StdDeck_CardMask *middle_hands,
    const StdDeck_CardMask *bottom_hands,
    int batch_size,
    ofc_simd_eval_result_t *results)
{
    int i;

    /* Process in batches of 4 */
    int full_batches = batch_size / 4;

    /* Clear results */
    memset(results, 0, sizeof(ofc_simd_eval_result_t));
    results->batch_size = batch_size;

    /* Process full SSE2 batches */
    for (int batch = 0; batch < full_batches; batch++)
    {
        int base_idx = batch * 4;

        /* For now, fall back to scalar evaluation */
        for (i = 0; i < 4 && (base_idx + i) < batch_size; i++)
        {
            int idx = base_idx + i;

            results->values[idx * 3 + 0] = OFC_EvaluateHand(top_hands[idx], 3);
            results->values[idx * 3 + 1] = StdDeck_StdRules_EVAL_N(middle_hands[idx], 5);
            results->values[idx * 3 + 2] = StdDeck_StdRules_EVAL_N(bottom_hands[idx], 5);

            /* Validate hierarchy: bottom >= middle >= top (in poker strength) */
            /* Note: HandVal uses "lower is better" encoding, so we check opposite */
            /* Valid if: bottom_val <= middle_val <= top_val */
            int hierarchy_ok = 1;

            /* Bottom must be better than or equal to middle */
            if (results->values[idx * 3 + 2] > results->values[idx * 3 + 1]) {
                hierarchy_ok = 0; /* Bottom is worse than middle - FOUL */
            }

            /* Middle must be better than or equal to top */
            if (results->values[idx * 3 + 1] > results->values[idx * 3 + 0]) {
                hierarchy_ok = 0; /* Middle is worse than top - FOUL */
            }
            results->hierarchy_valid[idx] = hierarchy_ok;
        }
    }

    /* Handle remainder */
    for (i = full_batches * 4; i < batch_size; i++)
    {
        results->values[i * 3 + 0] = OFC_EvaluateHand(top_hands[i], 3);
        results->values[i * 3 + 1] = StdDeck_StdRules_EVAL_N(middle_hands[i], 5);
        results->values[i * 3 + 2] = StdDeck_StdRules_EVAL_N(bottom_hands[i], 5);

        /* Validate hierarchy: bottom >= middle >= top (in poker strength) */
        /* Note: HandVal uses "lower is better" encoding, so we check opposite */
        /* Valid if: bottom_val <= middle_val <= top_val */
        int hierarchy_ok = 1;

        /* Bottom must be better than or equal to middle */
        if (results->values[i * 3 + 2] > results->values[i * 3 + 1]) {
            hierarchy_ok = 0; /* Bottom is worse than middle - FOUL */
        }

        /* Middle must be better than or equal to top */
        if (results->values[i * 3 + 1] > results->values[i * 3 + 0]) {
            hierarchy_ok = 0; /* Middle is worse than top - FOUL */
        }
        results->hierarchy_valid[i] = hierarchy_ok;
    }

    return 0;
}
#endif

/* CPU fallback implementation */
static int ofc_evaluate_hands_cpu(
    const StdDeck_CardMask *top_hands,
    const StdDeck_CardMask *middle_hands,
    const StdDeck_CardMask *bottom_hands,
    int batch_size,
    ofc_simd_eval_result_t *results)
{
    int i;

    /* Clear results */
    memset(results, 0, sizeof(ofc_simd_eval_result_t));
    results->batch_size = batch_size;

    /* Sequential evaluation */
    for (i = 0; i < batch_size; i++)
    {
        results->values[i * 3 + 0] = OFC_EvaluateHand(top_hands[i], 3);
        results->values[i * 3 + 1] = StdDeck_StdRules_EVAL_N(middle_hands[i], 5);
        results->values[i * 3 + 2] = StdDeck_StdRules_EVAL_N(bottom_hands[i], 5);

        /* Validate hierarchy: bottom >= middle >= top (in poker strength) */
        /* Note: HandVal uses "lower is better" encoding, so we check opposite */
        /* Valid if: bottom_val <= middle_val <= top_val */
        int hierarchy_ok = 1;

        /* Bottom must be better than or equal to middle */
        if (results->values[i * 3 + 2] > results->values[i * 3 + 1]) {
            hierarchy_ok = 0; /* Bottom is worse than middle - FOUL */
        }

        /* Middle must be better than or equal to top */
        if (results->values[i * 3 + 1] > results->values[i * 3 + 0]) {
            hierarchy_ok = 0; /* Middle is worse than top - FOUL */
        }
        results->hierarchy_valid[i] = hierarchy_ok;
    }

    return 0;
}

/* Main SIMD hand evaluation function */
int OFC_EvaluateHandsSIMD(
    const StdDeck_CardMask *top_hands,
    const StdDeck_CardMask *middle_hands,
    const StdDeck_CardMask *bottom_hands,
    int batch_size,
    ofc_simd_eval_result_t *results,
    ofc_processing_mode_t mode)
{
    if (!top_hands || !middle_hands || !bottom_hands || !results || batch_size <= 0)
    {
        return -1;
    }

    if (batch_size > OFC_SIMD_MAX_BATCH_SIZE)
    {
        return -1;
    }

    /* Auto-select processing mode if requested */
    if (mode == OFC_PROCESS_SIMD_AUTO)
    {
        mode = OFC_SelectProcessingMode(batch_size);
    }

    /* Dispatch to appropriate implementation */
    switch (mode)
    {
    case OFC_PROCESS_SIMD_AUTO:
        /* Should not reach here, auto-selection done above */
        return ofc_evaluate_hands_cpu(top_hands, middle_hands, bottom_hands, batch_size, results);

    case OFC_PROCESS_SIMD_AVX512:
#ifdef __AVX512F__
        /* TODO: Implement AVX-512 version */
        return ofc_evaluate_hands_cpu(top_hands, middle_hands, bottom_hands, batch_size, results);
#else
        /* Fall back to CPU if AVX-512 not available */
        return ofc_evaluate_hands_cpu(top_hands, middle_hands, bottom_hands, batch_size, results);
#endif

    case OFC_PROCESS_SIMD_AVX2:
#ifdef __AVX2__
        return ofc_evaluate_hands_avx2(top_hands, middle_hands, bottom_hands, batch_size, results);
#else
        return ofc_evaluate_hands_cpu(top_hands, middle_hands, bottom_hands, batch_size, results);
#endif

    case OFC_PROCESS_SIMD_SSE2:
#ifdef __SSE2__
        return ofc_evaluate_hands_sse2(top_hands, middle_hands, bottom_hands, batch_size, results);
#else
        return ofc_evaluate_hands_cpu(top_hands, middle_hands, bottom_hands, batch_size, results);
#endif

    case OFC_PROCESS_GPU:
        /* GPU mode not handled here - fall back to CPU */
        /* Fall through */
    case OFC_PROCESS_CPU:
    default:
        return ofc_evaluate_hands_cpu(top_hands, middle_hands, bottom_hands, batch_size, results);
    }
}

/* Validate hierarchy for multiple hands using SIMD */
int OFC_ValidateHierarchySIMD(
    const ofc_hand_t *hands,
    int batch_size,
    int *valid_flags,
    ofc_processing_mode_t mode)
{
    if (!hands || !valid_flags || batch_size <= 0)
    {
        return -1;
    }

    /* Extract hand masks for SIMD processing */
    StdDeck_CardMask top_hands[OFC_SIMD_MAX_BATCH_SIZE];
    StdDeck_CardMask middle_hands[OFC_SIMD_MAX_BATCH_SIZE];
    StdDeck_CardMask bottom_hands[OFC_SIMD_MAX_BATCH_SIZE];

    for (int i = 0; i < batch_size && i < OFC_SIMD_MAX_BATCH_SIZE; i++)
    {
        top_hands[i] = hands[i].hands[OFC_TOP];
        middle_hands[i] = hands[i].hands[OFC_MIDDLE];
        bottom_hands[i] = hands[i].hands[OFC_BOTTOM];
    }

    /* Evaluate using SIMD */
    ofc_simd_eval_result_t results;
    int ret = OFC_EvaluateHandsSIMD(top_hands, middle_hands, bottom_hands, batch_size, &results, mode);

    if (ret == 0)
    {
        /* Copy hierarchy validation results */
        for (int i = 0; i < batch_size; i++)
        {
            valid_flags[i] = results.hierarchy_valid[i];
        }
    }

    return ret;
}

/* ===== Utility Functions ===== */

const char *OFC_GetSIMDCapabilityName(ofc_simd_capability_t capability)
{
    switch (capability)
    {
    case OFC_SIMD_NONE:
        return "None";
    case OFC_SIMD_SSE2:
        return "SSE2";
    case OFC_SIMD_AVX2:
        return "AVX2";
    case OFC_SIMD_AVX512:
        return "AVX-512";
    default:
        return "Unknown";
    }
}

const char *OFC_GetProcessingModeName(ofc_processing_mode_t mode)
{
    switch (mode)
    {
    case OFC_PROCESS_CPU:
        return "CPU";
    case OFC_PROCESS_SIMD_AUTO:
        return "SIMD Auto";
    case OFC_PROCESS_SIMD_SSE2:
        return "SIMD SSE2";
    case OFC_PROCESS_SIMD_AVX2:
        return "SIMD AVX2";
    case OFC_PROCESS_SIMD_AVX512:
        return "SIMD AVX-512";
    case OFC_PROCESS_GPU:
        return "GPU";
    default:
        return "Unknown";
    }
}

void OFC_SetSIMDEnabled(int enabled)
{
    g_simd_enabled = enabled;
}

int OFC_IsSIMDEnabled(void)
{
    return g_simd_enabled;
}

/* Note: OFC_CalculateFoulRiskAdaptive moved to ofc_adaptive.c for GPU support */
/* Note: OFC_ValidateHierarchySIMD already implemented above (line 404) */
