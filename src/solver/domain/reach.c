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
    memcpy(dst->v, src->v, dst->n * sizeof(double));
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
 * Neumaier summation.
 *
 * The plain Kahan form assumes the running total dominates the term being
 * added and loses the compensation when it does not. A reach vector mixes a
 * few large weights with a long tail of small ones — exactly that case — so
 * the branch below picks up the lost part from whichever side was smaller.
 */
double pe_vec_sum(const pe_vec_t *v)
{
    double sum = 0.0;
    double c = 0.0;
    size_t i;

    if (v == NULL || v->v == NULL)
        return 0.0;

    for (i = 0; i < v->n; ++i)
    {
        double x = v->v[i];
        double t = sum + x;
        if (fabs(sum) >= fabs(x))
            c += (sum - t) + x;
        else
            c += (x - t) + sum;
        sum = t;
    }
    return sum + c;
}

double pe_vec_dot(const pe_vec_t *a, const pe_vec_t *b)
{
    double sum = 0.0;
    double c = 0.0;
    size_t i;

    if (a == NULL || b == NULL || a->v == NULL || b->v == NULL)
        return 0.0;
    if (a->n != b->n)
        return 0.0;

    for (i = 0; i < a->n; ++i)
    {
        double x = a->v[i] * b->v[i];
        double t = sum + x;
        if (fabs(sum) >= fabs(x))
            c += (sum - t) + x;
        else
            c += (x - t) + sum;
        sum = t;
    }
    return sum + c;
}
