/*
 * eval_7c_simd.h - SIMD-accelerated 7→5 evaluation (scaffold)
 */

#ifndef EVAL_7C_SIMD_H
#define EVAL_7C_SIMD_H

#include <stdint.h>
#include "eval_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Attempt SIMD-accelerated 7-card evaluation.
 * Returns EVAL_INVALID if SIMD path is unavailable or fails,
 * so callers can fallback to scalar enumeration.
 */
eval_t pe_eval_7c_simd(const EvalContext* ctx, mask_t seven_mask);

#ifdef __cplusplus
}
#endif

#endif /* EVAL_7C_SIMD_H */

