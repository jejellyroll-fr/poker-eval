/*
 * pe_work_priority_oracle.h - test-only exact oracle for work prioritisation.
 *
 * Header-only, every function `static`, included by exactly one .c
 * (tests/test_work_priority.c). It re-derives the priority from the header's
 * documentation and **never calls the code under test**:
 *
 *   - the score is recomputed in long double, with an explicit sqrtl of the
 *     two standard errors rather than the library's hypot;
 *   - the bucket is found with logl/floorl rather than the library's
 *     threshold loop, so the two disagree if either quantisation is wrong;
 *   - the UNCERTAINTY_AWARE order is found by **brute force over all n!
 *     permutations**, keeping the one that is non-decreasing under the
 *     documented comparator. That validates the counting sort and the
 *     round-robin interleave against the specification, not against
 *     themselves.
 *
 * The comparator itself is pinned by the hand-checkable fixtures in the
 * test, which are asserted before any randomised comparison runs.
 */

#ifndef POKER_EVAL_TESTS_WORK_PRIORITY_ORACLE_H
#define POKER_EVAL_TESTS_WORK_PRIORITY_ORACLE_H

#include <poker_eval/solver/pe_work_priority.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* The largest batch the exhaustive permutation search will take. */
#define ORACLE_MAX_ITEMS 8u

typedef struct oracle_ctx_t
{
    pe_work_priority_config_t config;
    const pe_work_priority_item_t *items;
    size_t count;
    uint64_t epoch;
} oracle_ctx_t;

/* The uncertainty of the leader-versus-runner-up difference, in long double:
   the two-sided reading of the header's `hypot`. */
static long double oracle_uncertainty(const pe_work_priority_item_t *item)
{
    long double a = (long double)item->best_stderr;
    long double b = (long double)item->second_stderr;
    return sqrtl(a * a + b * b);
}

/* uncertainty / max(gap, epsilon), with the documented special cases. */
static long double oracle_score(const pe_work_priority_config_t *config,
                                const pe_work_priority_item_t *item)
{
    long double gap, uncertainty, floor_gap;
    if (item->actions < 2u)
        return 0.0L;
    gap = (long double)item->best - (long double)item->second_best;
    if (!(gap > 0.0L))
        return INFINITY;
    uncertainty = oracle_uncertainty(item);
    if (!(uncertainty >= 0.0L))
        return INFINITY;
    if (!(uncertainty > 0.0L)) /* no spread supplied: the assumed one */
        uncertainty = (long double)config->assumed_stderr;
    floor_gap = (long double)config->epsilon;
    if (gap < floor_gap)
        gap = floor_gap;
    return uncertainty / gap;
}

/* #{k >= 0 : ratio^k <= score}, clamped to the last bucket, via logarithms
   rather than the library's threshold loop. */
static uint32_t oracle_bucket_raw(const pe_work_priority_config_t *config,
                                  long double score)
{
    long double ratio, k;
    if (!(score >= 1.0L))
        return 0u; /* below ratio^0, NaN, or negative */
    ratio = logl((long double)config->bucket_ratio);
    k = floorl(logl(score) / ratio) + 1.0L;
    if (!(k >= 0.0L))
        return 0u;
    if (k >= (long double)(config->buckets - 1u))
        return config->buckets - 1u;
    return (uint32_t)k;
}

/* The coverage-floor tier: true for a decision that has not been measured
   enough, which is ranked ahead of every decision that has. */
static int oracle_below_floor(const oracle_ctx_t *ctx, size_t index)
{
    if (ctx->config.policy == PE_WORK_SCHED_FIFO)
        return 0;
    return ctx->items[index].visits < ctx->config.min_visits;
}

/* The bucket the item is ranked in, after the coverage floor and aging. */
static uint32_t oracle_effective_bucket(const oracle_ctx_t *ctx, size_t index)
{
    const pe_work_priority_config_t *config = &ctx->config;
    const pe_work_priority_item_t *item = &ctx->items[index];
    uint32_t top = config->buckets - 1u;
    uint32_t bucket = oracle_bucket_raw(config, oracle_score(config, item));
    uint64_t age, promotion;

    if (config->policy == PE_WORK_SCHED_FIFO)
        return bucket;
    if (oracle_below_floor(ctx, index))
        return top;
    if (config->aging_interval == 0u)
        return bucket;
    age = ctx->epoch > item->last_served ? ctx->epoch - item->last_served : 0u;
    promotion = age / config->aging_interval;
    if (promotion == 0u)
        return bucket;
    if (promotion >= (uint64_t)top)
        return top;
    bucket += (uint32_t)promotion;
    return bucket > top ? top : bucket;
}

