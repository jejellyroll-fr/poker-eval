/*
 * test_pe_range.c - RNG-01: the solver's invariants on a range
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The ticket asked for a range type. The repository already had one —
 * pe_combo_t and pe_range_t are exactly what architecture v3 §7.1 describes —
 * so what is tested here is the part that did not exist: the guarantee a
 * traversal relies on when it indexes combos by position thousands of times
 * per node.
 *
 * Non-empty, deduplicated, stably ordered, positively weighted, summing to 1.
 * Each of those is something the traversal will assume without checking, which
 * is precisely why they are checked once, here.
 */

#include <poker_eval/solver/pe_range.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

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

static double weight_sum(const pe_range_t *r)
{
    double t = 0.0;
    size_t i;
    for (i = 0; i < r->count; ++i)
        t += r->combos[i].weight;
    return t;
}

/* ------------------------------------------------------------------ *
 * The counts the ticket names
 * ------------------------------------------------------------------ */

static void expect_count(const char *str, size_t expected)
{
    pe_range_t *r = NULL;
    pe_solver_status_t st = pe_solver_range_parse(game_holdem, str, no_dead(), &r);

    CHECK(st == PE_SOLVER_OK, "\"%s\" was refused (%d)", str, (int)st);
    if (st != PE_SOLVER_OK) return;

    CHECK(r->count == expected, "\"%s\" holds %zu combos, expected %zu",
          str, r->count, expected);

    /* Every weight equal, and summing to 1: an unweighted range is uniform. */
    CHECK(fabs(weight_sum(r) - 1.0) < 1e-12,
          "\"%s\" weights sum to %.17g", str, weight_sum(r));
    if (r->count > 0)
    {
        double w0 = r->combos[0].weight;
        size_t i;
        for (i = 1; i < r->count; ++i)
            CHECK(fabs(r->combos[i].weight - w0) < 1e-12,
                  "\"%s\" combo %zu weighs %.17g, expected %.17g",
                  str, i, r->combos[i].weight, w0);
        CHECK(fabs(w0 - 1.0 / (double)expected) < 1e-12,
              "\"%s\" uniform weight is %.17g, expected %.17g",
              str, w0, 1.0 / (double)expected);
    }

    CHECK(pe_solver_range_is_prepared(r, 1e-12), "\"%s\" is not prepared", str);
    pe_range_free(r);
}

static void test_hand_counts(void)
{
    expect_count("AA", 6);       /* C(4,2) */
    expect_count("AKs", 4);      /* one per suit */
    expect_count("AKo", 12);     /* 4*4 - 4 */
    expect_count("AA,KK", 12);
}

/* ------------------------------------------------------------------ *
 * Refusals
 * ------------------------------------------------------------------ */

