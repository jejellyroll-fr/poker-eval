/*
 * pe_average.h - Vector strategy averaging (architecture v3, VEC-04)
 */

#ifndef POKER_EVAL_PE_AVERAGE_H
#define POKER_EVAL_PE_AVERAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Add one strategy observation to a vector average.
 *
 * `weighted` is laid out [action][combo]. `normalizer` has one entry per
 * combo. Each observation contributes `strategy[a,c] * reach[c] * weight` to
 * the numerator and `reach[c] * weight` to that combo's denominator. The
 * caller may use weight 1 for uniform averaging or the iteration number for
 * linear averaging.
 *
 * @return 0 on success, -1 for invalid pointers, dimensions, reach or weight.
 */
int pe_average_accumulate_vector(double *weighted, double *normalizer,
                                 const double *strategy,
                                 const double *reach,
                                 uint16_t action_count,
                                 uint16_t combo_count, double weight);

/** Add one sampled observation with an inverse sampling-probability weight. */
int pe_average_accumulate_importance_vector(
    double *weighted, double *normalizer, const double *strategy,
    const double *reach, uint16_t action_count, uint16_t combo_count,
    double sampling_probability, double weight);

/**
 * Add one observation using delayed linear CFR+ averaging.
 *
 * Iterations are one-based. Iterations <= `averaging_delay` are intentionally
 * no-ops; iteration `averaging_delay + 1` contributes with weight 1, then the
 * weight increases linearly.
 */
int pe_average_accumulate_delayed_linear_vector(
    double *weighted, double *normalizer, const double *strategy,
    const double *reach, uint16_t action_count, uint16_t combo_count,
    uint64_t iteration, uint64_t averaging_delay);

/**
 * Normalize a weighted average into another [action][combo] span.
 *
 * Combos with a zero denominator receive a uniform distribution rather than
 * uninitialized or NaN values.
 *
 * @return 0 on success, -1 for invalid pointers or dimensions.
 */
int pe_average_finalize_vector(const double *weighted, const double *normalizer,
                               double *out_strategy,
                               uint16_t action_count,
                               uint16_t combo_count);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_AVERAGE_H */
