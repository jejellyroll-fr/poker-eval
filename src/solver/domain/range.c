/*
 * range.c - Solver invariants over a parsed range (RNG-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * No range type is defined here. pe_combo_t and pe_range_t already exist and
 * are what architecture v3 §7.1 describes; what the solver adds is the promise
 * that an array of combos is deduplicated, stably ordered, positively weighted
 * and normalised, established once so the traversal never re-checks it.
 *
 * Sorting is by the card mask read as a 64-bit key. The order has no poker
 * meaning and does not need one — what it needs is to be the same on every
 * run and on every machine, because a combo index is what a checkpoint stores
 * and what a device buffer is addressed by.
 */

#include <poker_eval/solver/pe_range.h>

#include "finite_double.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The card mask is a union: one 64-bit word where the platform has it, two
   32-bit halves otherwise. Both collapse to the same ordering key. */
static uint64_t combo_key(StdDeck_CardMask m)
{
#ifdef USE_INT64
    return (uint64_t)m.cards_n;
#else
    return ((uint64_t)m.cards_nn.n2 << 32) | (uint64_t)m.cards_nn.n1;
#endif
}

static int combo_cmp(const void *a, const void *b)
{
    uint64_t ka = combo_key(((const pe_combo_t *)a)->hand);
    uint64_t kb = combo_key(((const pe_combo_t *)b)->hand);
    if (ka < kb)
        return -1;
    return (ka > kb) ? 1 : 0;
}

pe_solver_status_t pe_solver_range_prepare(pe_range_t *range)
{
    size_t i;
    size_t out;
    double total;

    if (range == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    if (range->combos == NULL || range->count == 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    /* A weight that is negative or not finite means the caller computed it
       wrong. Clamping would hide that inside a solve whose numbers then look
       plausible, so it is refused before anything is changed. */
    for (i = 0; i < range->count; ++i)
    {
        double w = range->combos[i].weight;
        if (!(w >= 0.0) || !pe_finite_double(w))
            return PE_SOLVER_ERR_INVALID_CONFIG;
    }

    qsort(range->combos, range->count, sizeof(pe_combo_t), combo_cmp);

    /* Fold duplicates into the first occurrence and drop non-positive
       weights. Both in one pass, since sorting already grouped them. */
    out = 0;
    for (i = 0; i < range->count;)
    {
        uint64_t key = combo_key(range->combos[i].hand);
        double w = 0.0;
        size_t j = i;

        while (j < range->count && combo_key(range->combos[j].hand) == key)
        {
            w += range->combos[j].weight;
            ++j;
        }

        if (w > 0.0)
        {
            range->combos[out].hand = range->combos[i].hand;
            range->combos[out].weight = w;
            ++out;
        }
        i = j;
    }

    if (out == 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;   /* every weight was zero */

    range->count = out;

    total = 0.0;
    for (i = 0; i < out; ++i)
        total += range->combos[i].weight;

    if (!(total > 0.0) || !pe_finite_double(total))
        return PE_SOLVER_ERR_INVALID_CONFIG;

    for (i = 0; i < out; ++i)
        range->combos[i].weight /= total;

    /* The parsed range reports the sum it was built with; after normalisation
       that sum is 1, and leaving the old value would make the two disagree. */
    range->total_weight = 1.0;
    return PE_SOLVER_OK;
}

pe_range_view_t pe_solver_range_view(const pe_range_t *range)
{
    pe_range_view_t v;

    if (range == NULL)
    {
        v.combos = NULL;
        v.count = 0;
        return v;
    }
    v.combos = range->combos;
    v.count = range->count;
    return v;
}

int pe_solver_range_is_prepared(const pe_range_t *range, double tolerance)
{
    size_t i;
    double total = 0.0;

    if (range == NULL || range->combos == NULL || range->count == 0)
        return 0;

    for (i = 0; i < range->count; ++i)
    {
        double w = range->combos[i].weight;
        if (!(w > 0.0) || !pe_finite_double(w))
            return 0;
        if (i > 0 && combo_cmp(&range->combos[i - 1], &range->combos[i]) >= 0)
            return 0;   /* unsorted, or a duplicate survived */
        total += w;
    }

    return fabs(total - 1.0) <= tolerance;
}
