/*
 * hand_clustering.h - Learned hand abstraction via k-means clustering (FEAT-04)
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * The river adapters historically abstracted hands by plain strength
 * thresholding (bucket_mode 0..3: hand class + a linear cut of the eval_t
 * offset). That partition ignores how a hand actually performs against the
 * opponent's range: two hands with adjacent eval_t values can have completely
 * different equity profiles (the nuts versus a dominated bluff-catcher).
 *
 * This module implements a *learned* abstraction instead:
 *
 *   1. every hand is mapped to a feature vector
 *        [ E[HS^2] , equity-distribution histogram (OCHS-style) ]
 *   2. k-means (k-means++ seeding + Lloyd iterations) clusters those vectors
 *   3. the resulting centroids form a serializable bucket table (.pe_bkt)
 *      that can be trained once and reused across solves.
 *
 * The table is consumed by the CFR adapters through bucket_mode = 4, and lives
 * in a file format that follows the same conventions as the FEAT-09 compact
 * storage (.pe_sol / .pe_tree): an 8-byte magic, a version word, and
 * little-endian fixed-width fields.
 */

#ifndef POKER_EVAL_HAND_CLUSTERING_H
#define POKER_EVAL_HAND_CLUSTERING_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum number of equity-distribution histogram bins. */
#define PE_HS_MAX_BINS 16

/* Maximum feature vector length: E[HS^2] + histogram bins. */
#define PE_HS_MAX_FEATURES (PE_HS_MAX_BINS + 1)

/* Maximum number of clusters (the bucket id is packed into 8 key bits). */
#define PE_BUCKET_MAX_CLUSTERS 256

/* Default number of histogram bins when opts->n_bins is 0. */
#define PE_HS_DEFAULT_BINS 8

/* Default cap on the number of HERO hands that Omaha training samples
 * (train_all). A modest number keeps training cheap; the per-hero opponent
 * rollout is bounded separately by PE_HS_DEFAULT_OPP_SAMPLES. Total Omaha
 * training cost is O(max_samples * opp_samples). */
#define PE_HS_DEFAULT_MAX_SAMPLES 1000

/* Default number of opponent hands rolled out PER hero hand in Omaha feature
 * extraction. Kept small and independent of the hero-hand budget so the two
 * sampling dimensions do not multiply into an impractical cost. */
#define PE_HS_DEFAULT_OPP_SAMPLES 200

