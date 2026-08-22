/*
 * test_pe_blockers.c - RNG-05: card removal at terminal nodes
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The fast path is checked against the exhaustive pairing on real ranges and
 * real boards. That is the only check worth making: inclusion-exclusion is
 * easy to get almost right, and almost right here means a solve whose blocker
 * effects are slightly wrong everywhere — which does not crash, does not fail
 * an oracle at the root, and quietly changes every strategy in the tree.
 *
 * The two implementations are independent: one sums 52 accumulators and
 * corrects a double count, the other pairs every combo with every combo. They
 * agree or one of them is wrong.
 */

#include <poker_eval/solver/pe_blockers.h>
#include <poker_eval/solver/pe_range.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

static StdDeck_CardMask no_dead(void)
{
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    return m;
}

/* A prepared range, unpacked into the arrays the blocker code takes. */
typedef struct {
    mask_t *masks;
    double *reach;
    size_t n;
    pe_range_t *owner;
} unpacked_t;

static int unpack(const char *range_str, unpacked_t *out)
{
    pe_range_t *r = NULL;
    size_t i;
    int c;

    memset(out, 0, sizeof(*out));
    if (pe_solver_range_parse(game_holdem, range_str, no_dead(), &r) != PE_SOLVER_OK)
        return -1;

    out->owner = r;
    out->n = r->count;
    out->masks = (mask_t *)calloc(out->n, sizeof(mask_t));
    out->reach = (double *)calloc(out->n, sizeof(double));
    if (!out->masks || !out->reach)
        return -1;

    for (i = 0; i < out->n; ++i)
    {
        mask_t m = MASK_EMPTY;
        for (c = 0; c < 52; ++c)
            if (StdDeck_CardMask_CARD_IS_SET(r->combos[i].hand, c))
                m = mask_set(m, c);
        out->masks[i] = m;
        out->reach[i] = r->combos[i].weight;
    }
    return 0;
}

static void unpack_free(unpacked_t *u)
{
    free(u->masks);
    free(u->reach);
    pe_range_free(u->owner);
    memset(u, 0, sizeof(*u));
}

static mask_t board_of(const int *cards, int n)
{
    mask_t m = MASK_EMPTY;
    int i;
    for (i = 0; i < n; ++i)
        m = mask_set(m, cards[i]);
    return m;
}

/* ------------------------------------------------------------------ *
 * The two implementations agree
 * ------------------------------------------------------------------ */

static void compare_on(const char *label, const char *hero_str,
                       const char *opp_str, const int *board, int board_n)
{
    unpacked_t hero, opp;
    double *fast = NULL;
    double *slow = NULL;
    pe_blockers_path_t path = PE_BLOCKERS_PATH_NONE;
    mask_t dead;
    size_t i;
    double worst = 0.0;

    CHECK(unpack(hero_str, &hero) == 0, "%s: hero range failed", label);
    CHECK(unpack(opp_str, &opp) == 0, "%s: opponent range failed", label);
    if (!hero.masks || !opp.masks) { unpack_free(&hero); unpack_free(&opp); return; }

    fast = (double *)calloc(hero.n, sizeof(double));
    slow = (double *)calloc(hero.n, sizeof(double));
    dead = board_of(board, board_n);

    CHECK(pe_blockers_compatible_sum(hero.masks, hero.n, opp.masks, opp.reach,
                                     opp.n, dead, fast, &path) == PE_SOLVER_OK,
          "%s: accumulated path failed", label);
    CHECK(path == PE_BLOCKERS_PATH_ACCUMULATED,
          "%s: took the pairwise path on two-card hands", label);
    CHECK(pe_blockers_compatible_sum_pairwise(hero.masks, hero.n, opp.masks,
                                              opp.reach, opp.n, dead, slow)
              == PE_SOLVER_OK, "%s: pairwise path failed", label);

    for (i = 0; i < hero.n; ++i)
    {
        double d = fabs(fast[i] - slow[i]);
        if (d > worst)
            worst = d;
        if (d > 1e-12)
        {
            CHECK(0, "%s: combo %zu gives %.17g accumulated against %.17g paired",
                  label, i, fast[i], slow[i]);
            break;
        }
    }

    printf("    %-26s hero %4zu x opp %4zu combos, worst gap %.3e\n",
           label, hero.n, opp.n, worst);

    free(fast);
    free(slow);
    unpack_free(&hero);
    unpack_free(&opp);
}

