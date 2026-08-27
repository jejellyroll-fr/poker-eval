/*
 * strength_bucketing.h - EHS / EHS2 strength bucketing (FEAT-13)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The strength half of issue #149's node-abstraction pair. Where
 * hand_clustering.h (FEAT-04) learns k-means clusters over the full feature
 * vector (E[HS^2] + an equity histogram), this module buckets hands on the
 * classic two-dimensional strength abstraction used by abstraction-based
 * solvers:
 *
 *     EHS  = E[ HS ]            (Expected Hand Strength)
 *     EHS2 = E[ HS^2 ]          (Expected Hand Strength Squared)
 *
 * where HS is the hand's showdown strength against a single opponent (1 win,
 * 0.5 chop, 0 loss) — the same definition hand_clustering uses, so the two
 * abstraction layers stay consistent with the utilities CFR backs up.
 *
 * The (EHS, EHS2) point is clustered with k-means (k-means++ seeding + Lloyd
 * iterations, deterministic via an internal PCG32) into `n_buckets` strength
 * buckets. The resulting table is serializable (.pe_sbk, the same magic /
 * version / little-endian conventions as the compact storage and bucket
 * tables) and can be trained once per (board, hole-size) and reused across a
 * multi-street solve. Because EHS2 >= EHS^2, the points lie in a convex region
 * of the unit square and k-means separates polarized hands (high EHS, high
 * EHS2) from marginal ones (high EHS, low EHS2) exactly as the abstraction
 * literature intends (the original EHS2 bucketing of Gilpin & Sandholm).
 *
 * This is the per-hand strength abstraction; combine it with
 * board_texture.h's texture id to realize the compatible "Strength Buckets +
 * Texture Filter" node abstraction for PLO/Hold'em multi-street trees.
 */

#ifndef POKER_EVAL_STRENGTH_BUCKETING_H
#define POKER_EVAL_STRENGTH_BUCKETING_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of strength buckets (the bucket id is packed into key bits). */
#define PE_SBK_MAX_BUCKETS 256

/* Default number of strength buckets when opts->n_buckets is 0. */
#define PE_SBK_DEFAULT_BUCKETS 50

/* Default Lloyd iterations when opts->max_iterations is 0. */
#define PE_SBK_DEFAULT_ITERATIONS 100

/* Default opponent hands rolled out PER hero hand (Omaha sampling). */
#define PE_SBK_DEFAULT_OPP_SAMPLES 200

/* Hard ceiling for the opponent sampling budget. */
#define PE_SBK_MAX_OPP_SAMPLES 20000

/* Default cap on hero hands trained/sampled (Omaha only). */
#define PE_SBK_DEFAULT_MAX_SAMPLES 1000

/* ------------------------------------------------------------------ *
 * Strength features
 * ------------------------------------------------------------------ */

/**
 * Strength features for a single hand on a board.
 *
 * ehs  is E[HS]  in [0,1], the mean showdown strength.
 * ehs2 is E[HS^2] in [0,1], the mean squared strength; ehs2 >= ehs^2 always,
 *      so a polarized hand (often the nuts, sometimes nothing) has ehs2 close
 *      to ehs while a marginal hand (always a coin-flip) has ehs2 well below
 *      ehs. The (ehs, ehs2) pair is the classic 2-D strength abstraction.
 */
typedef struct pe_strength_features_s
{
    double ehs;        /* E[HS]   in [0,1] */
    double ehs2;       /* E[HS^2] in [0,1] */
    uint32_t samples;  /* opponent hands actually evaluated */
} pe_strength_features_t;

/**
 * Options controlling feature extraction and clustering.
 * Zero-initializing selects the documented defaults.
 */
typedef struct pe_strength_cluster_opts_s
{
    int n_buckets;       /* target bucket count, 0 => PE_SBK_DEFAULT_BUCKETS */
    int hole_cards;      /* 2 (hold'em) or 4 (omaha), 0 => 2 */
    uint32_t max_samples;/* cap on HERO hands trained/sampled, 0 => default */
    uint32_t opp_samples;/* opponent hands rolled out PER hero hand, 0 => default */
    uint32_t seed;       /* RNG seed; identical seeds give identical tables */
    int max_iterations;  /* Lloyd iterations, 0 => PE_SBK_DEFAULT_ITERATIONS */
} pe_strength_cluster_opts_t;

/**
 * Compute the (EHS, EHS2) strength features of one hand on one board.
 *
 * For 2-card hole games the opponent hands are enumerated exhaustively. For
 * 4-card games the opponent hands are sampled deterministically (seeded from
 * opts->seed) up to opts->opp_samples per hero hand.
 *
 * @return 0 on success, -1 on invalid arguments.
 */
