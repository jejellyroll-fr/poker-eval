/*
 * strength_bucketing.c - EHS / EHS2 strength bucketing (FEAT-13)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Implement the public contract of strength_bucketing.h: per-hand (EHS, EHS2)
 * feature extraction, k-means clustering into strength buckets, a serializable
 * .pe_sbk table, and a memoizing assignment helper for the CFR hot path.
 *
 * Implementation notes:
 *  - Feature extraction rolls each hand out against the opponent range on the
 *    board (exhaustive for 2-card holes, deterministic sample for 4-card holes)
 *    and accumulates HS / HS^2 with the same win=1 / chop=0.5 / loss=0 scoring
 *    as hand_clustering, so the abstraction is consistent with the showdown
 *    utilities CFR backs up.
 *  - Clustering is k-means++ seeding + Lloyd iterations with an internal PCG32
 *    so a given seed always yields the same table (CI runs these under
 *    sanitizers and requires determinism).
 *  - Empty clusters are re-seeded onto the point furthest from its centroid,
 *    keeping the effective bucket count equal to k.
 *  - The table file format follows FEAT-09 compact storage: 8-byte magic,
 *    version word, little-endian fixed-width fields.
 */

#include <poker_eval/engine/solvers/cfr/strength_bucketing.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_SBK_MAGIC "PESBK001"
#define PE_SBK_VERSION 1u

/* Memoization table sizing for pe_strength_table_assign_cached(). */
#define PE_SBK_CACHE_MIN_CAP 256u

/* ------------------------------------------------------------------ *
 * Deterministic RNG (PCG32)
 * ------------------------------------------------------------------ */

typedef struct
{
    uint64_t state;
    uint64_t inc;
} pe_sbk_pcg32_t;

static void pe_sbk_pcg32_seed(pe_sbk_pcg32_t *rng, uint64_t seed)
{
    rng->state = 0u;
    rng->inc = (seed << 1u) | 1u;
    rng->state = rng->state * 6364136223846793005ULL + rng->inc;
    rng->state += seed;
    rng->state = rng->state * 6364136223846793005ULL + rng->inc;
}

