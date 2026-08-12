/*
 * @file test_hand_clustering.c
 * @brief Tests for the learned k-means hand abstraction (FEAT-04)
 */

#include <poker_eval/engine/solvers/cfr/hand_clustering.h>
#include <poker_eval/engine/solvers/cfr/holdem_river_adapter.h>
#include <poker_eval/core/eval_context.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                  \
    do {                                  \
        if (!(cond)) {                    \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1;                     \
        }                                 \
    } while (0)

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

/* Ranks are 0-based (MODERN_RANK_2 = 0 .. MODERN_RANK_A = 12), suits 0..3.
 * Board: Ac Kd 7h 4s 2c - rainbow, no straight and no flush available, so a
 * hand's strength here is driven purely by its pairing with the board. */
static mask_t board_ak742(void)
{
    return card(MODERN_RANK_A, 0) | card(MODERN_RANK_K, 1) | card(MODERN_RANK_7, 2) |
           card(MODERN_RANK_4, 3) | card(MODERN_RANK_2, 0);
}

/* Ad Kh: two pair, aces and kings - near the top of the board's range. */
static mask_t hand_strong(void)
{
    return card(MODERN_RANK_A, 1) | card(MODERN_RANK_K, 2);
}

/* Th 9s: no pair, no draw - near the bottom. */
static mask_t hand_weak(void)
{
    return card(MODERN_RANK_T, 2) | card(MODERN_RANK_9, 3);
}

/* ------------------------------------------------------------------ *
 * Feature extraction
 * ------------------------------------------------------------------ */

static int test_features_ordering(EvalContext *ctx)
{
    mask_t board = board_ak742();

    /* Two pair with the ace kicker versus a hand that misses the board
     * completely: the strong hand must dominate on every feature. */
    mask_t strong = hand_strong();
    mask_t weak = hand_weak();

    pe_hand_features_t fs, fw;
    pe_hand_cluster_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.hole_cards = 2;

    CHECK(pe_hand_features(ctx, strong, board, &opts, &fs) == 0, "features(strong) failed");
    CHECK(pe_hand_features(ctx, weak, board, &opts, &fw) == 0, "features(weak) failed");

    CHECK(fs.samples > 0 && fw.samples > 0, "no matchups sampled");
    CHECK(fs.equity > fw.equity, "strong hand must have higher equity");
    CHECK(fs.hs2 > fw.hs2, "strong hand must have higher E[HS^2]");
    CHECK(fs.equity >= 0.0 && fs.equity <= 1.0, "equity out of range");
    CHECK(fs.hs2 >= 0.0 && fs.hs2 <= 1.0, "hs2 out of range");

    /* The histogram is a probability distribution. */
    double sum = 0.0;
    for (int b = 0; b < fs.n_bins; ++b)
        sum += fs.hist[b];
    CHECK(fabs(sum - 1.0) < 1e-9, "histogram must sum to 1");
    CHECK(fs.n_bins == PE_HS_DEFAULT_BINS, "default bin count expected");

    /* E[HS^2] >= E[HS]^2 by Jensen; equality only for a deterministic hand. */
    CHECK(fs.hs2 >= fs.equity * fs.equity - 1e-9, "hs2 must dominate equity^2");
    return 0;
}

static int test_features_rejects_bad_input(EvalContext *ctx)
{
    mask_t board = board_ak742();
    pe_hand_features_t f;

    CHECK(pe_hand_features(NULL, hand_strong(), board, NULL, &f) != 0,
          "NULL ctx must be rejected");
    CHECK(pe_hand_features(ctx, hand_strong(), board, NULL, NULL) != 0,
          "NULL out must be rejected");
    /* Hole card also on the board (Ac is the first board card). */
    CHECK(pe_hand_features(ctx, card(MODERN_RANK_A, 0) | card(MODERN_RANK_K, 2), board, NULL, &f) != 0,
          "overlapping hole/board must be rejected");
    /* Only one hole card. */
    CHECK(pe_hand_features(ctx, card(MODERN_RANK_A, 1), board, NULL, &f) != 0,
          "1-card hand must be rejected");
    /* Two-card board. */
    CHECK(pe_hand_features(ctx, hand_strong(), card(MODERN_RANK_4, 0) | card(MODERN_RANK_5, 1), NULL, &f) != 0,
          "2-card board must be rejected");
    return 0;
}

