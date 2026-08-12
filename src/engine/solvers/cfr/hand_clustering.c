/*
 * hand_clustering.c - Learned hand abstraction via k-means clustering (FEAT-04)
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * See include/poker_eval/engine/solvers/cfr/hand_clustering.h for the rationale
 * and the public contract. Implementation notes:
 *
 *  - Feature extraction rolls the hand out against every legal opponent hand on
 *    the board (or a deterministic sample of them when the combination count is
 *    large). Per-matchup hand strength is 1.0 for a win, 0.5 for a chop and 0.0
 *    for a loss; E[HS^2] and the strength histogram are accumulated from that.
 *  - Clustering is k-means++ seeding followed by Lloyd iterations, with an
 *    internal PCG32 so that a given seed always yields the same table (the CI
 *    runs these paths under sanitizers and requires determinism).
 *  - Empty clusters are re-seeded onto the point furthest from its centroid,
 *    which keeps the effective cluster count equal to k.
 */

#include <poker_eval/engine/solvers/cfr/hand_clustering.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_BKT_MAGIC "PEBKT001"
#define PE_BKT_VERSION 1u

#define PE_HS_DEFAULT_ITERATIONS 100

/* Memoization table sizing for pe_bucket_table_assign_cached(). */
#define PE_BKT_CACHE_MIN_CAP 256u

/* ------------------------------------------------------------------ *
 * Deterministic RNG (PCG32)
 * ------------------------------------------------------------------ */

typedef struct
{
    uint64_t state;
    uint64_t inc;
} pe_pcg32_t;

static void pe_pcg32_seed(pe_pcg32_t *rng, uint64_t seed)
{
    rng->state = 0u;
    rng->inc = (seed << 1u) | 1u;
    /* Two advances mix the seed into the state (standard PCG init). */
    rng->state = rng->state * 6364136223846793005ULL + rng->inc;
    rng->state += seed;
    rng->state = rng->state * 6364136223846793005ULL + rng->inc;
}