static void test_refusals(void)
{
    pe_range_t *r = (pe_range_t *)(uintptr_t)1;   /* poisoned */

    /* A string that reaches nothing is a rejected string, not an empty range
       the caller discovers three layers down. */
    CHECK(pe_solver_range_parse(game_holdem, "", no_dead(), &r) != PE_SOLVER_OK,
          "an empty string produced a range");
    CHECK(r == NULL, "a refused parse left a pointer behind");

    r = (pe_range_t *)(uintptr_t)1;
    CHECK(pe_solver_range_parse(game_holdem, "@@@", no_dead(), &r) != PE_SOLVER_OK,
          "nonsense produced a range");
    CHECK(r == NULL, "a refused parse left a pointer behind");

    CHECK(pe_solver_range_parse(game_holdem, NULL, no_dead(), &r)
              == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL string");
    CHECK(pe_solver_range_parse(game_holdem, "AA", no_dead(), NULL)
              == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL out");
    CHECK(pe_solver_range_prepare(NULL) == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL range");
}

/* ------------------------------------------------------------------ *
 * The invariants, on ranges built by hand
 * ------------------------------------------------------------------ */

static pe_range_t *hand_built(const pe_combo_t *src, size_t n)
{
    pe_range_t *r = NULL;
    size_t i;

    if (pe_solver_range_parse(game_holdem, "AA", no_dead(), &r) != PE_SOLVER_OK)
        return NULL;
    /* Reuse the allocation, overwrite the contents: building a pe_range_t from
       nothing is the range engine's business, not this test's. */
    for (i = 0; i < n && i < r->count; ++i)
        r->combos[i] = src[i];
    r->count = (n < r->count) ? n : r->count;
    return r;
}

static void test_duplicates_are_folded(void)
{
    pe_range_t *seed = NULL;
    pe_combo_t src[4];
    pe_range_t *r;
    double total;

    CHECK(pe_solver_range_parse(game_holdem, "AA", no_dead(), &seed) == PE_SOLVER_OK,
          "seed parse failed");
    if (!seed) return;

    /* Two distinct hands, one of them listed twice with different weights. */
    src[0] = seed->combos[0]; src[0].weight = 0.25;
    src[1] = seed->combos[1]; src[1].weight = 0.25;
    src[2] = seed->combos[0]; src[2].weight = 0.50;   /* duplicate of [0] */
    src[3] = seed->combos[1]; src[3].weight = 0.00;   /* dropped */
    pe_range_free(seed);

    r = hand_built(src, 4);
    CHECK(r != NULL, "hand-built range failed");
    if (!r) return;

    CHECK(pe_solver_range_prepare(r) == PE_SOLVER_OK, "prepare failed");
    CHECK(r->count == 2, "duplicates left %zu combos, expected 2", r->count);

    total = weight_sum(r);
    CHECK(fabs(total - 1.0) < 1e-12, "weights sum to %.17g", total);

    /* 0.25 + 0.50 against 0.25 + 0.00, normalised: 0.75 and 0.25. */
    {
        double a = r->combos[0].weight, b = r->combos[1].weight;
        double hi = (a > b) ? a : b, lo = (a > b) ? b : a;
        CHECK(fabs(hi - 0.75) < 1e-12, "folded weight is %.17g, expected 0.75", hi);
        CHECK(fabs(lo - 0.25) < 1e-12, "other weight is %.17g, expected 0.25", lo);
    }

    CHECK(pe_solver_range_is_prepared(r, 1e-12), "not prepared after preparing");
    pe_range_free(r);
}

static void test_preparation_is_idempotent(void)
{
    pe_range_t *r = NULL;
    double first[16];
    size_t n, i;

    CHECK(pe_solver_range_parse(game_holdem, "AA,KK,AKs", no_dead(), &r) == PE_SOLVER_OK,
          "parse failed");
    if (!r) return;

    n = (r->count < 16) ? r->count : 16;
    for (i = 0; i < n; ++i)
        first[i] = r->combos[i].weight;

    /* Preparing again must change nothing: the traversal may re-enter setup
       between iterations, and a second normalisation that shifted weights
       would move the solve without anyone asking. */
    CHECK(pe_solver_range_prepare(r) == PE_SOLVER_OK, "second prepare failed");
    for (i = 0; i < n; ++i)
        CHECK(r->combos[i].weight == first[i],
              "combo %zu moved from %.17g to %.17g", i, first[i], r->combos[i].weight);

    pe_range_free(r);
}

static void test_bad_weights_are_refused(void)
{
    pe_range_t *seed = NULL;
    pe_combo_t src[2];
    pe_range_t *r;

    CHECK(pe_solver_range_parse(game_holdem, "AA", no_dead(), &seed) == PE_SOLVER_OK,
          "seed parse failed");
    if (!seed) return;
    src[0] = seed->combos[0];
    src[1] = seed->combos[1];
    pe_range_free(seed);

    /* Negative: refused rather than clamped. A clamp would hide a caller's
       arithmetic error inside a solve whose numbers still look plausible. */
    src[0].weight = -0.5; src[1].weight = 1.0;
    r = hand_built(src, 2);
    CHECK(pe_solver_range_prepare(r) == PE_SOLVER_ERR_INVALID_CONFIG,
          "a negative weight was accepted");
    pe_range_free(r);

    /* All zero: nothing to normalise against. */
    src[0].weight = 0.0; src[1].weight = 0.0;
    r = hand_built(src, 2);
    CHECK(pe_solver_range_prepare(r) == PE_SOLVER_ERR_INVALID_CONFIG,
          "an all-zero range was accepted");
    pe_range_free(r);
}

static void test_order_is_stable(void)
{
    pe_range_t *a = NULL;
    pe_range_t *b = NULL;
    size_t i;

    /* The same set written in two orders must come out identical: a combo
       index is what a checkpoint stores and what a device buffer is addressed
       by, so it cannot depend on how the string was typed. */
    CHECK(pe_solver_range_parse(game_holdem, "AA,KK,QQ", no_dead(), &a) == PE_SOLVER_OK,
          "parse a failed");
    CHECK(pe_solver_range_parse(game_holdem, "QQ,KK,AA", no_dead(), &b) == PE_SOLVER_OK,
          "parse b failed");
    if (!a || !b) { pe_range_free(a); pe_range_free(b); return; }

    CHECK(a->count == b->count, "counts differ: %zu vs %zu", a->count, b->count);
    for (i = 0; i < a->count && i < b->count; ++i)
    {
        CHECK(memcmp(&a->combos[i].hand, &b->combos[i].hand,
                     sizeof(StdDeck_CardMask)) == 0,
              "combo %zu differs between the two orderings", i);
        CHECK(a->combos[i].weight == b->combos[i].weight,
              "combo %zu weighs differently", i);
    }

    pe_range_free(a);
    pe_range_free(b);
}

static void test_view(void)
{
    pe_range_t *r = NULL;
    pe_range_view_t v;

    CHECK(pe_solver_range_parse(game_holdem, "AA", no_dead(), &r) == PE_SOLVER_OK,
          "parse failed");
    v = pe_solver_range_view(r);
    CHECK(v.count == 6 && v.combos == r->combos, "the view does not borrow");

    v = pe_solver_range_view(NULL);
    CHECK(v.combos == NULL && v.count == 0, "a view of nothing is not empty");
    pe_range_free(r);
}

int main(void)
{
    test_hand_counts();
    test_refusals();
    test_duplicates_are_folded();
    test_preparation_is_idempotent();
    test_bad_weights_are_refused();
    test_order_is_stable();
    test_view();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_range: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_range: ranges are deduplicated, ordered and normalised\n");
    return 0;
}