/* ------------------------------------------------------------------ *
 * Clustering
 * ------------------------------------------------------------------ */

static int test_train_monotonic(EvalContext *ctx)
{
    mask_t board = board_ak742();
    pe_hand_cluster_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.hole_cards = 2;
    opts.seed = 4242u;

    pe_bucket_table_t *t = pe_bucket_table_train_all(ctx, board, 8, &opts);
    CHECK(t != NULL, "train_all failed");
    CHECK(pe_bucket_table_count(t) == 8, "expected 8 clusters");
    CHECK(pe_bucket_table_board(t) == board, "board round-trip");

    /* Buckets are sorted by ascending mean equity, and every bucket is used. */
    double prev = -1.0;
    int total = 0;
    for (int b = 0; b < 8; ++b)
    {
        double eq = pe_bucket_table_cluster_equity(t, b);
        int sz = pe_bucket_table_cluster_size(t, b);
        CHECK(eq >= prev - 1e-12, "cluster equities must be non-decreasing");
        CHECK(sz > 0, "no cluster may be empty");
        prev = eq;
        total += sz;
    }
    /* C(47,2) = 1081 hands on a 5-card board. */
    CHECK(total == 1081, "every enumerated hand must be assigned");

    /* Out-of-range accessors are rejected, not UB. */
    CHECK(pe_bucket_table_cluster_equity(t, -1) < 0.0, "negative bucket rejected");
    CHECK(pe_bucket_table_cluster_equity(t, 8) < 0.0, "overflow bucket rejected");
    CHECK(pe_bucket_table_cluster_size(NULL, 0) < 0, "NULL table rejected");

    /* A strong hand must land in a strictly higher bucket than a weak one. */
    int b_strong = pe_bucket_table_assign(t, ctx, hand_strong(), board);
    int b_weak = pe_bucket_table_assign(t, ctx, hand_weak(), board);
    CHECK(b_strong >= 0 && b_weak >= 0, "assignment failed");
    CHECK(b_strong > b_weak, "strong hand must be in a higher bucket");

    /* The memoized path must agree with the direct one. */
    CHECK(pe_bucket_table_assign_cached(t, ctx, hand_strong(), board) == b_strong,
          "cached assignment must match");
    CHECK(pe_bucket_table_assign_cached(t, ctx, hand_strong(), board) == b_strong,
          "cache hit must match");
    CHECK(pe_bucket_table_assign_cached(t, ctx, hand_weak(), board) == b_weak,
          "cached assignment must match (weak)");

    pe_bucket_table_free(t);
    return 0;
}

static int test_train_deterministic(EvalContext *ctx)
{
    mask_t board = board_ak742();
    pe_hand_cluster_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.hole_cards = 2;
    opts.seed = 7u;

    pe_bucket_table_t *a = pe_bucket_table_train_all(ctx, board, 6, &opts);
    pe_bucket_table_t *b = pe_bucket_table_train_all(ctx, board, 6, &opts);
    CHECK(a && b, "train failed");
    CHECK(pe_bucket_table_count(a) == pe_bucket_table_count(b), "cluster count must match");
    for (int i = 0; i < pe_bucket_table_count(a); ++i)
    {
        CHECK(fabs(pe_bucket_table_cluster_equity(a, i) - pe_bucket_table_cluster_equity(b, i)) < 1e-12,
              "same seed must give the same centroids");
        CHECK(pe_bucket_table_cluster_size(a, i) == pe_bucket_table_cluster_size(b, i),
              "same seed must give the same populations");
    }
    pe_bucket_table_free(a);
    pe_bucket_table_free(b);
    return 0;
}

