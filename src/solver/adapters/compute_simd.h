#ifndef PE_COMPUTE_SIMD_H
#define PE_COMPUTE_SIMD_H

#include <stddef.h>

/* Sum positive regrets using the best ISA compiled and validated for this
 * process. The result is an optimization of regret-matching only; callers
 * still own input validation and the scalar remainder. */
float pe_compute_simd_positive_sum(const float *values, size_t count);

/* Apply regret matching in-place for one contiguous action span. Returns 1
 * when an ISA-specific implementation handled the span, 0 otherwise. */
int pe_compute_simd_regret_match(const float *regrets, float *strategies,
                                 size_t count, float positive);

/* True only when the SIMD kernel is both compiled and enabled at runtime. */
int pe_compute_simd_enabled(void);

/* Apply the uniform regret/average update kernel in-place. Returns 1 when an
 * ISA-specific implementation handled the complete span, 0 when callers
 * should use their scalar implementation. Inputs must already be finite. */
int pe_compute_simd_apply_uniform(double *regrets, double *averages,
                                  const double *deltas,
                                  const double *average_deltas,
                                  size_t count, int clamp_regret);

/* Apply a sign-dependent regret discount and weighted average update in-place.
 * Returns 1 when an ISA-specific implementation handled the complete span,
 * 0 when callers should use their scalar implementation. Inputs must already
 * be finite. */
int pe_compute_simd_apply_weighted(double *regrets, double *averages,
                                  const double *deltas,
                                  const double *average_deltas, size_t count,
                                  double positive_factor,
                                  double negative_factor,
                                  double average_scale, int clamp_regret);

#endif
