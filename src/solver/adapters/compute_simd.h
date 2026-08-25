#ifndef PE_COMPUTE_SIMD_H
#define PE_COMPUTE_SIMD_H

#include <stddef.h>

/* Sum positive regrets using the best ISA compiled and validated for this
 * process. The result is an optimization of regret-matching only; callers
 * still own input validation and the scalar remainder. */
float pe_compute_simd_positive_sum(const float *values, size_t count);

/* True only when the SIMD kernel is both compiled and enabled at runtime. */
int pe_compute_simd_enabled(void);

#endif