/* The documented comparator, highest priority first: the coverage-floor
   tier, then the bucket, then the input position. */
static int oracle_rank_cmp(const oracle_ctx_t *ctx, size_t a, size_t b)
{
    int fa = oracle_below_floor(ctx, a);
    int fb = oracle_below_floor(ctx, b);
    uint32_t ba, bb;
    if (fa != fb)
        return fa > fb ? -1 : 1;
    ba = oracle_effective_bucket(ctx, a);
    bb = oracle_effective_bucket(ctx, b);
    if (ba != bb)
        return ba > bb ? -1 : 1;
    if (a != b)
        return a < b ? -1 : 1;
    return 0;
}

/* The unique permutation that is non-decreasing under oracle_rank_cmp, found
   by enumerating all n! of them. */
static int oracle_order_exhaustive(const oracle_ctx_t *ctx, size_t *out)
{
    size_t perm[ORACLE_MAX_ITEMS];
    size_t n = ctx->count;
    size_t i;
    if (n > ORACLE_MAX_ITEMS)
        return -1;
    for (i = 0u; i < n; ++i)
        perm[i] = i;
    for (;;)
    {
        int sorted = 1;
        for (i = 1u; i < n; ++i)
            if (oracle_rank_cmp(ctx, perm[i - 1u], perm[i]) > 0)
            {
                sorted = 0;
                break;
            }
        if (sorted)
        {
            for (i = 0u; i < n; ++i)
                out[i] = perm[i];
            return 0;
        }
        /* Lexicographic next permutation. */
        {
            size_t k = n, l, tmp;
            if (n < 2u)
                return -1;
            while (k > 1u && perm[k - 2u] >= perm[k - 1u])
                --k;
            if (k == 1u)
                return -1; /* every permutation tried */
            l = n;
            while (perm[k - 2u] >= perm[l - 1u])
                --l;
            tmp = perm[k - 2u];
            perm[k - 2u] = perm[l - 1u];
            perm[l - 1u] = tmp;
            {
                size_t lo = k - 1u, hi = n - 1u;
                while (lo < hi)
                {
                    tmp = perm[lo];
                    perm[lo] = perm[hi];
                    perm[hi] = tmp;
                    ++lo;
                    --hi;
                }
            }
        }
    }
}

/* BALANCED, built directly from the description: the coverage tier first in
   input order, then one item per non-empty bucket per round, walking down
   from the round's starting bucket and wrapping. The start is the top bucket
   on epoch 0 and one lower on each later epoch. */
static int oracle_balanced_order(const oracle_ctx_t *ctx, size_t *out)
{
    size_t depth[PE_WORK_PRIORITY_MAX_BUCKETS];
    size_t written = 0u;
    size_t round, i;
    uint32_t buckets = ctx->config.buckets;
    uint32_t start = buckets - 1u - (uint32_t)(ctx->epoch % (uint64_t)buckets);
    uint32_t b, k;

    for (b = 0u; b < buckets; ++b)
        depth[b] = 0u;
    for (i = 0u; i < ctx->count; ++i)
        if (!oracle_below_floor(ctx, i))
            depth[oracle_effective_bucket(ctx, i)]++;
    for (i = 0u; i < ctx->count; ++i)
        if (oracle_below_floor(ctx, i))
        {
            out[written] = i;
            written++;
        }
    for (round = 0u; round < ctx->count; ++round)
    {
        int any = 0;
        for (k = 0u; k < buckets; ++k)
        {
            size_t seen = 0u;
            b = (start + buckets - k) % buckets; /* the k-th bucket visited */
            if (round >= depth[b])
                continue;
            any = 1;
            for (i = 0u; i < ctx->count; ++i)
            {
                if (oracle_below_floor(ctx, i))
                    continue;
                if (oracle_effective_bucket(ctx, i) != b)
                    continue;
                if (seen == round)
                {
                    out[written] = i;
                    written++;
                    break;
                }
                seen++;
            }
        }
        if (!any)
            break;
    }
    return written == ctx->count ? 0 : -1;
}

/* ---------------------------------------------------------------- *
 * Fixture helpers
 * ---------------------------------------------------------------- */

static pe_work_priority_item_t oracle_item(double best, double second,
                                           double best_stderr,
                                           double second_stderr,
                                           uint64_t visits,
                                           uint64_t last_served,
                                           uint32_t actions)
{
    pe_work_priority_item_t item;
    item.best = best;
    item.second_best = second;
    item.best_stderr = best_stderr;
    item.second_stderr = second_stderr;
    item.visits = visits;
    item.last_served = last_served;
    item.actions = actions;
    return item;
}

#endif /* POKER_EVAL_TESTS_WORK_PRIORITY_ORACLE_H */