static uint32_t pe_pcg32_next(pe_pcg32_t *rng)
{
    uint64_t old = rng->state;
    rng->state = old * 6364136223846793005ULL + rng->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

/* Unbiased bounded draw in [0, bound). */
static uint32_t pe_pcg32_bounded(pe_pcg32_t *rng, uint32_t bound)
{
    if (bound == 0u)
        return 0u;
    uint32_t threshold = (uint32_t)(-(int32_t)bound) % bound;
    for (;;)
    {
        uint32_t r = pe_pcg32_next(rng);
        if (r >= threshold)
            return r % bound;
    }
}

static double pe_pcg32_double(pe_pcg32_t *rng)
{
    uint32_t r = pe_pcg32_next(rng);
    return (double)r / 4294967296.0;
}

/* ------------------------------------------------------------------ *
 * Internal table layout
 * ------------------------------------------------------------------ */

typedef struct
{
    uint64_t key; /* (hole, board) fingerprint; 0 means empty */
    int bucket;
} pe_bkt_cache_entry_t;

struct pe_bucket_table_t
{
    int k;           /* cluster count */
    int n_features;  /* 1 + n_bins */
    int n_bins;      /* histogram bins */
    int hole_cards;  /* 2 or 4 */
    uint32_t seed;   /* training seed (kept for reproducibility/debug) */
    uint32_t max_samples; /* hero-hand budget used at training time */
    uint32_t opp_samples; /* per-hero opponent rollout used at training time */
    double hist_weight;
    mask_t board;

    double *centroids;      /* k * n_features */
    double *cluster_equity; /* k */
    uint32_t *cluster_size; /* k */

    /* Lookup memoization (not serialized). */
    pe_bkt_cache_entry_t *cache;
    size_t cache_cap;
    size_t cache_used;
};

/* ------------------------------------------------------------------ *
 * Options normalization
 * ------------------------------------------------------------------ */

static void pe_opts_normalize(const pe_hand_cluster_opts_t *in, pe_hand_cluster_opts_t *out)
{
    if (in)
        *out = *in;
    else
        memset(out, 0, sizeof(*out));

    if (out->n_bins <= 0)
        out->n_bins = PE_HS_DEFAULT_BINS;
    if (out->n_bins > PE_HS_MAX_BINS)
        out->n_bins = PE_HS_MAX_BINS;
    if (out->hole_cards <= 0)
        out->hole_cards = 2;
    if (out->hole_cards > 4)
        out->hole_cards = 4;
    if (out->max_samples == 0u)
        out->max_samples = PE_HS_DEFAULT_MAX_SAMPLES;
    /* Per-hero opponent rollout budget for Omaha. Default to a small fixed
     * count so it does not scale with the (possibly large) hero-hand budget.
     * It is clamped to at least 1 and to the exact opponent count when smaller. */
    if (out->opp_samples == 0u)
        out->opp_samples = PE_HS_DEFAULT_OPP_SAMPLES;
    if (out->opp_samples > PE_HS_MAX_OPP_SAMPLES)
        out->opp_samples = PE_HS_MAX_OPP_SAMPLES;
    if (out->max_iterations <= 0)
        out->max_iterations = PE_HS_DEFAULT_ITERATIONS;
    if (!(out->hist_weight > 0.0))
        out->hist_weight = 1.0;
}

/* ------------------------------------------------------------------ *
 * Feature extraction
 * ------------------------------------------------------------------ */

static int pe_collect_free_cards(mask_t used, int *out, int max_out)
{
    int n = 0;
    for (int card = 0; card < MODERN_DECK_SIZE && n < max_out; ++card)
    {
        if (!mask_is_set(used, card))
            out[n++] = card;
    }
    return n;
}

/* Evaluate a hand of `hole_cards` hole cards on `board`.
 *
 * Hold'em style (2 hole cards) and Omaha style (4 hole cards) both reduce to a
 * best-of-all-cards evaluation here: pe_eval_nc picks the strongest 5-card
 * subset, which matches how the river adapters score showdowns
 * (eval_omaha_best is itself pe_eval_7c over hole|board). Keeping the same
 * scoring function as the adapters is what makes the abstraction consistent
 * with the utilities CFR actually backs up. */
static eval_t pe_score_hand(const EvalContext *ctx, mask_t hole, mask_t board)
{
    mask_t all = hole | board;
    int n = mask_popcount(all);
    if (n == 5)
        return pe_eval_5c(ctx, all);
    if (n == 7)
        return pe_eval_7c(ctx, all);
    return pe_eval_nc(ctx, all);
}

/* Accumulate one matchup outcome into the feature accumulators. */
static void pe_accumulate(double strength,
                          int n_bins,
                          double *sum,
                          double *sum_sq,
                          double *hist)
{
    *sum += strength;
    *sum_sq += strength * strength;
    int bin = (int)(strength * (double)n_bins);
    if (bin < 0)
        bin = 0;
    if (bin >= n_bins)
        bin = n_bins - 1;
    hist[bin] += 1.0;
}

int pe_hand_features(const EvalContext *ctx,
                     mask_t hole,
                     mask_t board,
                     const pe_hand_cluster_opts_t *opts,
                     pe_hand_features_t *out)
{
    if (!ctx || !out)
        return -1;

    pe_hand_cluster_opts_t o;
    pe_opts_normalize(opts, &o);

    int n_board = mask_popcount(board);
    int n_hole = mask_popcount(hole);
    if (n_board < 3 || n_board > 5 || n_hole < 2 || n_hole > 4)
        return -1;
    if ((hole & board) != MASK_EMPTY)
        return -1;

    int free_cards[MODERN_DECK_SIZE];
    int n_free = pe_collect_free_cards(hole | board, free_cards, MODERN_DECK_SIZE);
    if (n_free < n_hole)
        return -1;

    memset(out, 0, sizeof(*out));
    out->n_bins = o.n_bins;

    eval_t hero = pe_score_hand(ctx, hole, board);
    if (hero == EVAL_INVALID)
        return -1;

    double sum = 0.0;
    double sum_sq = 0.0;
    double hist[PE_HS_MAX_BINS];
    memset(hist, 0, sizeof(hist));
    uint32_t samples = 0u;

    if (n_hole == 2)
    {
        /* Exhaustive: C(n_free, 2) <= C(47,2) = 1081 matchups. */
        for (int i = 0; i < n_free; ++i)
        {
            for (int j = i + 1; j < n_free; ++j)
            {
                mask_t opp = mask_set(mask_set(MASK_EMPTY, free_cards[i]), free_cards[j]);
                eval_t v = pe_score_hand(ctx, opp, board);
                double strength = (hero > v) ? 1.0 : ((hero == v) ? 0.5 : 0.0);
                pe_accumulate(strength, o.n_bins, &sum, &sum_sq, hist);
                ++samples;
            }
        }
    }
    else
    {
        /* 4-card hole games: C(46,4) = 163185 matchups is too many for a hot
         * path, so draw a deterministic sample of distinct opponent hands. The
         * per-hero opponent budget (opp_samples) is deliberately independent of
         * the hero-hand budget (max_samples) to avoid O(n_hero * n_opp) cost. */
        pe_pcg32_t rng;
        pe_pcg32_seed(&rng, (uint64_t)o.seed * 0x9E3779B97F4A7C15ULL + (uint64_t)hole + (uint64_t)board);
        uint32_t target = o.opp_samples;
        uint32_t attempts = 0u;
        uint32_t max_attempts = target * 4u + 64u;
        while (samples < target && attempts < max_attempts)
        {
            ++attempts;
            int picked[4];
            mask_t opp = MASK_EMPTY;
            int ok = 1;
            for (int c = 0; c < n_hole; ++c)
            {
                int idx = (int)pe_pcg32_bounded(&rng, (uint32_t)n_free);
                int card = free_cards[idx];
                if (mask_is_set(opp, card))
                {
                    ok = 0;
                    break;
                }
                picked[c] = card;
                opp = mask_set(opp, card);
            }
            (void)picked;
            if (!ok)
                continue;
            eval_t v = pe_score_hand(ctx, opp, board);
            double strength = (hero > v) ? 1.0 : ((hero == v) ? 0.5 : 0.0);
            pe_accumulate(strength, o.n_bins, &sum, &sum_sq, hist);
            ++samples;
        }
    }

    if (samples == 0u)
        return -1;

    out->samples = samples;
    out->equity = sum / (double)samples;
    out->hs2 = sum_sq / (double)samples;
    for (int b = 0; b < o.n_bins; ++b)
        out->hist[b] = hist[b] / (double)samples;
    return 0;
}

/* Copy a feature vector of `n` doubles. Used instead of memcpy so static
 * analyzers can see a bounded, element-wise copy (memcpy triggers buffer-size
 * warnings that the tooling cannot prove safe here). */
static void pe_copy_vec(double *dst, const double *src, int n)
{
    for (int d = 0; d < n; ++d)
        dst[d] = src[d];
}

/* Flatten features into the clustering space (hs2 first, then the weighted
 * histogram block). */
static void pe_features_to_vector(const pe_hand_features_t *f,
                                  int n_bins,
                                  double hist_weight,
                                  double *out)
{
    out[0] = f->hs2;
    for (int b = 0; b < n_bins; ++b)
        out[1 + b] = f->hist[b] * hist_weight;
}


/* ------------------------------------------------------------------ *
 * Shared opponent table (2-card hole games)
 * ------------------------------------------------------------------ *
 *
 * Featurizing n hands naively costs n * m evaluations, where m is the number of
 * opponent hands - and the same m opponent hands are re-evaluated for every
 * single hand. Precomputing the opponent showdown values once turns training
 * into n + m evaluations. That matters a lot in the unoptimized sanitizer
 * builds the CI runs, where a hand evaluation is orders of magnitude slower
 * than the arithmetic around it.
 */

typedef struct
{
    mask_t hole;
    eval_t value;
} pe_opp_entry_t;

typedef struct
{
    pe_opp_entry_t *entries;
    size_t count;
} pe_opp_table_t;

static void pe_opp_table_free(pe_opp_table_t *t)
{
    if (!t)
        return;
    free(t->entries);
    t->entries = NULL;
    t->count = 0;
}

/* Enumerate and evaluate every 2-card opponent hand on the board. */
static int pe_opp_table_build(const EvalContext *ctx, mask_t board, pe_opp_table_t *out)
{
    int free_cards[MODERN_DECK_SIZE];
    int n_free = pe_collect_free_cards(board, free_cards, MODERN_DECK_SIZE);
    if (n_free < 2)
        return -1;

    size_t cap = (size_t)n_free * (size_t)(n_free - 1) / 2u;
    out->entries = (pe_opp_entry_t *)calloc(cap, sizeof(pe_opp_entry_t));
    if (!out->entries)
        return -1;
    out->count = 0;
    for (int i = 0; i < n_free; ++i)
    {
        for (int j = i + 1; j < n_free; ++j)
        {
            mask_t hole = mask_set(mask_set(MASK_EMPTY, free_cards[i]), free_cards[j]);
            out->entries[out->count].hole = hole;
            out->entries[out->count].value = pe_score_hand(ctx, hole, board);
            out->count++;
        }
    }
    return 0;
}

/* Featurize one hand against a precomputed opponent table. Opponent hands that
 * share a card with the hero hand are skipped, exactly as the enumeration in
 * pe_hand_features() does. */
static int pe_features_from_opp_table(const EvalContext *ctx,
                                     mask_t hole,
                                     mask_t board,
                                     const pe_opp_table_t *opp,
                                     int n_bins,
                                     pe_hand_features_t *out)
{
    eval_t hero = pe_score_hand(ctx, hole, board);
    if (hero == EVAL_INVALID)
        return -1;

    double sum = 0.0;
    double sum_sq = 0.0;
    double hist[PE_HS_MAX_BINS];
    memset(hist, 0, sizeof(hist));
    uint32_t samples = 0u;

    for (size_t i = 0; i < opp->count; ++i)
    {
        if (opp->entries[i].hole & hole)
            continue; /* card conflict with the hero hand */
        eval_t v = opp->entries[i].value;
        double strength = (hero > v) ? 1.0 : ((hero == v) ? 0.5 : 0.0);
        pe_accumulate(strength, n_bins, &sum, &sum_sq, hist);
        ++samples;
    }
    if (samples == 0u)
        return -1;

    memset(out, 0, sizeof(*out));
    out->n_bins = n_bins;
    out->samples = samples;
    out->equity = sum / (double)samples;
    out->hs2 = sum_sq / (double)samples;
    for (int b = 0; b < n_bins; ++b)
        out->hist[b] = hist[b] / (double)samples;
    return 0;
}

static double pe_dist2(const double *a, const double *b, int n)
{
    double d = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double diff = a[i] - b[i];
        d += diff * diff;
    }
    return d;
}

