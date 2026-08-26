#include "compute_simd.h"

#include <poker_eval/equity/simd_operations.h>

#include <math.h>

#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define PE_COMPUTE_TARGET_AVX2 __attribute__((target("avx2")))
#define PE_COMPUTE_TARGET_AVX512 __attribute__((target("avx512f")))
#define PE_COMPUTE_X86_MULTIVERSION 1
#else
#define PE_COMPUTE_TARGET_AVX2
#define PE_COMPUTE_TARGET_AVX512
#endif

static float scalar_positive_sum(const float *values, size_t count)
{
    float sum = 0.0f;
    size_t i;

    for (i = 0u; i < count; ++i)
        if (values[i] > 0.0f)
            sum += values[i];
    return sum;
}

#if defined(PE_COMPUTE_X86_MULTIVERSION) || defined(__AVX2__)
static PE_COMPUTE_TARGET_AVX2 float avx2_positive_sum(const float *values,
                                                      size_t count)
{
    size_t i = 0u;
    float sum = 0.0f;

    for (; i + 8u <= count; i += 8u)
    {
        __m256 v = _mm256_loadu_ps(values + i);
        __m256 positive = _mm256_max_ps(v, _mm256_setzero_ps());
        __m128 lo = _mm256_castps256_ps128(positive);
        __m128 hi = _mm256_extractf128_ps(positive, 1);
        __m128 pair = _mm_add_ps(lo, hi);
        pair = _mm_hadd_ps(pair, pair);
        pair = _mm_hadd_ps(pair, pair);
        sum += _mm_cvtss_f32(pair);
    }
    return sum + scalar_positive_sum(values + i, count - i);
}
#endif

#if defined(PE_COMPUTE_X86_MULTIVERSION) || defined(__AVX512F__)
static PE_COMPUTE_TARGET_AVX512 float avx512_positive_sum(const float *values,
                                                          size_t count)
{
    size_t i = 0u;
    float sum = 0.0f;

    for (; i + 16u <= count; i += 16u)
    {
        __m512 v = _mm512_loadu_ps(values + i);
        __m512 zero = _mm512_setzero_ps();
        sum += _mm512_reduce_add_ps(_mm512_max_ps(v, zero));
    }
    return sum + scalar_positive_sum(values + i, count - i);
}
#endif

#if defined(__aarch64__) || defined(__ARM_NEON)
static float neon_positive_sum(const float *values, size_t count)
{
    size_t i = 0u;
    float sum = 0.0f;

    for (; i + 4u <= count; i += 4u)
    {
        float32x4_t v = vld1q_f32(values + i);
        float32x4_t positive = vmaxq_f32(v, vdupq_n_f32(0.0f));
#if defined(__aarch64__)
        sum += vaddvq_f32(positive);
#else
        float32x2_t pair = vadd_f32(vget_low_f32(positive),
                                    vget_high_f32(positive));
        pair = vpadd_f32(pair, pair);
        sum += vget_lane_f32(pair, 0);
#endif
    }
    return sum + scalar_positive_sum(values + i, count - i);
}
#endif

int pe_compute_simd_enabled(void)
{
    return simd_runtime_capability() != SIMD_NONE;
}

float pe_compute_simd_positive_sum(const float *values, size_t count)
{
    if (!values || count == 0u)
        return values ? scalar_positive_sum(values, count) : 0.0f;

    switch (simd_runtime_capability())
    {
        case SIMD_AVX512:
#if defined(PE_COMPUTE_X86_MULTIVERSION) || defined(__AVX512F__)
            return avx512_positive_sum(values, count);
#endif
            break;
        case SIMD_AVX2:
#if defined(PE_COMPUTE_X86_MULTIVERSION) || defined(__AVX2__)
            return avx2_positive_sum(values, count);
#endif
            break;
        case SIMD_NEON:
#if defined(__aarch64__) || defined(__ARM_NEON)
            return neon_positive_sum(values, count);
#endif
            break;
        case SIMD_NONE:
        case SIMD_SSE2:
        default:
            break;
    }
    return scalar_positive_sum(values, count);
}
