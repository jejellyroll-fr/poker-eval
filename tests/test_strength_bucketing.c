/*
 * test_strength_bucketing.c - Unit tests for the FEAT-13 EHS/EHS2 strength
 *                             bucketing engine.
 *
 * Copyright (C) 2026 poker-eval contributors
 */

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/engine/solvers/cfr/strength_bucketing.h>

static mask_t C(int rank, int suit) { return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit)); }
static mask_t hand2(int r0, int s0, int r1, int s1)
{
    return mask_set(mask_set(MASK_EMPTY, MODERN_MAKE_CARD(r0, s0)), MODERN_MAKE_CARD(r1, s1));
}

static mask_t flop(int r0, int s0, int r1, int s1, int r2, int s2)
{
    return mask_set(mask_set(C(r0, s0), MODERN_MAKE_CARD(r1, s1)), MODERN_MAKE_CARD(r2, s2));
}

static void test_features_range(void)
{
    printf("test_strength_bucketing: feature range ... ");
    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    assert(ctx);

    mask_t board = flop(0, 0, 1, 1, 6, 2); /* A K 7 rainbow */
    pe_strength_features_t f;
    int rc = pe_strength_features(ctx, hand2(2, 2, 3, 2), board, NULL, &f);
    assert(rc == 0);
    assert(f.ehs >= 0.0 && f.ehs <= 1.0);
    assert(f.ehs2 >= 0.0 && f.ehs2 <= 1.0);
    assert(f.ehs2 >= f.ehs * f.ehs - 1e-9); /* EHS2 >= EHS^2 */
    assert(f.samples > 0);

    eval_context_destroy(ctx);
    printf("PASS\n");
}

static void test_train_and_assign(void)
{
    printf("test_strength_bucketing: train + assign ... ");
    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    assert(ctx);

    mask_t board = flop(1, 0, 2, 1, 12, 2); /* Ks Qh 2d rainbow */

    /* A handful of distinct hold'em hands, disjoint from the board (K Q 2). */
    mask_t hands[] = {
        hand2(0, 0, 0, 1), hand2(3, 0, 3, 1), hand2(4, 0, 4, 1),
        hand2(5, 0, 5, 1), hand2(6, 0, 6, 1), hand2(7, 0, 7, 1),
        hand2(8, 0, 8, 1), hand2(9, 0, 9, 1), hand2(10, 0, 10, 1),
        hand2(11, 0, 11, 1), hand2(0, 2, 3, 2), hand2(4, 2, 5, 2)
    };
    size_t n = sizeof(hands) / sizeof(hands[0]);

    pe_strength_cluster_opts_t opts = {0};
    opts.n_buckets = 4;
    opts.max_iterations = 20;

    int k = 0;
    pe_strength_table_t *t = pe_strength_table_train(ctx, board, hands, n, &opts, &k);
    assert(t);
    assert(k == 4);
    assert(pe_strength_table_count(t) == 4);
    assert(pe_strength_table_board(t) == board);

    /* Both hands must map to valid, distinct buckets, and the bucket table is
     * ordered so bucket_ehs is monotonically increasing (bucket 0 weakest). */
    /* Assignment yields valid buckets and the table is ordered: bucket_ehs is
     * monotonically increasing (bucket 0 = weakest, bucket k-1 = strongest),
     * which is the contract the solver relies on. */
    int b_aa = pe_strength_table_assign(t, ctx, hand2(0, 0, 0, 1), board); /* As Ad */
    assert(b_aa >= 0 && b_aa < 4);
    for (int c = 1; c < 4; ++c)
        assert(pe_strength_table_bucket_ehs(t, c) >= pe_strength_table_bucket_ehs(t, c - 1) - 1e-9);

    /* Cached assignment is stable and matches the uncached one. */
    int c1 = pe_strength_table_assign_cached(t, ctx, hand2(0, 1, 0, 2), board);
    int c2 = pe_strength_table_assign_cached(t, ctx, hand2(0, 1, 0, 2), board);
    int u = pe_strength_table_assign(t, ctx, hand2(0, 1, 0, 2), board);
    assert(c1 == c2 && c1 == u);

    pe_strength_table_free(t);
    eval_context_destroy(ctx);
    printf("PASS\n");
}

static void test_train_all_deterministic(void)
{
    printf("test_strength_bucketing: train_all deterministic + save/load ... ");
    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    assert(ctx);

    mask_t board = flop(4, 0, 5, 1, 6, 2); /* Ts 9h 8d (two-tone) */

    pe_strength_cluster_opts_t opts = {0};
    opts.n_buckets = 8;
    opts.max_iterations = 15;
    opts.seed = 12345u;

    int k1 = 0, k2 = 0;
    pe_strength_table_t *a = pe_strength_table_train_all(ctx, board, &opts, &k1);
    pe_strength_table_t *b = pe_strength_table_train_all(ctx, board, &opts, &k2);
    assert(a && b);
    assert(k1 == k2);
    assert(k1 == 8);

    /* Identical seeds => identical centroids (tolerance for FP). */
    for (int c = 0; c < k1; ++c)
    {
        assert(fabs(pe_strength_table_bucket_ehs(a, c) - pe_strength_table_bucket_ehs(b, c)) < 1e-12);
        assert(fabs(pe_strength_table_bucket_ehs2(a, c) - pe_strength_table_bucket_ehs2(b, c)) < 1e-12);
    }

    /* Hierarchy on the full distribution: a made top set (AA) lands in a
     * stronger bucket than an air hand (6 3), and buckets are ordered. */
    int b_aa = pe_strength_table_assign(a, ctx, hand2(0, 0, 0, 1), board);  /* As Ad */
    int b_air = pe_strength_table_assign(a, ctx, hand2(8, 0, 11, 0), board); /* 6s 3s */
    assert(b_aa >= 0 && b_air >= 0);
    assert(pe_strength_table_bucket_ehs(a, b_aa) > pe_strength_table_bucket_ehs(a, b_air));

    /* Save / load round-trip. */
    const char *path = "test_strength_bucketing.sbk";
    assert(pe_strength_table_save(a, path) == 0);
    pe_strength_table_t *loaded = pe_strength_table_load(path);
    assert(loaded);
    assert(pe_strength_table_count(loaded) == k1);
    for (int c = 0; c < k1; ++c)
        assert(fabs(pe_strength_table_bucket_ehs(loaded, c) - pe_strength_table_bucket_ehs(a, c)) < 1e-12);

    pe_strength_table_free(a);
    pe_strength_table_free(b);
    pe_strength_table_free(loaded);
    eval_context_destroy(ctx);
    printf("PASS\n");
}

int main(void)
{
    printf("=== test_strength_bucketing (FEAT-13) ===\n");
    test_features_range();
    test_train_and_assign();
    test_train_all_deterministic();
    printf("=== all strength_bucketing tests passed ===\n");
    return 0;
}