static int pe_nearest(const double *point, const double *centroids, int k, int n, double *out_d2)
{
    int best = 0;
    double best_d = pe_dist2(point, centroids, n);
    for (int c = 1; c < k; ++c)
    {
        double d = pe_dist2(point, centroids + (size_t)c * n, n);
        if (d < best_d)
        {
            best_d = d;
            best = c;
        }
    }
    if (out_d2)
        *out_d2 = best_d;
    return best;
}

/* ------------------------------------------------------------------ *
 * k-means
 * ------------------------------------------------------------------ */

/* k-means++ seeding: first centroid uniform, then each next centroid drawn
 * with probability proportional to its squared distance to the closest already
 * chosen centroid. */
static void pe_kmeanspp_init(const double *points,
                             size_t n_points,
                             int n,
                             int k,
                             double *centroids,
                             double *scratch_d2,
                             pe_pcg32_t *rng)
{
    size_t first = (size_t)pe_pcg32_bounded(rng, (uint32_t)n_points);
    pe_copy_vec(centroids, points + first * (size_t)n, n);

    for (size_t i = 0; i < n_points; ++i)
        scratch_d2[i] = pe_dist2(points + i * (size_t)n, centroids, n);

    for (int c = 1; c < k; ++c)
    {
        double total = 0.0;
        for (size_t i = 0; i < n_points; ++i)
            total += scratch_d2[i];

        size_t chosen;
        if (total <= 0.0)
        {
            /* All remaining points coincide with a centroid: fall back to a
             * uniform pick so the cluster count is still honoured. */
            chosen = (size_t)pe_pcg32_bounded(rng, (uint32_t)n_points);
        }
        else
        {
            double target = pe_pcg32_double(rng) * total;
            double acc = 0.0;
            chosen = n_points - 1;
            for (size_t i = 0; i < n_points; ++i)
            {
                acc += scratch_d2[i];
                if (acc >= target)
                {
                    chosen = i;
                    break;
                }
            }
        }
        pe_copy_vec(centroids + (size_t)c * n, points + chosen * (size_t)n, n);

        for (size_t i = 0; i < n_points; ++i)
        {
            double d = pe_dist2(points + i * (size_t)n, centroids + (size_t)c * n, n);
            if (d < scratch_d2[i])
                scratch_d2[i] = d;
        }
    }
}