static int test_train_edge_cases(EvalContext *ctx)
{
    mask_t board = board_ak742();
    pe_hand_cluster_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.hole_cards = 2;
    opts.seed = 1u;

    /* k is clamped to at least 1 and to the hand count. */
    pe_bucket_table_t *t1 = pe_bucket_table_train_all(ctx, board, 0, &opts);
    CHECK(t1 && pe_bucket_table_count(t1) == 1, "k=0 must clamp to 1");
    pe_bucket_table_free(t1);

    mask_t hands[3];
    hands[0] = hand_strong();
    hands[1] = hand_weak();
    hands[2] = card(MODERN_RANK_Q, 1) | card(MODERN_RANK_Q, 2);
    pe_bucket_table_t *t2 = pe_bucket_table_train(ctx, board, hands, 3, 32, &opts);
    CHECK(t2 && pe_bucket_table_count(t2) == 3, "k must clamp to the hand count");
    pe_bucket_table_free(t2);

    CHECK(pe_bucket_table_train(ctx, board, hands, 0, 4, &opts) == NULL, "0 hands must fail");
    CHECK(pe_bucket_table_train(ctx, board, NULL, 3, 4, &opts) == NULL, "NULL hands must fail");
    CHECK(pe_bucket_table_train(NULL, board, hands, 3, 4, &opts) == NULL, "NULL ctx must fail");
    CHECK(pe_bucket_table_count(NULL) == 0, "NULL count is 0");
    pe_bucket_table_free(NULL); /* must not crash */
    return 0;
}

/* ------------------------------------------------------------------ *
 * Serialization
 * ------------------------------------------------------------------ */

static int test_save_load_roundtrip(EvalContext *ctx)
{
    mask_t board = board_ak742();
    pe_hand_cluster_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.hole_cards = 2;
    opts.seed = 99u;
    opts.n_bins = 6;

    pe_bucket_table_t *t = pe_bucket_table_train_all(ctx, board, 5, &opts);
    CHECK(t != NULL, "train failed");

    const char *path = "test_hand_clustering.pe_bkt";
    CHECK(pe_bucket_table_save(t, path) == 0, "save failed");

    pe_bucket_table_t *l = pe_bucket_table_load(path);
    CHECK(l != NULL, "load failed");
    CHECK(pe_bucket_table_count(l) == pe_bucket_table_count(t), "k must round-trip");
    CHECK(pe_bucket_table_board(l) == board, "board must round-trip");
    for (int i = 0; i < pe_bucket_table_count(t); ++i)
    {
        CHECK(fabs(pe_bucket_table_cluster_equity(l, i) - pe_bucket_table_cluster_equity(t, i)) < 1e-12,
              "cluster equity must round-trip exactly");
        CHECK(pe_bucket_table_cluster_size(l, i) == pe_bucket_table_cluster_size(t, i),
              "cluster size must round-trip");
    }

    /* A loaded table must reproduce the assignments of the trained one. */
    mask_t probe[4];
    probe[0] = hand_strong();
    probe[1] = hand_weak();
    probe[2] = card(MODERN_RANK_Q, 1) | card(MODERN_RANK_Q, 2);
    probe[3] = card(MODERN_RANK_6, 1) | card(MODERN_RANK_5, 2);
    for (int i = 0; i < 4; ++i)
    {
        int bt = pe_bucket_table_assign(t, ctx, probe[i], board);
        int bl = pe_bucket_table_assign(l, ctx, probe[i], board);
        CHECK(bt == bl, "loaded table must reproduce assignments");
    }

    pe_bucket_table_free(l);
    pe_bucket_table_free(t);
    remove(path);
    return 0;
}

