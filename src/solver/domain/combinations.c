/*
 * combinations.c - Indexing k-subsets (CHN-02)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Colexicographic order. The rank of {c0 < c1 < ... < c(k-1)} is
 *
 *     sum over i of C(ci, i+1)
 *
 * and unranking runs the sum backwards: for the highest position, find the
 * largest c with C(c, k) <= rank, subtract it, and continue. A descending scan
 * is enough — no division, no table, and the binomials involved are small.
 */

#include <poker_eval/solver/pe_combinations.h>

#include <stddef.h>

uint64_t pe_comb_count(unsigned n, unsigned k)
{
    uint64_t result = 1;
    unsigned i;

    if (k > n)
        return 0;
    if (k == 0)
        return 1;
    /* C(n,k) == C(n,n-k); taking the smaller keeps the loop short and the
       intermediate products well below the saturation point for any k this
       module accepts. */
    if (k > n - k)
        k = n - k;

    for (i = 1; i <= k; ++i)
    {
        uint64_t prev = result;
        /* Multiply then divide: the product of i consecutive integers is
           divisible by i!, and dividing at each step keeps it exact. */
        result = result * (uint64_t)(n - k + i) / (uint64_t)i;
        if (result < prev && prev != 0)
            return UINT64_MAX;   /* saturate rather than wrap */
    }
    return result;
}

pe_solver_status_t pe_comb_unrank(unsigned n, unsigned k, uint64_t rank,
                                  unsigned *out)
{
    unsigned i;

    if (out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (k == 0 || k > PE_COMB_MAX_K || k > n)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    if (rank >= pe_comb_count(n, k))
        return PE_SOLVER_ERR_INVALID_CONFIG;

    /* Highest position first: the largest c whose C(c, i) still fits. */
    for (i = k; i >= 1; --i)
    {
        unsigned c = i - 1;
        while (pe_comb_count(c + 1, i) <= rank)
            c++;
        out[i - 1] = c;
        rank -= pe_comb_count(c, i);
    }
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_comb_rank(unsigned n, unsigned k, const unsigned *values,
                                uint64_t *out_rank)
{
    uint64_t rank = 0;
    unsigned i;

    if (values == NULL || out_rank == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (k == 0 || k > PE_COMB_MAX_K || k > n)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    for (i = 0; i < k; ++i)
    {
        if (values[i] >= n)
            return PE_SOLVER_ERR_INVALID_CONFIG;
        if (i > 0 && values[i] <= values[i - 1])
            return PE_SOLVER_ERR_INVALID_CONFIG;   /* not strictly increasing */
        rank += pe_comb_count(values[i], i + 1);
    }

    *out_rank = rank;
    return PE_SOLVER_OK;
}