static void pe_kmeans_run(const double *points,
                          size_t n_points,
                          int n,
                          int k,
                          int max_iterations,
                          double *centroids,
                          int *assign,
                          pe_pcg32_t *rng)
{
    double *sums = (double *)calloc((size_t)k * (size_t)n, sizeof(double));
    uint32_t *counts = (uint32_t *)calloc((size_t)k, sizeof(uint32_t));
    if (!sums || !counts)
    {
        /* Degenerate but valid: keep the seeded centroids and assign greedily. */
        for (size_t i = 0; i < n_points; ++i)
            assign[i] = pe_nearest(points + i * (size_t)n, centroids, k, n, NULL);
        free(sums);
        free(counts);
        return;
    }

    for (int it = 0; it < max_iterations; ++it)
    {
        int changed = 0;
        memset(sums, 0, sizeof(double) * (size_t)k * (size_t)n);
        memset(counts, 0, sizeof(uint32_t) * (size_t)k);

        for (size_t i = 0; i < n_points; ++i)
        {
            int c = pe_nearest(points + i * (size_t)n, centroids, k, n, NULL);
            if (assign[i] != c)
            {
                assign[i] = c;
                changed = 1;
            }
            const double *p = points + i * (size_t)n;
            double *acc = sums + (size_t)c * n;
            for (int d = 0; d < n; ++d)
                acc[d] += p[d];
            counts[c]++;
        }

        for (int c = 0; c < k; ++c)
        {
            if (counts[c] > 0u)
            {
                double inv = 1.0 / (double)counts[c];
                double *cen = centroids + (size_t)c * n;
                const double *acc = sums + (size_t)c * n;
                for (int d = 0; d < n; ++d)
                    cen[d] = acc[d] * inv;
            }
            else
            {
                /* Re-seed an empty cluster onto the worst-represented point so
                 * the abstraction really uses k buckets. */
                size_t worst = 0;
                double worst_d = -1.0;
                for (size_t i = 0; i < n_points; ++i)
                {
                    double d = pe_dist2(points + i * (size_t)n, centroids + (size_t)assign[i] * n, n);
                    if (d > worst_d)
                    {
                        worst_d = d;
                        worst = i;
                    }
                }
                if (worst_d <= 0.0)
                {
                    /* Everything already sits exactly on a centroid: pick a
                     * random point rather than looping forever. */
                    worst = (size_t)pe_pcg32_bounded(rng, (uint32_t)n_points);
                }
                pe_copy_vec(centroids + (size_t)c * n, points + worst * (size_t)n, n);
                changed = 1;
            }
        }

        if (!changed)
            break;
    }

    free(sums);
    free(counts);
}

/* ------------------------------------------------------------------ *
 * Table construction
 * ------------------------------------------------------------------ */

static pe_bucket_table_t *pe_table_alloc(int k, int n_bins)
{
    pe_bucket_table_t *t = (pe_bucket_table_t *)calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->k = k;
    t->n_bins = n_bins;
    t->n_features = n_bins + 1;
    t->centroids = (double *)calloc((size_t)k * (size_t)t->n_features, sizeof(double));
    t->cluster_equity = (double *)calloc((size_t)k, sizeof(double));
    t->cluster_size = (uint32_t *)calloc((size_t)k, sizeof(uint32_t));
    if (!t->centroids || !t->cluster_equity || !t->cluster_size)
    {
        pe_bucket_table_free(t);
        return NULL;
    }
    return t;
}

void pe_bucket_table_free(pe_bucket_table_t *table)
{
    if (!table)
        return;
    free(table->centroids);
    free(table->cluster_equity);
    free(table->cluster_size);
    free(table->cache);
    free(table);
}

/* Reorder clusters by ascending mean equity so bucket ids are meaningful. */
static void pe_table_sort_by_equity(pe_bucket_table_t *t)
{
    int k = t->k;
    int n = t->n_features;
    for (int i = 1; i < k; ++i)
    {
        for (int j = i; j > 0; --j)
        {
            if (t->cluster_equity[j] >= t->cluster_equity[j - 1])
                break;
            double tmp_eq = t->cluster_equity[j];
            t->cluster_equity[j] = t->cluster_equity[j - 1];
            t->cluster_equity[j - 1] = tmp_eq;
            uint32_t tmp_sz = t->cluster_size[j];
            t->cluster_size[j] = t->cluster_size[j - 1];
            t->cluster_size[j - 1] = tmp_sz;
            for (int d = 0; d < n; ++d)
            {
                double tmp = t->centroids[(size_t)j * n + d];
                t->centroids[(size_t)j * n + d] = t->centroids[(size_t)(j - 1) * n + d];
                t->centroids[(size_t)(j - 1) * n + d] = tmp;
            }
        }
    }
}