static int test_load_rejects_bad_files(void)
{
    const char *path = "test_hand_clustering_bad.pe_bkt";

    /* Wrong magic. */
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "tmp file create failed");
    fwrite("NOTABKT!", 1, 8, f);
    for (int i = 0; i < 64; ++i)
        fputc(0, f);
    fclose(f);
    CHECK(pe_bucket_table_load(path) == NULL, "bad magic must be rejected");

    /* Right magic, wrong version. */
    f = fopen(path, "wb");
    CHECK(f != NULL, "tmp file create failed");
    fwrite("PEBKT001", 1, 8, f);
    unsigned char ver[4] = {99, 0, 0, 0};
    fwrite(ver, 1, 4, f);
    for (int i = 0; i < 64; ++i)
        fputc(0, f);
    fclose(f);
    CHECK(pe_bucket_table_load(path) == NULL, "bad version must be rejected");

    /* Truncated payload: valid header, no cluster data. */
    f = fopen(path, "wb");
    CHECK(f != NULL, "tmp file create failed");
    fwrite("PEBKT001", 1, 8, f);
    unsigned char v1[4] = {1, 0, 0, 0};
    fwrite(v1, 1, 4, f);
    unsigned char zero[4] = {0, 0, 0, 0};
    fwrite(zero, 1, 4, f);              /* flags */
    unsigned char k4[4] = {4, 0, 0, 0}; /* k = 4 */
    fwrite(k4, 1, 4, f);
    unsigned char bins[4] = {8, 0, 0, 0};
    fwrite(bins, 1, 4, f);
    unsigned char hole[4] = {2, 0, 0, 0};
    fwrite(hole, 1, 4, f);
    fwrite(zero, 1, 4, f); /* seed */
    fwrite(zero, 1, 4, f); /* max_samples */
    fwrite(zero, 1, 4, f); /* reserved */
    for (int i = 0; i < 8; ++i)
        fputc(0, f); /* board */
    for (int i = 0; i < 8; ++i)
        fputc(0, f); /* hist_weight */
    fclose(f);
    CHECK(pe_bucket_table_load(path) == NULL, "truncated payload must be rejected");

    CHECK(pe_bucket_table_load("definitely_missing_file.pe_bkt") == NULL, "missing file must fail");
    CHECK(pe_bucket_table_load(NULL) == NULL, "NULL path must fail");
    CHECK(pe_bucket_table_save(NULL, path) != 0, "NULL table must fail");

    remove(path);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Solver integration (bucket_mode 4)
 * ------------------------------------------------------------------ */

static size_t solve_and_count(EvalContext *ctx,
                              mask_t h0,
                              mask_t h1,
                              mask_t board,
                              int bucket_mode,
                              pe_bucket_table_t *table)
{
    cfr_game_t game;
    holdem_river_state_t st;
    hr_build_game(ctx, h0, h1, board, &game, &st);
    st.bucket_mode = bucket_mode;
    st.bucket_table = table;
    st.num_bet_sizes = 1;
    st.raise_cap = 1;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 100;

    cfr_storage_t *storage = cfr_storage_create();
    if (!storage)
        return 0;
    double expl = 0.0;
    cfr_solve(&game, storage, &cfg, &expl);
    size_t n = cfr_storage_count_infosets(storage);
    cfr_storage_destroy(storage);
    return n;
}

static int test_solver_integration(EvalContext *ctx)
{
    mask_t board = board_ak742();
    mask_t h0 = hand_strong();
    mask_t h1 = hand_weak();

    pe_hand_cluster_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.hole_cards = 2;
    opts.seed = 31337u;
    pe_bucket_table_t *t = pe_bucket_table_train_all(ctx, board, 8, &opts);
    CHECK(t != NULL, "train failed");

    size_t n3 = solve_and_count(ctx, h0, h1, board, 3, NULL);
    size_t n4 = solve_and_count(ctx, h0, h1, board, 4, t);
    CHECK(n3 > 0 && n4 > 0, "solve produced no infosets");

    /* bucket_mode 4 with a NULL table must degrade to the mode-3 abstraction
     * rather than failing or collapsing the tree. */
    size_t n4_null = solve_and_count(ctx, h0, h1, board, 4, NULL);
    CHECK(n4_null == n3, "mode 4 without a table must match mode 3");

    /* Modes 0..3 must be untouched by this feature: a fixed key layout check. */
    size_t n0 = solve_and_count(ctx, h0, h1, board, 0, t);
    size_t n1 = solve_and_count(ctx, h0, h1, board, 1, t);
    CHECK(n0 > 0 && n1 > 0, "legacy modes must still solve");
    CHECK(n0 == n1, "on a single deal, modes 0 and 1 share the action skeleton");

    pe_bucket_table_free(t);
    return 0;
}

int main(void)
{
    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
    {
        fprintf(stderr, "FAIL: eval_context_create\n");
        return 1;
    }

    int rc = 0;
    rc |= test_features_ordering(ctx);
    rc |= test_features_rejects_bad_input(ctx);
    rc |= test_train_monotonic(ctx);
    rc |= test_train_deterministic(ctx);
    rc |= test_train_edge_cases(ctx);
    rc |= test_save_load_roundtrip(ctx);
    rc |= test_load_rejects_bad_files();
    rc |= test_solver_integration(ctx);

    eval_context_destroy(ctx);
    if (rc == 0)
        printf("test_hand_clustering: all checks passed\n");
    return rc;
}