int pe_strength_features(const EvalContext *ctx,
                         mask_t hole,
                         mask_t board,
                         const pe_strength_cluster_opts_t *opts,
                         pe_strength_features_t *out);

/* ------------------------------------------------------------------ *
 * Strength bucket table
 * ------------------------------------------------------------------ */

typedef struct pe_strength_table_t pe_strength_table_t;

/**
 * Train a strength bucket table by k-means clustering the (EHS, EHS2) feature
 * vectors of the given hands on the given board.
 *
 * Centroids are sorted by ascending ehs so bucket 0 is always the weakest and
 * bucket n-1 the strongest, making bucket ids comparable across tables.
 *
 * @param board   Board the abstraction is trained for (3, 4 or 5 cards)
 * @param hands   Array of hole-card masks (must not be NULL)
 * @param n_hands Number of hands (must be > 0)
 * @param opts    Options (may be NULL for all defaults)
 * @param k_out   Receives the actual bucket count (<= requested), may be NULL
 * @return A newly allocated table, or NULL on error. Free with
 *         pe_strength_table_free().
 */
pe_strength_table_t *pe_strength_table_train(const EvalContext *ctx,
                                             mask_t board,
                                             const mask_t *hands,
                                             size_t n_hands,
                                             const pe_strength_cluster_opts_t *opts,
                                             int *k_out);

/**
 * Train a strength bucket table over every possible hole-card combination on
 * the board (all C(52 - |board|, hole_cards) hands), the usual way to build a
 * reusable river/flop abstraction. For 4-card games the enumeration is capped
 * to opts->max_samples hands drawn deterministically from the seed.
 */
pe_strength_table_t *pe_strength_table_train_all(const EvalContext *ctx,
                                                 mask_t board,
                                                 const pe_strength_cluster_opts_t *opts,
                                                 int *k_out);

/** Release a table returned by train/load. NULL is accepted. */
void pe_strength_table_free(pe_strength_table_t *table);

/** Number of buckets in the table (0 if table is NULL). */
int pe_strength_table_count(const pe_strength_table_t *table);

/** Board the table was trained on (MASK_EMPTY if table is NULL). */
mask_t pe_strength_table_board(const pe_strength_table_t *table);

/** Mean EHS of a bucket, or -1.0 on bad arguments. */
double pe_strength_table_bucket_ehs(const pe_strength_table_t *table, int bucket);

/** Mean EHS2 of a bucket, or -1.0 on bad arguments. */
double pe_strength_table_bucket_ehs2(const pe_strength_table_t *table, int bucket);

/** Number of training hands assigned to a bucket, or -1 on bad arguments. */
int pe_strength_table_bucket_size(const pe_strength_table_t *table, int bucket);

/**
 * Assign a hand to its bucket by nearest (EHS, EHS2) centroid. This recomputes
 * the hand's features (the expensive part); the solver hot path should call
 * pe_strength_table_assign_cached().
 * @return the bucket id in [0, k), or -1 on error.
 */
int pe_strength_table_assign(const pe_strength_table_t *table,
                             const EvalContext *ctx,
                             mask_t hole,
                             mask_t board);

/**
 * Assign a hand to its bucket, memoizing the result per (hole, board) pair.
 * Safe to call from the CFR traversal (same hand featurized once, later visits
 * are a hash lookup). Not thread-safe on a single table.
 * @return the bucket id in [0, k), or -1 on error.
 */
int pe_strength_table_assign_cached(pe_strength_table_t *table,
                                    const EvalContext *ctx,
                                    mask_t hole,
                                    mask_t board);

/** Assign a precomputed feature vector to its nearest centroid. */
int pe_strength_table_assign_features(const pe_strength_table_t *table,
                                      const pe_strength_features_t *features);

/* ------------------------------------------------------------------ *
 * .pe_sbk : serialized strength bucket table
 * ------------------------------------------------------------------ */

/**
 * Save a strength table to a compact binary .pe_sbk file. Mirrors the FEAT-09
 * compact-storage conventions (8-byte magic, version word, little-endian
 * fixed-width fields).
 * @return 0 on success, -1 on error (errno set on failure).
 */
int pe_strength_table_save(const pe_strength_table_t *table, const char *path);

/**
 * Load a .pe_sbk file.
 * @return A newly allocated table on success, NULL on error (errno set; EINVAL
 *         on magic/version mismatch or corrupt payload). Free with
 *         pe_strength_table_free().
 */
pe_strength_table_t *pe_strength_table_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_STRENGTH_BUCKETING_H */