static uint32_t pe_sbk_pcg32_next(pe_sbk_pcg32_t *rng)
{
    uint64_t old = rng->state;
    rng->state = old * 636413622 + rng->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

static double pe_sbk_pcg32_double(pe_sbk_pcg32_t *rng)
{
    uint32_t r = pe_sbk_pcg32_next(rng);
    return (double)r / 4294967296.0;
}

/* ------------------------------------------------------------------ *
 * Options normalization
 * ------------------------------------------------------------------ */

typedef struct
{
    int n_buckets;
    int hole_cards;
    uint32_t max_samples;
    uint32_t opp_samples;
    uint32_t seed;
    int max_iterations;
} pe_sbk_opts_t;

static void pe_sbk_opts_normalize(const pe_strength_cluster_opts_t *opts,
                                  pe_sbk_opts_t *o)
{
    if (opts)
    {
        o->n_buckets = opts->n_buckets > 0 ? opts->n_buckets : PE_SBK_DEFAULT_BUCKETS;
        o->hole_cards = opts->hole_cards > 0 ? opts->hole_cards : 2;
        o->max_samples = opts->max_samples > 0 ? opts->max_samples : PE_SBK_DEFAULT_MAX_SAMPLES;
        o->opp_samples = opts->opp_samples > 0 ? opts->opp_samples : PE_SBK_DEFAULT_OPP_SAMPLES;
        o->seed = opts->seed;
        o->max_iterations = opts->max_iterations > 0 ? opts->max_iterations : PE_SBK_DEFAULT_ITERATIONS;
    }
    else
    {
        o->n_buckets = PE_SBK_DEFAULT_BUCKETS;
        o->hole_cards = 2;
        o->max_samples = PE_SBK_DEFAULT_MAX_SAMPLES;
        o->opp_samples = PE_SBK_DEFAULT_OPP_SAMPLES;
        o->seed = 0u;
        o->max_iterations = PE_SBK_DEFAULT_ITERATIONS;
    }
    if (o->n_buckets > PE_SBK_MAX_BUCKETS)
        o->n_buckets = PE_SBK_MAX_BUCKETS;
    if (o->opp_samples > PE_SBK_MAX_OPP_SAMPLES)
        o->opp_samples = PE_SBK_MAX_OPP_SAMPLES;
    if (o->hole_cards != 4)
        o->hole_cards = 2;
}

/* ------------------------------------------------------------------ *
 * Card helpers / scoring (mirrors hand_clustering)
 * ------------------------------------------------------------------ */

static int pe_sbk_collect_free(mask_t used, int *out, int max_out)
{
    int n = 0;
    for (int card = 0; card < MODERN_DECK_SIZE && n < max_out; ++card)
        if (!mask_is_set(used, card))
            out[n++] = card;
    return n;
}

static eval_t pe_sbk_score(const EvalContext *ctx, mask_t hole, mask_t board)
{
    mask_t all = hole | board;
    int n = mask_popcount(all);
    if (n == 5)
        return pe_eval_5c(ctx, all);
    if (n == 7)
        return pe_eval_7c(ctx, all);
    return pe_eval_nc(ctx, all);
}

/* ------------------------------------------------------------------ *
 * Feature extraction
 * ------------------------------------------------------------------ */

int pe_strength_features(const EvalContext *ctx,
                         mask_t hole,
                         mask_t board,
                         const pe_strength_cluster_opts_t *opts,
                         pe_strength_features_t *out)
{
    if (!ctx || !out)
        return -1;

    pe_sbk_opts_t o;
    pe_sbk_opts_normalize(opts, &o);

    int n_board = mask_popcount(board);
    int n_hole = mask_popcount(hole);
    if (n_board < 3 || n_board > 5 || n_hole < 2 || n_hole > 4)
        return -1;
    if ((hole & board) != MASK_EMPTY)
        return -1;

    int free_cards[MODERN_DECK_SIZE];
    int n_free = pe_sbk_collect_free(hole | board, free_cards, MODERN_DECK_SIZE);
    if (n_free < n_hole)
        return -1;

    memset(out, 0, sizeof(*out));

    eval_t hero = pe_sbk_score(ctx, hole, board);
    if (hero == EVAL_INVALID)
        return -1;

    double sum = 0.0, sum_sq = 0.0;
    uint32_t samples = 0u;

    if (n_hole == 2)
    {
        for (int i = 0; i < n_free; ++i)
        {
            for (int j = i + 1; j < n_free; ++j)
            {
                mask_t opp = mask_set(mask_set(MASK_EMPTY, free_cards[i]), free_cards[j]);
                eval_t v = pe_sbk_score(ctx, opp, board);
                double s = (hero > v) ? 1.0 : ((hero == v) ? 0.5 : 0.0);
                sum += s;
                sum_sq += s * s;
                ++samples;
            }
        }
    }
    else
    {
        pe_sbk_pcg32_t rng;
        pe_sbk_pcg32_seed(&rng, (uint64_t)o.seed * 0x9E3779B97F4A7C15ULL +
                                     (uint64_t)hole + (uint64_t)board);
        uint32_t target = o.opp_samples;
        uint32_t attempts = 0u;
        uint32_t max_attempts = target * 4u + 64u;
        while (samples < target && attempts < max_attempts)
        {
            ++attempts;
            mask_t opp = MASK_EMPTY;
            int ok = 1;
            for (int c = 0; c < n_hole; ++c)
            {
                int idx = (int)(pe_sbk_pcg32_double(&rng) * (double)n_free);
                int card = free_cards[idx];
                if (mask_is_set(opp, card))
                {
                    ok = 0;
                    break;
                }
                opp = mask_set(opp, card);
            }
            if (!ok)
                continue;
            eval_t v = pe_sbk_score(ctx, opp, board);
            double s = (hero > v) ? 1.0 : ((hero == v) ? 0.5 : 0.0);
            sum += s;
            sum_sq += s * s;
            ++samples;
        }
    }

    if (samples == 0u)
        return -1;

    out->samples = samples;
    out->ehs = sum / (double)samples;
    out->ehs2 = sum_sq / (double)samples;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Table layout
 * ------------------------------------------------------------------ */

typedef struct
{
    uint64_t key;  /* (hole, board) fingerprint; 0 means empty */
    int bucket;
} pe_sbk_cache_entry_t;

struct pe_strength_table_t
{
    int k;              /* bucket count */
    int hole_cards;     /* 2 or 4 */
    uint32_t seed;
    uint32_t max_samples;
    uint32_t opp_samples;
    int max_iterations;
    mask_t board;

    double *ehs;        /* k centroids' ehs  */
    double *ehs2;       /* k centroids' ehs2 */
    double *bucket_ehs; /* k mean ehs of assigned training hands */
    double *bucket_ehs2;
    uint32_t *bucket_size;

    pe_sbk_cache_entry_t *cache;
    size_t cache_cap;
    size_t cache_used;
};

static double pe_sbk_dist2(double ehs, double ehs2, const pe_strength_table_t *t, int c)
{
    double de = ehs - t->ehs[c];
    double de2 = ehs2 - t->ehs2[c];
    return de * de + de2 * de2;
}

static int pe_sbk_nearest(double ehs, double ehs2, const pe_strength_table_t *t)
{
    int best = 0;
    double best_d = pe_sbk_dist2(ehs, ehs2, t, 0);
    for (int c = 1; c < t->k; ++c)
    {
        double d = pe_sbk_dist2(ehs, ehs2, t, c);
        if (d < best_d)
        {
            best_d = d;
            best = c;
        }
    }
    return best;
}

/* k-means++ seeding over the given points. */
static void pe_sbk_seed(const double *ehs, const double *ehs2, int n,
                        pe_strength_table_t *t, pe_sbk_pcg32_t *rng)
{
    /* First centroid: deterministic (first point). */
    t->ehs[0] = ehs[0];
    t->ehs2[0] = ehs2[0];
    double *d2 = (double *)calloc((size_t)n, sizeof(double));
    if (!d2)
        return;
    for (int c = 1; c < t->k; ++c)
    {
        double total = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double best = pe_sbk_dist2(ehs[i], ehs2[i], t, c);
            for (int cc = 0; cc < c; ++cc)
            {
                double d = pe_sbk_dist2(ehs[i], ehs2[i], t, cc);
                if (d < best)
                    best = d;
            }
            d2[i] = best;
            total += best;
        }
        double r = pe_sbk_pcg32_double(rng) * total;
        int chosen = n - 1;
        for (int i = 0; i < n; ++i)
        {
            r -= d2[i];
            if (r <= 0.0)
            {
                chosen = i;
                break;
            }
        }
        t->ehs[c] = ehs[chosen];
        t->ehs2[c] = ehs2[chosen];
    }
    free(d2);
}

static pe_strength_table_t *pe_sbk_cluster(const EvalContext *ctx,
                                           mask_t board,
                                           const double *ehs,
                                           const double *ehs2,
                                           int n,
                                           const pe_sbk_opts_t *o)
{
    if (n <= 0)
        return NULL;
    int k = o->n_buckets;
    if (k > n)
        k = n;
    if (k < 1)
        k = 1;

    pe_strength_table_t *t = (pe_strength_table_t *)calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->k = k;
    t->hole_cards = o->hole_cards;
    t->seed = o->seed;
    t->max_samples = o->max_samples;
    t->opp_samples = o->opp_samples;
    t->max_iterations = o->max_iterations;
    t->board = board;
    t->ehs = (double *)calloc((size_t)k, sizeof(double));
    t->ehs2 = (double *)calloc((size_t)k, sizeof(double));
    t->bucket_ehs = (double *)calloc((size_t)k, sizeof(double));
    t->bucket_ehs2 = (double *)calloc((size_t)k, sizeof(double));
    t->bucket_size = (uint32_t *)calloc((size_t)k, sizeof(uint32_t));
    if (!t->ehs || !t->ehs2 || !t->bucket_ehs || !t->bucket_ehs2 || !t->bucket_size)
    {
        pe_strength_table_free(t);
        return NULL;
    }

    pe_sbk_pcg32_t rng;
    pe_sbk_pcg32_seed(&rng, (uint64_t)o->seed * 0x85EBCA77C2B9AE3DULL + (uint64_t)board + 1u);
    pe_sbk_seed(ehs, ehs2, n, t, &rng);

    int *assign = (int *)calloc((size_t)n, sizeof(int));
    if (!assign)
    {
        pe_strength_table_free(t);
        return NULL;
    }

    for (int iter = 0; iter < o->max_iterations; ++iter)
    {
        int moved = 0;
        for (int i = 0; i < n; ++i)
        {
            int c = pe_sbk_nearest(ehs[i], ehs2[i], t);
            if (assign[i] != c)
            {
                assign[i] = c;
                ++moved;
            }
        }
        /* Recompute centroids. */
        double *se = (double *)calloc((size_t)k, sizeof(double));
        double *se2 = (double *)calloc((size_t)k, sizeof(double));
        uint32_t *cnt = (uint32_t *)calloc((size_t)k, sizeof(uint32_t));
        if (!se || !se2 || !cnt)
        {
            free(se); free(se2); free(cnt);
            free(assign);
            pe_strength_table_free(t);
            return NULL;
        }
        for (int i = 0; i < n; ++i)
        {
            int c = assign[i];
            se[c] += ehs[i];
            se2[c] += ehs2[i];
            cnt[c]++;
        }
        for (int c = 0; c < k; ++c)
        {
            if (cnt[c] > 0)
            {
                t->ehs[c] = se[c] / (double)cnt[c];
                t->ehs2[c] = se2[c] / (double)cnt[c];
            }
            else
            {
                /* Empty cluster: reseed onto the point furthest from its centroid. */
                int far = 0;
                double fd = -1.0;
                for (int i = 0; i < n; ++i)
                {
                    double d = pe_sbk_dist2(ehs[i], ehs2[i], t, assign[i]);
                    if (d > fd)
                    {
                        fd = d;
                        far = i;
                    }
                }
                t->ehs[c] = ehs[far];
                t->ehs2[c] = ehs2[far];
            }
        }
        free(se); free(se2); free(cnt);
        if (moved == 0 && iter > 0)
            break;
    }

    /* Final assignment + per-bucket aggregates (sorted by ascending ehs). */
    for (int i = 0; i < n; ++i)
        assign[i] = pe_sbk_nearest(ehs[i], ehs2[i], t);
    double *be = (double *)calloc((size_t)k, sizeof(double));
    double *be2 = (double *)calloc((size_t)k, sizeof(double));
    uint32_t *bs = (uint32_t *)calloc((size_t)k, sizeof(uint32_t));
    for (int i = 0; i < n; ++i)
    {
        int c = assign[i];
        be[c] += ehs[i];
        be2[c] += ehs2[i];
        bs[c]++;
    }
    /* Stable ordering: assign a temp bucket order by ascending ehs, then remap. */
    int *order = (int *)calloc((size_t)k, sizeof(int));
    for (int c = 0; c < k; ++c)
        order[c] = c;
    for (int a = 0; a + 1 < k; ++a)
        for (int b = a + 1; b < k; ++b)
            if (t->ehs[order[b]] < t->ehs[order[a]])
            {
                int tmp = order[a];
                order[a] = order[b];
                order[b] = tmp;
            }
    /* Build remapped centroids. */
    double *re = (double *)calloc((size_t)k, sizeof(double));
    double *re2 = (double *)calloc((size_t)k, sizeof(double));
    double *rbe = (double *)calloc((size_t)k, sizeof(double));
    double *rbe2 = (double *)calloc((size_t)k, sizeof(double));
    uint32_t *rbs = (uint32_t *)calloc((size_t)k, sizeof(uint32_t));
    for (int c = 0; c < k; ++c)
    {
        int src = order[c];
        re[c] = t->ehs[src];
        re2[c] = t->ehs2[src];
        rbe[c] = bs[src] > 0 ? be[src] / (double)bs[src] : 0.0;
        rbe2[c] = bs[src] > 0 ? be2[src] / (double)bs[src] : 0.0;
        rbs[c] = bs[src];
    }
    memcpy(t->ehs, re, (size_t)k * sizeof(double));
    memcpy(t->ehs2, re2, (size_t)k * sizeof(double));
    memcpy(t->bucket_ehs, rbe, (size_t)k * sizeof(double));
    memcpy(t->bucket_ehs2, rbe2, (size_t)k * sizeof(double));
    memcpy(t->bucket_size, rbs, (size_t)k * sizeof(uint32_t));

    free(be); free(be2); free(bs); free(order);
    free(re); free(re2); free(rbe); free(rbe2); free(rbs);
    free(assign);
    return t;
}

/* ------------------------------------------------------------------ *
 * Training
 * ------------------------------------------------------------------ */

pe_strength_table_t *pe_strength_table_train(const EvalContext *ctx,
                                             mask_t board,
                                             const mask_t *hands,
                                             size_t n_hands,
                                             const pe_strength_cluster_opts_t *opts,
                                             int *k_out)
{
    if (!ctx || !hands || n_hands == 0)
        return NULL;

    pe_sbk_opts_t o;
    pe_sbk_opts_normalize(opts, &o);

    double *ehs = (double *)calloc(n_hands, sizeof(double));
    double *ehs2 = (double *)calloc(n_hands, sizeof(double));
    if (!ehs || !ehs2)
    {
        free(ehs); free(ehs2);
        return NULL;
    }

    size_t kept = 0;
    for (size_t i = 0; i < n_hands; ++i)
    {
        pe_strength_features_t f;
        if (pe_strength_features(ctx, hands[i], board, opts, &f) == 0)
        {
            ehs[kept] = f.ehs;
            ehs2[kept] = f.ehs2;
            ++kept;
        }
    }
    if (kept == 0)
    {
        free(ehs); free(ehs2);
        return NULL;
    }

    pe_strength_table_t *t = pe_sbk_cluster(ctx, board, ehs, ehs2, (int)kept, &o);
    free(ehs); free(ehs2);
    if (t && k_out)
        *k_out = t->k;
    return t;
}

pe_strength_table_t *pe_strength_table_train_all(const EvalContext *ctx,
                                                 mask_t board,
                                                 const pe_strength_cluster_opts_t *opts,
                                                 int *k_out)
{
    if (!ctx)
        return NULL;

    pe_sbk_opts_t o;
    pe_sbk_opts_normalize(opts, &o);

    int n_board = mask_popcount(board);
    if (n_board < 3 || n_board > 5)
        return NULL;

    int free_cards[MODERN_DECK_SIZE];
    int n_free = pe_sbk_collect_free(board, free_cards, MODERN_DECK_SIZE);
    int hc = o.hole_cards;

    /* Count combinations up front (cheap) to size buffers. */
    unsigned long long total = 1;
    int denom = 1;
    for (int i = 0; i < hc; ++i)
    {
        total *= (unsigned long long)(n_free - i);
        denom *= (i + 1);
    }
    unsigned long long combos = total / (unsigned long long)denom;
    if (combos == 0)
        return NULL;

    size_t want = (size_t)combos;
    if (hc == 4)
    {
        size_t cap = (size_t)o.max_samples;
        if (cap == 0)
            cap = (size_t)PE_SBK_DEFAULT_MAX_SAMPLES;
        if (want > cap)
            want = cap;
    }

    mask_t *hands = (mask_t *)calloc(want, sizeof(mask_t));
    double *ehs = (double *)calloc(want, sizeof(double));
    double *ehs2 = (double *)calloc(want, sizeof(double));
    if (!hands || !ehs || !ehs2)
    {
        free(hands); free(ehs); free(ehs2);
        return NULL;
    }

    /* Enumerate C(n_free, hc) deterministically; stop at `want`. */
    pe_sbk_pcg32_t rng;
    pe_sbk_pcg32_seed(&rng, (uint64_t)o.seed * 0x27D4EB2F165667C5ULL + (uint64_t)board + 7u);
    int idx[4];
    size_t produced = 0;
    /* Lexicographic combination generation. */
    for (int i = 0; i < hc; ++i)
        idx[i] = i;
    int done = (n_free < hc) ? 1 : 0;
    while (!done && produced < want)
    {
        mask_t h = MASK_EMPTY;
        for (int i = 0; i < hc; ++i)
            h = mask_set(h, free_cards[idx[i]]);
        pe_strength_features_t f;
        if (pe_strength_features(ctx, h, board, opts, &f) == 0)
        {
            hands[produced] = h;
            ehs[produced] = f.ehs;
            ehs2[produced] = f.ehs2;
            ++produced;
        }
        /* advance to next combination */
        int p = hc - 1;
        while (p >= 0 && idx[p] == n_free - hc + p)
            --p;
        if (p < 0)
        {
            done = 1;
        }
        else
        {
            idx[p]++;
            for (int i = p + 1; i < hc; ++i)
                idx[i] = idx[i - 1] + 1;
        }
    }

    pe_strength_table_t *t = NULL;
    if (produced > 0)
        t = pe_sbk_cluster(ctx, board, ehs, ehs2, (int)produced, &o);

    if (t && k_out)
        *k_out = t->k;

    free(hands); free(ehs); free(ehs2);
    return t;
}

void pe_strength_table_free(pe_strength_table_t *table)
{
    if (!table)
        return;
    free(table->ehs);
    free(table->ehs2);
    free(table->bucket_ehs);
    free(table->bucket_ehs2);
    free(table->bucket_size);
    free(table->cache);
    free(table);
}

int pe_strength_table_count(const pe_strength_table_t *table)
{
    return table ? table->k : 0;
}

mask_t pe_strength_table_board(const pe_strength_table_t *table)
{
    return table ? table->board : MASK_EMPTY;
}

double pe_strength_table_bucket_ehs(const pe_strength_table_t *table, int bucket)
{
    if (!table || bucket < 0 || bucket >= table->k)
        return -1.0;
    return table->bucket_ehs[bucket];
}

double pe_strength_table_bucket_ehs2(const pe_strength_table_t *table, int bucket)
{
    if (!table || bucket < 0 || bucket >= table->k)
        return -1.0;
    return table->bucket_ehs2[bucket];
}

int pe_strength_table_bucket_size(const pe_strength_table_t *table, int bucket)
{
    if (!table || bucket < 0 || bucket >= table->k)
        return -1;
    return (int)table->bucket_size[bucket];
}

/* ------------------------------------------------------------------ *
 * Assignment
 * ------------------------------------------------------------------ */

int pe_strength_table_assign_features(const pe_strength_table_t *table,
                                      const pe_strength_features_t *features)
{
    if (!table || !features)
        return -1;
    return pe_sbk_nearest(features->ehs, features->ehs2, table);
}

int pe_strength_table_assign(const pe_strength_table_t *table,
                             const EvalContext *ctx,
                             mask_t hole,
                             mask_t board)
{
    if (!table || !ctx)
        return -1;
    pe_strength_cluster_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.n_buckets = table->k;
    opts.hole_cards = table->hole_cards;
    opts.seed = table->seed;
    opts.max_samples = table->max_samples;
    opts.opp_samples = table->opp_samples;
    opts.max_iterations = table->max_iterations;

    pe_strength_features_t f;
    if (pe_strength_features(ctx, hole, board, &opts, &f) != 0)
        return -1;
    return pe_sbk_nearest(f.ehs, f.ehs2, table);
}

static uint64_t pe_sbk_cache_key(mask_t hole, mask_t board)
{
    /* board occupies low bits, hole the high bits; collisions are tolerated
     * because the cache only accelerates repeated identical lookups. */
    return ((uint64_t)hole << 24) ^ (uint64_t)board;
}

int pe_strength_table_assign_cached(pe_strength_table_t *table,
                                    const EvalContext *ctx,
                                    mask_t hole,
                                    mask_t board)
{
    if (!table || !ctx)
        return -1;

    uint64_t key = pe_sbk_cache_key(hole, board);
    if (table->cache)
    {
        size_t mask = table->cache_cap - 1;
        size_t h = (size_t)(key & (uint64_t)mask);
        for (size_t probe = 0; probe < table->cache_cap; ++probe)
        {
            size_t idx = (h + probe) & mask;
            if (table->cache[idx].key == 0)
                break;
            if (table->cache[idx].key == key)
                return table->cache[idx].bucket;
        }
    }

    int bucket = pe_strength_table_assign(table, ctx, hole, board);
    if (bucket < 0)
        return bucket;

    if (table->cache_cap == 0)
    {
        table->cache_cap = PE_SBK_CACHE_MIN_CAP;
        table->cache = (pe_sbk_cache_entry_t *)calloc(table->cache_cap, sizeof(pe_sbk_cache_entry_t));
    }
    if (table->cache && table->cache_cap > 0)
    {
        size_t mask = table->cache_cap - 1;
        size_t h = (size_t)(key & (uint64_t)mask);
        for (size_t probe = 0; probe < table->cache_cap; ++probe)
        {
            size_t idx = (h + probe) & mask;
            if (table->cache[idx].key == 0 || table->cache[idx].key == key)
            {
                table->cache[idx].key = key;
                table->cache[idx].bucket = bucket;
                break;
            }
        }
    }
    return bucket;
}

/* ------------------------------------------------------------------ *
 * Serialization (.pe_sbk)
 * ------------------------------------------------------------------ */

static int pe_sbk_write_u32(FILE *f, uint32_t v)
{
    uint32_t le = v;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    le = ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
#endif
    return fwrite(&le, sizeof(le), 1, f) == 1 ? 0 : -1;
}

static int pe_sbk_write_u64(FILE *f, uint64_t v)
{
    uint32_t lo = (uint32_t)v;
    uint32_t hi = (uint32_t)(v >> 32);
    if (pe_sbk_write_u32(f, lo) != 0 || pe_sbk_write_u32(f, hi) != 0)
        return -1;
    return 0;
}

static int pe_sbk_write_double(FILE *f, double d)
{
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return pe_sbk_write_u64(f, bits);
}

int pe_strength_table_save(const pe_strength_table_t *table, const char *path)
{
    if (!table || !path)
    {
        errno = EINVAL;
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    if (fwrite(PE_SBK_MAGIC, 1, 8, f) != 8 ||
        pe_sbk_write_u32(f, PE_SBK_VERSION) != 0 ||
        pe_sbk_write_u32(f, (uint32_t)table->k) != 0 ||
        pe_sbk_write_u32(f, (uint32_t)table->hole_cards) != 0 ||
        pe_sbk_write_u32(f, table->seed) != 0 ||
        pe_sbk_write_u32(f, table->max_samples) != 0 ||
        pe_sbk_write_u32(f, table->opp_samples) != 0 ||
        pe_sbk_write_u32(f, (uint32_t)table->max_iterations) != 0 ||
        pe_sbk_write_u64(f, (uint64_t)table->board) != 0)
    {
        fclose(f);
        return -1;
    }
    for (int c = 0; c < table->k; ++c)
    {
        if (pe_sbk_write_double(f, table->ehs[c]) != 0 ||
            pe_sbk_write_double(f, table->ehs2[c]) != 0 ||
            pe_sbk_write_double(f, table->bucket_ehs[c]) != 0 ||
            pe_sbk_write_double(f, table->bucket_ehs2[c]) != 0 ||
            pe_sbk_write_u32(f, table->bucket_size[c]) != 0)
        {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

pe_strength_table_t *pe_strength_table_load(const char *path)
{
    if (!path)
    {
        errno = EINVAL;
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, PE_SBK_MAGIC, 8) != 0)
    {
        fclose(f);
        errno = EINVAL;
        return NULL;
    }

    uint32_t version = 0, k = 0, hole_cards = 0, seed = 0, max_samples = 0,
             opp_samples = 0, max_iterations = 0;
    uint64_t board = 0;
    if (fread(&version, sizeof(version), 1, f) != 1 || version != PE_SBK_VERSION ||
        fread(&k, sizeof(k), 1, f) != 1 || k == 0 || k > PE_SBK_MAX_BUCKETS ||
        fread(&hole_cards, sizeof(hole_cards), 1, f) != 1 ||
        fread(&seed, sizeof(seed), 1, f) != 1 ||
        fread(&max_samples, sizeof(max_samples), 1, f) != 1 ||
        fread(&opp_samples, sizeof(opp_samples), 1, f) != 1 ||
        fread(&max_iterations, sizeof(max_iterations), 1, f) != 1 ||
        fread(&board, sizeof(board), 1, f) != 1)
    {
        fclose(f);
        errno = EINVAL;
        return NULL;
    }

    pe_strength_table_t *t = (pe_strength_table_t *)calloc(1, sizeof(*t));
    if (!t)
    {
        fclose(f);
        return NULL;
    }
    t->k = (int)k;
    t->hole_cards = (int)hole_cards;
    t->seed = seed;
    t->max_samples = max_samples;
    t->opp_samples = opp_samples;
    t->max_iterations = (int)max_iterations;
    t->board = (mask_t)board;
    t->ehs = (double *)calloc((size_t)k, sizeof(double));
    t->ehs2 = (double *)calloc((size_t)k, sizeof(double));
    t->bucket_ehs = (double *)calloc((size_t)k, sizeof(double));
    t->bucket_ehs2 = (double *)calloc((size_t)k, sizeof(double));
    t->bucket_size = (uint32_t *)calloc((size_t)k, sizeof(uint32_t));
    if (!t->ehs || !t->ehs2 || !t->bucket_ehs || !t->bucket_ehs2 || !t->bucket_size)
    {
        pe_strength_table_free(t);
        fclose(f);
        return NULL;
    }

    int ok = 1;
    for (int c = 0; c < t->k && ok; ++c)
    {
        uint64_t bits;
        if (fread(&bits, sizeof(bits), 1, f) != 1) { ok = 0; break; }
        memcpy(&t->ehs[c], &bits, sizeof(double));
        if (fread(&bits, sizeof(bits), 1, f) != 1) { ok = 0; break; }
        memcpy(&t->ehs2[c], &bits, sizeof(double));
        if (fread(&bits, sizeof(bits), 1, f) != 1) { ok = 0; break; }
        memcpy(&t->bucket_ehs[c], &bits, sizeof(double));
        if (fread(&bits, sizeof(bits), 1, f) != 1) { ok = 0; break; }
        memcpy(&t->bucket_ehs2[c], &bits, sizeof(double));
        uint32_t bs = 0;
        if (fread(&bs, sizeof(bs), 1, f) != 1) { ok = 0; break; }
        t->bucket_size[c] = bs;
    }
    fclose(f);
    if (!ok)
    {
        pe_strength_table_free(t);
        errno = EINVAL;
        return NULL;
    }
    return t;
}