/* Hard ceilings for the two sampling budgets. */
#define PE_HS_MAX_OPP_SAMPLES 20000


    /* ------------------------------------------------------------------ *
     * Feature extraction
     * ------------------------------------------------------------------ */

    /**
     * Feature vector for a single hand on a given board.
     *
     * hs2 is E[HS^2] (the expectation of the squared hand strength, where a
     * split pot counts as half a win), the classic scalar abstraction feature.
     * hist is the normalized distribution of per-matchup hand strength across
     * n_bins equal-width bins in [0, 1]; it separates polarized hands from
     * hands of merely average strength, which hs2 alone conflates.
     */
    typedef struct pe_hand_features_s
    {
        double hs2;                    /* E[HS^2] in [0, 1] */
        double equity;                 /* E[HS] in [0, 1] (mean strength) */
        double hist[PE_HS_MAX_BINS];   /* normalized histogram, sums to 1 */
        int n_bins;                    /* number of populated hist entries */
        uint32_t samples;              /* opponent hands actually evaluated */
    } pe_hand_features_t;

    /**
     * Options controlling feature extraction and clustering.
     *
     * Zero-initializing this struct selects the documented defaults, so
     * `pe_hand_cluster_opts_t opts = {0};` is always valid.
     */
    typedef struct pe_hand_cluster_opts_s
    {
        int n_bins;           /* histogram bins, 0 => PE_HS_DEFAULT_BINS */
        int hole_cards;       /* 2 (hold'em) or 4 (omaha), 0 => 2 */
        uint32_t max_samples; /* cap on HERO hands trained/sampled, 0 => default.
                                 For Omaha this bounds the number of distinct
                                 hands featurized; for Hold'em it is the size of
                                 the exhaustive hand enumeration (unused, since
                                 all C(47,2) hands are always covered). */
        uint32_t opp_samples; /* cap on opponent hands rolled out PER hero hand
                                 (Omaha only), 0 => default. Kept separate from
                                 max_samples so the two sampling dimensions do
                                 not multiply into an impractical cost. */
        uint32_t seed;        /* RNG seed; identical seeds give identical tables */
        int max_iterations;   /* Lloyd iterations, 0 => 100 */
        double hist_weight;   /* weight of the histogram block, 0 => 1.0 */
    } pe_hand_cluster_opts_t;

    /**
     * Compute the feature vector of one hand on one board.
     *
     * For 2-card hole games the opponent hands are enumerated exhaustively.
     * For 4-card (Omaha) games the opponent hands are sampled deterministically
     * (seeded from opts->seed) up to opts->opp_samples per hero hand, so the
     * result is reproducible for a given (hand, board, opts) triple without the
     * cost scaling with the hero-hand budget.
     *
     * @param ctx    Evaluation context (must not be NULL)
     * @param hole   Hole cards of the hand being scored
     * @param board  Board cards (3, 4 or 5 cards)
     * @param opts   Options (may be NULL for all defaults)
     * @param out    Receives the features (must not be NULL)
     * @return 0 on success, -1 on invalid arguments
     */
    int pe_hand_features(const EvalContext *ctx,
                         mask_t hole,
                         mask_t board,
                         const pe_hand_cluster_opts_t *opts,
                         pe_hand_features_t *out);

    /* ------------------------------------------------------------------ *
     * Bucket table
     * ------------------------------------------------------------------ */

    /**
     * A trained hand abstraction: k centroids in feature space plus the
     * metadata needed to reproduce the feature extraction at lookup time.
     */
    typedef struct pe_bucket_table_t pe_bucket_table_t;

    /**
     * Train a bucket table by clustering the feature vectors of the given
     * hands on the given board.
     *
     * Centroids are sorted by ascending mean equity, so bucket 0 is always the
     * weakest cluster and bucket k-1 the strongest. That ordering makes bucket
     * ids comparable across tables and readable in strategy dumps.
     *
     * For 2-card hole games the opponent hands are evaluated once and shared
     * across every trained hand, so training cost is O(n_hands + n_opponents)
     * evaluations rather than O(n_hands * n_opponents).
     *
     * @param ctx      Evaluation context (must not be NULL)
     * @param board    Board the abstraction is trained for
     * @param hands    Array of hole-card masks (must not be NULL)
     * @param n_hands  Number of hands (must be > 0)
     * @param k        Requested cluster count (clamped to [1, n_hands] and to
     *                 PE_BUCKET_MAX_CLUSTERS)
     * @param opts     Options (may be NULL for all defaults)
     * @return A newly allocated table, or NULL on error. Free with
     *         pe_bucket_table_free().
     */
    pe_bucket_table_t *pe_bucket_table_train(const EvalContext *ctx,
                                             mask_t board,
                                             const mask_t *hands,
                                             size_t n_hands,
                                             int k,
                                             const pe_hand_cluster_opts_t *opts);

    /**
     * Train a bucket table over every possible hole-card combination on the
     * board (all C(52 - |board|, hole_cards) hands), which is the usual way to
     * build a reusable river abstraction.
     *
     * For 4-card hole games the enumeration is capped: at most
     * opts->max_samples hands are drawn deterministically from the seed.
     */
    pe_bucket_table_t *pe_bucket_table_train_all(const EvalContext *ctx,
                                                 mask_t board,
                                                 int k,
                                                 const pe_hand_cluster_opts_t *opts);

    /** Release a table returned by train/load. NULL is accepted. */
    void pe_bucket_table_free(pe_bucket_table_t *table);

    /** Number of clusters in the table (0 if table is NULL). */
    int pe_bucket_table_count(const pe_bucket_table_t *table);

    /** Board the table was trained on (MASK_EMPTY if table is NULL). */
    mask_t pe_bucket_table_board(const pe_bucket_table_t *table);

    /**
     * Mean equity of a cluster, used to interpret and to order buckets.
     *
     * @return the cluster equity in [0, 1], or -1.0 if the arguments are invalid.
     */
    double pe_bucket_table_cluster_equity(const pe_bucket_table_t *table, int bucket);

    /** Number of training hands assigned to a cluster, or -1 on bad arguments. */
    int pe_bucket_table_cluster_size(const pe_bucket_table_t *table, int bucket);

    /**
     * Assign a hand to its bucket by nearest centroid.
     *
     * This recomputes the hand's features, which is the expensive part; the
     * solver hot path should therefore call pe_bucket_table_assign_cached().
     *
     * @return the bucket id in [0, k), or -1 on error.
     */
    int pe_bucket_table_assign(const pe_bucket_table_t *table,
                               const EvalContext *ctx,
                               mask_t hole,
                               mask_t board);

    /**
     * Assign a hand to its bucket, memoizing the result per (hole, board) pair.
     *
     * Safe to call from the CFR traversal: the same hand is featurized once and
     * every later visit is a hash lookup. Not thread-safe on a single table.
     *
     * @return the bucket id in [0, k), or -1 on error.
     */
    int pe_bucket_table_assign_cached(pe_bucket_table_t *table,
                                      const EvalContext *ctx,
                                      mask_t hole,
                                      mask_t board);

    /** Assign a precomputed feature vector to its nearest centroid. */
    int pe_bucket_table_assign_features(const pe_bucket_table_t *table,
                                        const pe_hand_features_t *features);

    /* ------------------------------------------------------------------ *
     * .pe_bkt : serialized bucket table
     * ------------------------------------------------------------------ */

    /**
     * Save a bucket table to a compact binary .pe_bkt file.
     *
     * The format mirrors the FEAT-09 compact storage conventions: the 8-byte
     * magic "PEBKT001", a version word, then little-endian fixed-width fields.
     * A table is portable across solves of the same board.
     *
     * @return 0 on success, -1 on error (errno is set on failure)
     */
    int pe_bucket_table_save(const pe_bucket_table_t *table, const char *path);

    /**
     * Load a .pe_bkt file.
     *
     * @return A newly allocated table on success, NULL on error (errno is set;
     *         EINVAL on a magic/version mismatch or a corrupt payload).
     *         Free with pe_bucket_table_free().
     */
    pe_bucket_table_t *pe_bucket_table_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_HAND_CLUSTERING_H */