pe_bucket_table_t *pe_bucket_table_train(const EvalContext *ctx,
                                         mask_t board,
                                         const mask_t *hands,
                                         size_t n_hands,
                                         int k,
                                         const pe_hand_cluster_opts_t *opts)
{
    if (!ctx || !hands || n_hands == 0)
    {
        errno = EINVAL;
        return NULL;
    }

    pe_hand_cluster_opts_t o;
    pe_opts_normalize(opts, &o);

    if (k < 1)
        k = 1;
    if (k > PE_BUCKET_MAX_CLUSTERS)
        k = PE_BUCKET_MAX_CLUSTERS;
    if ((size_t)k > n_hands)
        k = (int)n_hands;

    int n_features = o.n_bins + 1;
    double *points = (double *)calloc(n_hands * (size_t)n_features, sizeof(double));
    double *equities = (double *)calloc(n_hands, sizeof(double));
    int *assign = (int *)calloc(n_hands, sizeof(int));
    double *scratch = (double *)calloc(n_hands, sizeof(double));
    if (!points || !equities || !assign || !scratch)
    {
        free(points);
        free(equities);
        free(assign);
        free(scratch);
        errno = ENOMEM;
        return NULL;
    }

    /* Fast path for 2-card hole games: evaluate the opponent hands once and
     * reuse them for every hand being clustered. */
    pe_opp_table_t opp;
    memset(&opp, 0, sizeof(opp));
    int use_opp_table = (o.hole_cards == 2 && pe_opp_table_build(ctx, board, &opp) == 0);

    size_t n_valid = 0;
    for (size_t i = 0; i < n_hands; ++i)
    {
        pe_hand_features_t f;
        int rc;
        if (use_opp_table && mask_popcount(hands[i]) == 2 && (hands[i] & board) == MASK_EMPTY)
            rc = pe_features_from_opp_table(ctx, hands[i], board, &opp, o.n_bins, &f);
        else
            rc = pe_hand_features(ctx, hands[i], board, &o, &f);
        if (rc != 0)
            continue;
        pe_features_to_vector(&f, o.n_bins, o.hist_weight, points + n_valid * (size_t)n_features);
        equities[n_valid] = f.equity;
        ++n_valid;
    }
    pe_opp_table_free(&opp);

    if (n_valid == 0)
    {
        free(points);
        free(equities);
        free(assign);
        free(scratch);
        errno = EINVAL;
        return NULL;
    }
    if ((size_t)k > n_valid)
        k = (int)n_valid;

    /* Collapse identical feature vectors before clustering. k-means cannot use
     * more clusters than there are distinct points; if k exceeds the number of
     * unique vectors the surplus clusters would be reseeded onto already-claimed
     * points forever (see the empty-cluster handling in pe_kmeans_run) and the
     * table would silently use fewer buckets than requested. Deduping and
     * clamping k avoids that, while keeping every hand's equity contribution via
     * the unique->original mapping below. */
    double *uniq_pts = (double *)calloc(n_valid * (size_t)n_features, sizeof(double));
    double *uniq_eq = (double *)calloc(n_valid, sizeof(double));
    size_t *uniq_of = (size_t *)calloc(n_valid, sizeof(size_t)); /* unique idx per original */
    int *uniq_n = (int *)calloc(n_valid, sizeof(int));          /* population per unique */
    if (!uniq_pts || !uniq_eq || !uniq_of || !uniq_n)
    {
        free(points); free(equities); free(assign); free(scratch);
        free(uniq_pts); free(uniq_eq); free(uniq_of); free(uniq_n);
        errno = ENOMEM;
        return NULL;
    }
    size_t n_unique = 0;
    for (size_t i = 0; i < n_valid; ++i)
    {
        int found = 0;
        for (size_t u = 0; u < n_unique; ++u)
        {
            const double *a = points + i * (size_t)n_features;
            const double *b = uniq_pts + u * (size_t)n_features;
            int same = 1;
            for (int d = 0; d < n_features; ++d)
            {
                /* Feature vectors are produced by identical arithmetic, so an
                 * exact match is what we want; use a tiny epsilon to keep the
                 * compiler's float-equality warning quiet and tolerate any
                 * incidental rounding. */
                if (fabs(a[d] - b[d]) > 1e-15) { same = 0; break; }
            }
            if (same) { uniq_of[i] = u; uniq_n[u]++; found = 1; break; }
        }
        if (!found)
        {
            pe_copy_vec(uniq_pts + n_unique * (size_t)n_features,
                        points + i * (size_t)n_features, n_features);
            uniq_eq[n_unique] = equities[i];
            uniq_of[i] = (int)n_unique;
            uniq_n[n_unique] = 1;
            ++n_unique;
        }
    }
    if ((size_t)k > n_unique)
        k = (int)n_unique;

    pe_bucket_table_t *t = pe_table_alloc(k, o.n_bins);
    if (!t)
    {
        free(points);
        free(equities);
        free(assign);
        free(scratch);
        free(uniq_pts); free(uniq_eq); free(uniq_of); free(uniq_n);
        errno = ENOMEM;
        return NULL;
    }
    t->board = board;
    t->hole_cards = o.hole_cards;
    t->seed = o.seed;
    t->max_samples = o.max_samples;
    t->opp_samples = o.opp_samples;
    t->hist_weight = o.hist_weight;

    pe_pcg32_t rng;
    pe_pcg32_seed(&rng, (uint64_t)o.seed + 0x5DEECE66DULL);

    pe_kmeanspp_init(uniq_pts, n_unique, n_features, k, t->centroids, scratch, &rng);
    for (size_t i = 0; i < n_unique; ++i)
        assign[i] = -1;
    pe_kmeans_run(uniq_pts, n_unique, n_features, k, o.max_iterations, t->centroids, assign, &rng);

    /* Map each original hand through its unique representative, then accumulate
     * per-cluster mean equity and population across all original hands. */
    for (size_t i = 0; i < n_valid; ++i)
    {
        size_t u = uniq_of[i];
        int c = assign[u];
        if (c < 0 || c >= k)
            c = pe_nearest(uniq_pts + u * (size_t)n_features, t->centroids, k, n_features, NULL);
        t->cluster_equity[c] += equities[i];
        t->cluster_size[c]++;
    }
    for (int c = 0; c < k; ++c)
    {
        if (t->cluster_size[c] > 0u)
            t->cluster_equity[c] /= (double)t->cluster_size[c];
        else
            t->cluster_equity[c] = t->centroids[(size_t)c * n_features]; /* hs2 proxy */
    }

    free(uniq_pts); free(uniq_eq); free(uniq_of); free(uniq_n);
    pe_table_sort_by_equity(t);

    free(points);
    free(equities);
    free(assign);
    free(scratch);
    return t;
}