static void test_agreement_on_several_boards(void)
{
    /* Three boards, chosen to move what the removal actually does: one that
       shares nothing with the ranges, one that takes a card out of both, and
       one that guts the hero range. */
    const int dry[5] = { 20, 15, 10, 5, 1 };
    const int shares_an_ace[5] = { 51, 20, 15, 10, 5 };
    const int two_aces[5] = { 51, 50, 20, 15, 10 };

    printf("  accumulated against exhaustive pairing:\n");
    compare_on("dry board, wide ranges", "AA,KK,QQ,AKs,AKo,JJ", "TT,99,AQs,KQs,87s",
               dry, 5);
    compare_on("board shares an ace", "AA,AKs,AQo", "KK,QQ,AJs", shares_an_ace, 5);
    compare_on("two aces on the board", "AA,KK", "AA,QQ", two_aces, 5);
    compare_on("overlapping ranges", "AA,KK,QQ,JJ,TT", "AA,KK,QQ,JJ,TT", dry, 5);
    compare_on("no board", "AKs,AKo,QQ", "AA,KK,AQs", dry, 0);
}

/* ------------------------------------------------------------------ *
 * The number the removal exists for
 * ------------------------------------------------------------------ */

static void test_aces_against_aces(void)
{
    /* Both sides hold "AA": six combos each, and for any hero combo exactly
       one opponent combo avoids its two aces. Uniform weights make that one
       combo worth 1/6 of the opponent range, and every other hero combo sees
       the same. If the removal were skipped, each would see the whole 1.0. */
    unpacked_t hero, opp;
    double out[16];
    pe_blockers_path_t path;
    size_t i;

    CHECK(unpack("AA", &hero) == 0, "hero failed");
    CHECK(unpack("AA", &opp) == 0, "opponent failed");
    if (!hero.masks || !opp.masks) { unpack_free(&hero); unpack_free(&opp); return; }
    CHECK(hero.n == 6 && opp.n == 6, "\"AA\" should hold 6 combos");

    CHECK(pe_blockers_compatible_sum(hero.masks, hero.n, opp.masks, opp.reach,
                                     opp.n, MASK_EMPTY, out, &path) == PE_SOLVER_OK,
          "failed");

    for (i = 0; i < hero.n; ++i)
        CHECK(fabs(out[i] - 1.0 / 6.0) < 1e-12,
              "combo %zu faces %.17g of the opponent range, expected 1/6 "
              "(1.0 would mean no removal at all)", i, out[i]);

    unpack_free(&hero);
    unpack_free(&opp);
}

static void test_dead_cards_remove_from_both_sides(void)
{
    unpacked_t hero, opp;
    double out[16];
    mask_t dead;
    size_t i;
    int held = 0;

    CHECK(unpack("AA", &hero) == 0, "hero failed");
    CHECK(unpack("KK", &opp) == 0, "opponent failed");
    if (!hero.masks || !opp.masks) { unpack_free(&hero); unpack_free(&opp); return; }

    /* One ace and one king on the board. */
    dead = mask_set(mask_set(MASK_EMPTY, 51), 50 - 13);

    CHECK(pe_blockers_compatible_sum(hero.masks, hero.n, opp.masks, opp.reach,
                                     opp.n, dead, out, NULL) == PE_SOLVER_OK, "failed");

    for (i = 0; i < hero.n; ++i)
    {
        if ((hero.masks[i] & dead) != 0)
            CHECK(out[i] == 0.0,
                  "a hero combo using a board card faces %.17g, expected 0", out[i]);
        else
            held++;
    }
    CHECK(held == 3, "with one ace on the board 3 hero combos remain, got %d", held);

    unpack_free(&hero);
    unpack_free(&opp);
}

/* ------------------------------------------------------------------ *
 * Degenerate inputs
 * ------------------------------------------------------------------ */

