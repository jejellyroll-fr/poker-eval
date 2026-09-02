/*
 * reach.c - Per-combo vectors (RNG-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Small, hot, and deliberately unclever. These loops are the innermost work of
 * the vector lane; what they need is to be obviously correct and easy for a
 * compiler to vectorise, not to be hand-optimised now against a traversal that
 * does not exist yet.
 *
 * The one place that is not naive is the summation. See pe_vec_sum.
 */

#include <poker_eval/solver/pe_vector.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

pe_solver_status_t pe_vec_alloc(pe_vec_t *out, size_t n)
{
    if (out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    out->v = NULL;
    out->n = 0;

    /* A zero-length vector is a caller who has not worked out how many combos
       there are, not a range that reaches nothing. */
    if (n == 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    out->v = (double *)calloc(n, sizeof(double));
    if (out->v == NULL)
        return PE_SOLVER_ERR_OUT_OF_MEMORY;

    out->n = n;
    return PE_SOLVER_OK;
}

void pe_vec_free(pe_vec_t *v)
{
    if (v == NULL)
        return;
    free(v->v);
    v->v = NULL;
    v->n = 0;
}

pe_vec_t pe_vec_wrap(double *data, size_t n)
{
    pe_vec_t v;
    v.v = data;
    v.n = (data != NULL) ? n : 0;
    return v;
}

void pe_vec_fill(pe_vec_t *v, double x)
{
    size_t i;
    if (v == NULL || v->v == NULL)
        return;
    for (i = 0; i < v->n; ++i)
        v->v[i] = x;
}

void pe_vec_copy(pe_vec_t *dst, const pe_vec_t *src)
{
    if (dst == NULL || src == NULL || dst->v == NULL || src->v == NULL)
        return;
    /* Mismatched lengths are a caller error the hot path cannot report, so
       nothing happens rather than something partial. */
    if (dst->n != src->n)
        return;
    for (size_t i = 0u; i < dst->n; ++i)
        dst->v[i] = src->v[i];
}

void pe_vec_scale(pe_vec_t *v, double s)
{
    size_t i;
    if (v == NULL || v->v == NULL)
        return;
    for (i = 0; i < v->n; ++i)
        v->v[i] *= s;
}

void pe_vec_mul(pe_vec_t *dst, const pe_vec_t *src)
{
    size_t i;
    if (dst == NULL || src == NULL || dst->v == NULL || src->v == NULL)
        return;
    if (dst->n != src->n)
        return;
    for (i = 0; i < dst->n; ++i)
        dst->v[i] *= src->v[i];
}

void pe_vec_axpy(pe_vec_t *dst, double a, const pe_vec_t *src)
{
    size_t i;
    if (dst == NULL || src == NULL || dst->v == NULL || src->v == NULL)
        return;
    if (dst->n != src->n)
        return;
    for (i = 0; i < dst->n; ++i)
        dst->v[i] += a * src->v[i];
}

/*
 * Branchless Neumaier accumulation. Four independent lanes shorten the
 * dependency chain and give the compiler room to vectorise the hot input
 * loads. The boolean-to-double selection compiles to a compare plus select on
 * the supported compilers; both correction expressions are evaluated so the
 * loop contains no unpredictable branch.
 */
static inline void pe_neumaier_add(double x, double *sum, double *correction)
{
    double t = *sum + x;
    double sum_correction = (*sum - t) + x;
    double x_correction = (x - t) + *sum;
    double sum_is_larger = fabs(*sum) >= fabs(x) ? 1.0 : 0.0;

    *correction += sum_is_larger * sum_correction +
                   (1.0 - sum_is_larger) * x_correction;
    *sum = t;
}

#define PE_VEC_SUM_LANES 4u

double pe_vec_sum(const pe_vec_t *v)
{
    double sums[PE_VEC_SUM_LANES] = {0.0, 0.0, 0.0, 0.0};
    double corrections[PE_VEC_SUM_LANES] = {0.0, 0.0, 0.0, 0.0};
    double sum = 0.0;
    double correction = 0.0;
    size_t i;
    size_t lane;

    if (v == NULL || v->v == NULL)
        return 0.0;

    for (i = 0; i < v->n; ++i)
        pe_neumaier_add(v->v[i], &sums[i % PE_VEC_SUM_LANES],
                        &corrections[i % PE_VEC_SUM_LANES]);

    for (lane = 0u; lane < PE_VEC_SUM_LANES; ++lane)
    {
        pe_neumaier_add(sums[lane], &sum, &correction);
        pe_neumaier_add(corrections[lane], &sum, &correction);
    }
    return sum + correction;
}

double pe_vec_dot(const pe_vec_t *a, const pe_vec_t *b)
{
    double sums[PE_VEC_SUM_LANES] = {0.0, 0.0, 0.0, 0.0};
    double corrections[PE_VEC_SUM_LANES] = {0.0, 0.0, 0.0, 0.0};
    double sum = 0.0;
    double correction = 0.0;
    size_t i;
    size_t lane;

    if (a == NULL || b == NULL || a->v == NULL || b->v == NULL)
        return 0.0;
    if (a->n != b->n)
        return 0.0;

    for (i = 0; i < a->n; ++i)
        pe_neumaier_add(a->v[i] * b->v[i], &sums[i % PE_VEC_SUM_LANES],
                        &corrections[i % PE_VEC_SUM_LANES]);

    for (lane = 0u; lane < PE_VEC_SUM_LANES; ++lane)
    {
        pe_neumaier_add(sums[lane], &sum, &correction);
        pe_neumaier_add(corrections[lane], &sum, &correction);
    }
    return sum + correction;
}