pe_bucket_table_t *pe_bucket_table_train_all(const EvalContext *ctx,
                                             mask_t board,
                                             int k,
                                             const pe_hand_cluster_opts_t *opts)
{
    if (!ctx)
    {
        errno = EINVAL;
        return NULL;
    }

    pe_hand_cluster_opts_t o;
    pe_opts_normalize(opts, &o);

    int free_cards[MODERN_DECK_SIZE];
    int n_free = pe_collect_free_cards(board, free_cards, MODERN_DECK_SIZE);
    if (n_free < o.hole_cards)
    {
        errno = EINVAL;
        return NULL;
    }

    mask_t *hands = NULL;
    size_t n_hands = 0;

    if (o.hole_cards == 2)
    {
        size_t cap = (size_t)n_free * (size_t)(n_free - 1) / 2u;
        hands = (mask_t *)calloc(cap, sizeof(mask_t));
        if (!hands)
        {
            errno = ENOMEM;
            return NULL;
        }
        for (int i = 0; i < n_free; ++i)
            for (int j = i + 1; j < n_free; ++j)
                hands[n_hands++] = mask_set(mask_set(MASK_EMPTY, free_cards[i]), free_cards[j]);
    }
    else
    {
        /* Enumerating C(47,4) = 178365 Omaha hands and featurizing each one is
         * prohibitive; sample a deterministic (seeded) subset of hero hands
         * instead. The per-hero opponent rollout is bounded separately by
         * opts->opp_samples, so total cost stays O(max_samples * opp_samples)
         * rather than O(max_samples^2)-ish. */
        size_t cap = o.max_samples;
        hands = (mask_t *)calloc(cap, sizeof(mask_t));
        if (!hands)
        {
            errno = ENOMEM;
            return NULL;
        }
        pe_pcg32_t rng;
        pe_pcg32_seed(&rng, (uint64_t)o.seed + 0x2545F4914F6CDD1DULL);
        size_t attempts = 0;
        size_t max_attempts = cap * 8u + 64u;
        while (n_hands < cap && attempts < max_attempts)
        {
            ++attempts;
            mask_t h = MASK_EMPTY;
            int ok = 1;
            for (int c = 0; c < o.hole_cards; ++c)
            {
                int card = free_cards[pe_pcg32_bounded(&rng, (uint32_t)n_free)];
                if (mask_is_set(h, card))
                {
                    ok = 0;
                    break;
                }
                h = mask_set(h, card);
            }
            if (ok)
                hands[n_hands++] = h;
        }
    }

    pe_bucket_table_t *t = pe_bucket_table_train(ctx, board, hands, n_hands, k, &o);
    free(hands);
    return t;
}

int pe_bucket_table_count(const pe_bucket_table_t *table)
{
    return table ? table->k : 0;
}

mask_t pe_bucket_table_board(const pe_bucket_table_t *table)
{
    return table ? table->board : MASK_EMPTY;
}

double pe_bucket_table_cluster_equity(const pe_bucket_table_t *table, int bucket)
{
    if (!table || bucket < 0 || bucket >= table->k)
        return -1.0;
    return table->cluster_equity[bucket];
}

int pe_bucket_table_cluster_size(const pe_bucket_table_t *table, int bucket)
{
    if (!table || bucket < 0 || bucket >= table->k)
        return -1;
    return (int)table->cluster_size[bucket];
}

int pe_bucket_table_assign_features(const pe_bucket_table_t *table,
                                    const pe_hand_features_t *features)
{
    if (!table || !features || features->n_bins != table->n_bins)
        return -1;
    double vec[PE_HS_MAX_FEATURES];
    pe_features_to_vector(features, table->n_bins, table->hist_weight, vec);
    return pe_nearest(vec, table->centroids, table->k, table->n_features, NULL);
}

