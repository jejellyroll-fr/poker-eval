/*
 * test_mpf_ranges.c - RNG-02: the game model accepts a range per player
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Two claims, and the second is the one that matters.
 *
 * A configuration that names fixed hole cards must solve exactly as it did
 * before ranges existed. And a range holding one combo must solve exactly like
 * the fixed hand it describes — same value, same strategies, bit for bit. If
 * those two ever disagree, the range path is not the same path, and every
 * result RNG-03 produces on top of it would be measured against a different
 * solver than the one the oracles validated.
 *
 * A range with several combos is refused. The traversal has no root private
 * chance yet, so accepting one would mean solving its first combo while the
 * caller believed they had asked for a range — a wrong answer that looks like
 * a right one.
 */

#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/solver/pe_range.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static EvalContext *g_ctx = NULL;

/* Exact equality is the intent: the two configurations describe the same game,
   so the same arithmetic runs in the same order. A tolerance would let the
   range path diverge quietly. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
            g_failures++;                                      \
        }                                                      \
    } while (0)

static StdDeck_CardMask no_dead(void)
{
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    return m;
}

static mask_t mask_of(const char *cards)
{
    /* "AhKs" -> a two-card mask, through the range parser so the test and the
       code under test agree on what a card index means. */
    pe_range_t *r = NULL;
    mask_t m = MASK_EMPTY;
    int c;

    if (pe_solver_range_parse(game_holdem, cards, no_dead(), &r) != PE_SOLVER_OK)
        return MASK_EMPTY;
    for (c = 0; c < MODERN_DECK_SIZE; ++c)
        if (StdDeck_CardMask_CARD_IS_SET(r->combos[0].hand, c))
            m = mask_set(m, c);
    pe_range_free(r);
    return m;
}

/* A river spot: fixed board, two players, a handful of iterations. Small
   enough to compare exhaustively, real enough to exercise the adapter. */
static void configure(mpf_config_t *cfg, const EvalContext *ctx)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ctx = ctx;
    cfg->rules = MPF_RULE_HOLDEM;
    cfg->num_players = 2;
    cfg->button_index = 0;
    cfg->start_street = MPF_STREET_RIVER;
    cfg->board_cards[0] = 51; cfg->board_cards[1] = 46; cfg->board_cards[2] = 40;
    cfg->board_cards[3] = 33; cfg->board_cards[4] = 20;
    cfg->board_card_count = 5;
    cfg->stacks[0] = 100.0; cfg->stacks[1] = 100.0;
    cfg->sb = 0.5; cfg->bb = 1.0;
    cfg->bet_sizes_common[0] = 0.75;
    cfg->bet_size_count_common = 1;
    cfg->raise_cap = 1;
    cfg->enable_pot_sizing = 1;
    cfg->preflop.defined = 1;
    cfg->preflop.has_pot = 1;
    cfg->preflop.pot = 10.0;
}

/* Solve and capture every infoset, so "identical" means identical rather than
   "the root value agrees". */
typedef struct { uint64_t key; int n; double avg[8]; } row_t;
static row_t g_rows[4096];
static int g_nrows;

static void collect(uint64_t key, int n, const double *regret,
                    const double *avg, void *user)
{
    (void)regret; (void)user;
    if (g_nrows >= (int)(sizeof(g_rows) / sizeof(g_rows[0])))
        return;
    g_rows[g_nrows].key = key;
    g_rows[g_nrows].n = n;
    for (int i = 0; i < n && i < 8; ++i)
        g_rows[g_nrows].avg[i] = avg[i];
    g_nrows++;
}

static int solve_and_capture(const mpf_config_t *cfg, double *out_value,
                             row_t *out, int *out_n)
{
    cfr_game_t game;
    mpf_state_t root;
    cfr_config_t solve_cfg;
    cfr_storage_t *storage;

    if (mpf_build_game(cfg, &game, &root) != 0)
        return -1;

    memset(&solve_cfg, 0, sizeof(solve_cfg));
    solve_cfg.max_iterations = 40;
    solve_cfg.max_depth = 64;

    storage = cfr_storage_create();
    *out_value = cfr_solve(&game, storage, &solve_cfg, NULL);

    g_nrows = 0;
    cfr_storage_iterate(storage, collect, NULL);
    memcpy(out, g_rows, sizeof(row_t) * (size_t)g_nrows);
    *out_n = g_nrows;

    cfr_storage_destroy(storage);
    mpf_state_cleanup(&root);
    return 0;
}

static row_t g_fixed[4096];
static row_t g_ranged[4096];

