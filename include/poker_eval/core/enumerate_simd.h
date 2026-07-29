/* enumerate_simd.h - SIMD optimized enumeration functions */

#ifndef ENUMERATE_SIMD_H
#define ENUMERATE_SIMD_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SIMD optimized double flop holdem enumeration
 * Evaluates two flops in parallel using AVX2 instructions
 * Falls back to standard implementation if AVX2 not available
 */
POKEREVAL_EXPORT int enumExhaustiveDoubleFlop_SIMD(
    StdDeck_CardMask pockets[],
    StdDeck_CardMask board,
    StdDeck_CardMask dead,
    int npockets, int nboard,
    int orderflag,
    enum_result_t *result);

/* Check if SIMD acceleration is available */
POKEREVAL_EXPORT int enumerate_simd_available(void);

/* Get SIMD implementation info */
POKEREVAL_EXPORT const char* enumerate_simd_info(void);

#ifdef __cplusplus
}
#endif

#endif /* ENUMERATE_SIMD_H */