int pe_bucket_table_assign(const pe_bucket_table_t *table,
                           const EvalContext *ctx,
                           mask_t hole,
                           mask_t board)
{
    if (!table || !ctx)
        return -1;
    /* Reject a table that does not match this board or hole-card game. Without
     * this, pe_hand_features() would featurize the hand with the wrong path
     * (e.g. a 4-card Omaha hand scored against Hold'em centroids from a
     * same-board file) and return a plausible-but-invalid bucket, defeating the
     * adapter's mode-3 fallback and producing wrong infoset keys. */
    if (board != table->board)
        return -1;
    int n_hole = mask_popcount(hole);
    if (n_hole != table->hole_cards)
        return -1;
    if ((hole & board) != MASK_EMPTY)
        return -1;
    pe_hand_cluster_opts_t o;
    memset(&o, 0, sizeof(o));
    o.n_bins = table->n_bins;
    o.hole_cards = table->hole_cards;
    o.max_samples = table->max_samples;
    o.opp_samples = table->opp_samples;
    o.seed = table->seed;
    o.hist_weight = table->hist_weight;

    pe_hand_features_t f;
    if (pe_hand_features(ctx, hole, board, &o, &f) != 0)
        return -1;
    return pe_bucket_table_assign_features(table, &f);
}

/* ------------------------------------------------------------------ *
 * Lookup memoization
 * ------------------------------------------------------------------ */

static uint64_t pe_cache_key(mask_t hole, mask_t board)
{
    uint64_t h = (uint64_t)hole * 0x9E3779B97F4A7C15ULL;
    h ^= (uint64_t)board + 0xC2B2AE3D27D4EB4FULL + (h << 6) + (h >> 2);
    h *= 0xD6E8FEB86659FD93ULL;
    h ^= h >> 29;
    /* 0 is reserved as the "empty slot" marker. */
    return h ? h : 1ULL;
}

static int pe_cache_grow(pe_bucket_table_t *t)
{
    size_t new_cap = t->cache_cap ? t->cache_cap * 2u : PE_BKT_CACHE_MIN_CAP;
    pe_bkt_cache_entry_t *tab = (pe_bkt_cache_entry_t *)calloc(new_cap, sizeof(*tab));
    if (!tab)
        return -1;
    for (size_t i = 0; i < t->cache_cap; ++i)
    {
        if (!t->cache[i].key)
            continue;
        size_t idx = (size_t)(t->cache[i].key & (uint64_t)(new_cap - 1u));
        while (tab[idx].key)
            idx = (idx + 1u) & (new_cap - 1u);
        tab[idx] = t->cache[i];
    }
    free(t->cache);
    t->cache = tab;
    t->cache_cap = new_cap;
    return 0;
}

int pe_bucket_table_assign_cached(pe_bucket_table_t *table,
                                  const EvalContext *ctx,
                                  mask_t hole,
                                  mask_t board)
{
    if (!table || !ctx)
        return -1;

    uint64_t key = pe_cache_key(hole, board);
    if (table->cache_cap == 0u && pe_cache_grow(table) != 0)
        return pe_bucket_table_assign(table, ctx, hole, board);

    size_t mask = table->cache_cap - 1u;
    size_t idx = (size_t)(key & (uint64_t)mask);
    while (table->cache[idx].key)
    {
        if (table->cache[idx].key == key)
            return table->cache[idx].bucket;
        idx = (idx + 1u) & mask;
    }

    int bucket = pe_bucket_table_assign(table, ctx, hole, board);
    if (bucket < 0)
        return -1;

    /* Keep the load factor under 0.75 so probe chains stay short. */
    if ((table->cache_used + 1u) * 4u > table->cache_cap * 3u)
    {
        if (pe_cache_grow(table) == 0)
        {
            mask = table->cache_cap - 1u;
            idx = (size_t)(key & (uint64_t)mask);
            while (table->cache[idx].key)
                idx = (idx + 1u) & mask;
        }
    }
    table->cache[idx].key = key;
    table->cache[idx].bucket = bucket;
    table->cache_used++;
    return bucket;
}

/* ------------------------------------------------------------------ *
 * Serialization (.pe_bkt)
 * ------------------------------------------------------------------ */

/*
 * .pe_bkt layout (little-endian, fixed width):
 *
 *   char     magic[8]      "PEBKT001"
 *   uint32   version       PE_BKT_VERSION
 *   uint32   flags         reserved, 0
 *   uint32   k             cluster count
 *   uint32   n_bins        histogram bins
 *   uint32   hole_cards    2 or 4
 *   uint32   seed          training seed
 *   uint32   max_samples   hero-hand budget used at training time
 *   uint32   opp_samples   per-hero opponent rollout used at training time
 *                           (was "reserved" in v1; 0 -> default on load)
 *   uint64   board         card mask the table was trained on
 *   double   hist_weight
 *   then, k times:
 *     double   cluster_equity
 *     uint32   cluster_size
 *     double   centroid[n_bins + 1]
 */

static int pe_wr_u32(FILE *f, uint32_t v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    b[2] = (unsigned char)((v >> 16) & 0xFFu);
    b[3] = (unsigned char)((v >> 24) & 0xFFu);
    return (fwrite(b, 1, 4, f) == 4) ? 0 : -1;
}

static int pe_wr_u64(FILE *f, uint64_t v)
{
    unsigned char b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = (unsigned char)((v >> (8 * i)) & 0xFFu);
    return (fwrite(b, 1, 8, f) == 8) ? 0 : -1;
}