static void test_single_combo_range_equals_fixed_hand(void)
{
    const EvalContext *ctx = g_ctx;
    mpf_config_t cfg;
    double v_fixed = 0.0, v_ranged = 0.0;
    int n_fixed = 0, n_ranged = 0;
    pe_range_t *r0 = NULL;
    pe_range_t *r1 = NULL;
    int i, j;

    /* Fixed hands. */
    configure(&cfg, ctx);
    cfg.hole[0] = mask_of("AhKh");
    cfg.hole[1] = mask_of("QsJs");
    cfg.hole_specified[0] = 1;
    cfg.hole_specified[1] = 1;
    CHECK(cfg.hole[0] != MASK_EMPTY && cfg.hole[1] != MASK_EMPTY, "mask setup failed");
    CHECK(solve_and_capture(&cfg, &v_fixed, g_fixed, &n_fixed) == 0, "fixed solve failed");

    /* The same two hands, expressed as one-combo ranges. */
    CHECK(pe_solver_range_parse(game_holdem, "AhKh", no_dead(), &r0) == PE_SOLVER_OK,
          "range 0 parse failed");
    CHECK(pe_solver_range_parse(game_holdem, "QsJs", no_dead(), &r1) == PE_SOLVER_OK,
          "range 1 parse failed");
    if (!r0 || !r1) { pe_range_free(r0); pe_range_free(r1); return; }
    CHECK(r0->count == 1 && r1->count == 1,
          "a specific hand should be one combo, got %zu and %zu", r0->count, r1->count);

    configure(&cfg, ctx);
    cfg.range[0] = r0;
    cfg.range[1] = r1;
    CHECK(solve_and_capture(&cfg, &v_ranged, g_ranged, &n_ranged) == 0,
          "ranged solve failed");

    /* Bit for bit, not to a tolerance: the two configurations describe the
       same game, so the same arithmetic runs in the same order. */
    CHECK(v_fixed == v_ranged,
          "root value differs: %.17g fixed against %.17g ranged", v_fixed, v_ranged);
    CHECK(n_fixed == n_ranged && n_fixed > 0,
          "infoset counts differ: %d fixed against %d ranged", n_fixed, n_ranged);

    for (i = 0; i < n_fixed && i < n_ranged; ++i)
    {
        CHECK(g_fixed[i].key == g_ranged[i].key,
              "infoset %d has a different key", i);
        CHECK(g_fixed[i].n == g_ranged[i].n, "infoset %d has a different width", i);
        for (j = 0; j < g_fixed[i].n && j < 8; ++j)
            if (g_fixed[i].avg[j] != g_ranged[i].avg[j])
            {
                CHECK(0, "infoset %d action %d: %.17g fixed against %.17g ranged",
                      i, j, g_fixed[i].avg[j], g_ranged[i].avg[j]);
                i = n_fixed;   /* one report is enough */
                break;
            }
    }

    pe_range_free(r0);
    pe_range_free(r1);
}

static void test_wide_range_is_refused(void)
{
    const EvalContext *ctx = g_ctx;
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t root;
    pe_range_t *wide = NULL;

    CHECK(pe_solver_range_parse(game_holdem, "AA", no_dead(), &wide) == PE_SOLVER_OK,
          "range parse failed");
    if (!wide) return;
    CHECK(wide->count == 6, "\"AA\" should hold 6 combos, got %zu", wide->count);

    configure(&cfg, ctx);
    cfg.hole[1] = mask_of("QsJs");
    cfg.hole_specified[1] = 1;
    cfg.range[0] = wide;

    /* Refused, not collapsed onto the first combo: solving one hand while the
       caller asked for six would be a wrong answer that looks right. */
    CHECK(mpf_build_game(&cfg, &game, &root) != 0,
          "a six-combo range was accepted before RNG-03");

    pe_range_free(wide);
}

static void test_unprepared_range_is_refused(void)
{
    const EvalContext *ctx = g_ctx;
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t root;
    pe_range_t *r = NULL;

    CHECK(pe_solver_range_parse(game_holdem, "AhKh", no_dead(), &r) == PE_SOLVER_OK,
          "parse failed");
    if (!r) return;

    /* Break the invariant the traversal is entitled to assume. */
    r->combos[0].weight = 0.5;

    configure(&cfg, ctx);
    cfg.hole[1] = mask_of("QsJs");
    cfg.hole_specified[1] = 1;
    cfg.range[0] = r;

    CHECK(mpf_build_game(&cfg, &game, &root) != 0,
          "an unnormalised range was accepted");

    pe_range_free(r);
}

int main(void)
{
    EvalConfig eval_cfg = eval_config_holdem();
    g_ctx = eval_context_create(&eval_cfg);
    if (g_ctx == NULL)
    {
        fprintf(stderr, "test_mpf_ranges: could not create an eval context\n");
        return 1;
    }

    test_single_combo_range_equals_fixed_hand();
    test_wide_range_is_refused();
    test_unprepared_range_is_refused();

    eval_context_destroy(g_ctx);

    if (g_failures != 0)
    {
        fprintf(stderr, "test_mpf_ranges: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_mpf_ranges: a one-combo range solves exactly like a fixed hand\n");
    return 0;
}