static void test_degenerate(void)
{
    mask_t m[2] = { 3u, 12u };
    double r[2] = { 0.5, 0.5 };
    double out[2];

    CHECK(pe_blockers_compatible_sum(NULL, 2, m, r, 2, 0, out, NULL)
              == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL hero");
    CHECK(pe_blockers_compatible_sum(m, 2, m, r, 2, 0, NULL, NULL)
              == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL out");
    CHECK(pe_blockers_compatible_sum(m, 0, m, r, 2, 0, out, NULL)
              == PE_SOLVER_ERR_INVALID_CONFIG, "empty hero");
    CHECK(pe_blockers_compatible_sum(m, 2, m, r, 0, 0, out, NULL)
              == PE_SOLVER_ERR_INVALID_CONFIG, "empty opponent");
}

static void test_wide_hands_take_the_exact_fallback(void)
{
    /* Four-card hands: the two-card inclusion-exclusion does not cover them,
       and the code says so by falling back rather than by being close. */
    mask_t hero[1];
    mask_t opp[2];
    double reach[2] = { 0.5, 0.5 };
    double out[1];
    pe_blockers_path_t path = PE_BLOCKERS_PATH_NONE;

    hero[0] = mask_set(mask_set(mask_set(mask_set(MASK_EMPTY, 0), 1), 2), 3);
    opp[0] = mask_set(mask_set(mask_set(mask_set(MASK_EMPTY, 4), 5), 6), 7);
    opp[1] = mask_set(mask_set(mask_set(mask_set(MASK_EMPTY, 0), 8), 9), 10);

    CHECK(pe_blockers_compatible_sum(hero, 1, opp, reach, 2, MASK_EMPTY, out, &path)
              == PE_SOLVER_OK, "failed");
    CHECK(path == PE_BLOCKERS_PATH_PAIRWISE,
          "four-card hands did not take the exact fallback");
    /* Only the first opponent hand avoids the hero's cards. */
    CHECK(fabs(out[0] - 0.5) < 1e-12, "faced %.17g, expected 0.5", out[0]);
}

/*
 * A two-card hero against four-card opponents.
 *
 * The width guard checks both sides, and an earlier version of this file only
 * exercised the hero one: with four cards on both sides, removing the opponent
 * check changed nothing because the hero check still caught it. Here only the
 * opponent guard can, and without it the fast path skips every opponent combo
 * as malformed and reports that the hero faces nothing at all.
 */
static void test_asymmetric_widths_take_the_fallback(void)
{
    mask_t hero[2];
    mask_t opp[2];
    double reach[2] = { 0.25, 0.75 };
    double out[2];
    pe_blockers_path_t path = PE_BLOCKERS_PATH_NONE;

    hero[0] = mask_set(mask_set(MASK_EMPTY, 0), 1);
    hero[1] = mask_set(mask_set(MASK_EMPTY, 4), 5);
    opp[0] = mask_set(mask_set(mask_set(mask_set(MASK_EMPTY, 8), 9), 10), 11);
    opp[1] = mask_set(mask_set(mask_set(mask_set(MASK_EMPTY, 0), 12), 13), 14);

    CHECK(pe_blockers_compatible_sum(hero, 2, opp, reach, 2, MASK_EMPTY, out, &path)
              == PE_SOLVER_OK, "failed");
    CHECK(path == PE_BLOCKERS_PATH_PAIRWISE,
          "four-card opponents did not force the exact fallback");

    /* Hero combo 0 uses card 0, which opponent combo 1 also uses. */
    CHECK(fabs(out[0] - 0.25) < 1e-12, "hero 0 faced %.17g, expected 0.25", out[0]);
    /* Hero combo 1 conflicts with neither. */
    CHECK(fabs(out[1] - 1.0) < 1e-12, "hero 1 faced %.17g, expected 1.0", out[1]);
}

int main(void)
{
    test_agreement_on_several_boards();
    test_aces_against_aces();
    test_dead_cards_remove_from_both_sides();
    test_degenerate();
    test_wide_hands_take_the_exact_fallback();
    test_asymmetric_widths_take_the_fallback();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_blockers: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_blockers: accumulated removal matches exhaustive pairing\n");
    return 0;
}