static int pe_wr_f64(FILE *f, double v)
{
    union { double d; uint64_t u; } pun;
    pun.d = v;
    return pe_wr_u64(f, pun.u);
}

static int pe_rd_u32(FILE *f, uint32_t *out)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4)
        return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static int pe_rd_u64(FILE *f, uint64_t *out)
{
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8)
        return -1;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= ((uint64_t)b[i]) << (8 * i);
    *out = v;
    return 0;
}

static int pe_rd_f64(FILE *f, double *out)
{
    uint64_t bits;
    if (pe_rd_u64(f, &bits) != 0)
        return -1;
    union { double d; uint64_t u; } pun;
    pun.u = bits;
    *out = pun.d;
    return 0;
}

int pe_bucket_table_save(const pe_bucket_table_t *table, const char *path)
{
    if (!table || !path)
    {
        errno = EINVAL;
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    int rc = 0;
    if (fwrite(PE_BKT_MAGIC, 1, 8, f) != 8)
        rc = -1;
    if (!rc)
        rc = pe_wr_u32(f, PE_BKT_VERSION);
    if (!rc)
        rc = pe_wr_u32(f, 0u); /* flags */
    if (!rc)
        rc = pe_wr_u32(f, (uint32_t)table->k);
    if (!rc)
        rc = pe_wr_u32(f, (uint32_t)table->n_bins);
    if (!rc)
        rc = pe_wr_u32(f, (uint32_t)table->hole_cards);
    if (!rc)
        rc = pe_wr_u32(f, table->seed);
    if (!rc)
        rc = pe_wr_u32(f, table->max_samples);
    if (!rc)
        rc = pe_wr_u32(f, table->opp_samples); /* reserved -> opp_samples */
    if (!rc)
        rc = pe_wr_u64(f, (uint64_t)table->board);
    if (!rc)
        rc = pe_wr_f64(f, table->hist_weight);

    for (int c = 0; !rc && c < table->k; ++c)
    {
        rc = pe_wr_f64(f, table->cluster_equity[c]);
        if (!rc)
            rc = pe_wr_u32(f, table->cluster_size[c]);
        for (int d = 0; !rc && d < table->n_features; ++d)
            rc = pe_wr_f64(f, table->centroids[(size_t)c * table->n_features + d]);
    }

    if (fclose(f) != 0)
        rc = -1;
    if (rc)
    {
        if (errno == 0)
            errno = EIO;
        remove(path);
    }
    return rc;
}

pe_bucket_table_t *pe_bucket_table_load(const char *path)
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
    uint32_t version = 0, flags = 0, k = 0, n_bins = 0, hole_cards = 0, seed = 0, max_samples = 0;
    uint64_t board = 0;
    double hist_weight = 1.0;

    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, PE_BKT_MAGIC, 8) != 0)
        goto bad_format;
    if (pe_rd_u32(f, &version) != 0 || version != PE_BKT_VERSION)
        goto bad_format;
    if (pe_rd_u32(f, &flags) != 0)
        goto bad_format;
    if (pe_rd_u32(f, &k) != 0 || k == 0u || k > PE_BUCKET_MAX_CLUSTERS)
        goto bad_format;
    if (pe_rd_u32(f, &n_bins) != 0 || n_bins == 0u || n_bins > PE_HS_MAX_BINS)
        goto bad_format;
    if (pe_rd_u32(f, &hole_cards) != 0 || hole_cards < 2u || hole_cards > 4u)
        goto bad_format;
    if (pe_rd_u32(f, &seed) != 0)
        goto bad_format;
    if (pe_rd_u32(f, &max_samples) != 0)
        goto bad_format;
    /* The slot after max_samples was "reserved" in the initial format; it now
     * carries opp_samples. Older files wrote 0 here, which maps to the default
     * opponent budget on load (see pe_table_alloc below). */
    uint32_t opp_samples = 0;
    if (pe_rd_u32(f, &opp_samples) != 0)
        goto bad_format;
    if (pe_rd_u64(f, &board) != 0)
        goto bad_format;
    if (pe_rd_f64(f, &hist_weight) != 0)
        goto bad_format;
    (void)flags;

    pe_bucket_table_t *t = pe_table_alloc((int)k, (int)n_bins);
    if (!t)
    {
        fclose(f);
        errno = ENOMEM;
        return NULL;
    }
    t->board = (mask_t)board;
    t->hole_cards = (int)hole_cards;
    t->seed = seed;
    t->max_samples = max_samples ? max_samples : PE_HS_DEFAULT_MAX_SAMPLES;
    t->opp_samples = opp_samples ? opp_samples : PE_HS_DEFAULT_OPP_SAMPLES;
    t->hist_weight = (hist_weight > 0.0) ? hist_weight : 1.0;

    for (uint32_t c = 0; c < k; ++c)
    {
        if (pe_rd_f64(f, &t->cluster_equity[c]) != 0)
            goto bad_payload;
        if (pe_rd_u32(f, &t->cluster_size[c]) != 0)
            goto bad_payload;
        for (int d = 0; d < t->n_features; ++d)
        {
            if (pe_rd_f64(f, &t->centroids[(size_t)c * t->n_features + d]) != 0)
                goto bad_payload;
        }
    }

    fclose(f);
    return t;

bad_payload:
    pe_bucket_table_free(t);
    fclose(f);
    errno = EINVAL;
    return NULL;

bad_format:
    fclose(f);
    errno = EINVAL;
    return NULL;
}
