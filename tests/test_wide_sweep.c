/*
 * test_wide_sweep.c - the wide inclusion-exclusion sweep of issue #332.
 *
 * pe_showdown_wide_mass() prices five- and six-card ranges by accumulating
 * opponent reach per card subset instead of visiting every pair. This test
 * holds it against the pairwise oracle, which sums the same products one pair
 * at a time, and pins the regression that found it: the sweep used to forget
 * the opponents stronger than every hero, because its table was only filled as
 * far as the hero cursor reached. That bug left the compatible mass short and
 * only showed when the two ranges had different maximum strengths.
 *
 * The cases run three strength ladders, because the bug only showed when the
 * opponents above every hero were a large part of the range: one where the
 * heroes are on top, one where heroes straddle the opponent range, and one
 * where every opponent is stronger than every hero.
 *
 * The weights are thirds and sevenths on purpose: unlike the benchmark's
 * dyadic weights, these are not exactly representable, so the test compares
 * with a tolerance instead of expecting the re-associated sum to be identical
 * bit for bit.
 */

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_showdown.h>
#include <poker_eval/solver/pe_vector.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_failures;

#define CHECK(cond, ...)                                                    \
    do                                                                      \
    {                                                                       \
        if (!(cond))                                                        \
        {                                                                   \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);          \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;

static uint32_t next32(void)
{
    g_rng = g_rng * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(g_rng >> 33);
}

/* A six- or five-card hand whose cards are all absent from `used`. */
static mask_t random_hand(mask_t used, unsigned hole_cards)
{
    mask_t mask = 0;
    unsigned need = hole_cards;

    while (need > 0u)
    {
        int card = (int)(next32() % 52u);
        if (mask_is_set(used, card) || mask_is_set(mask, card))
            continue;
        mask = mask_set(mask, card);
        --need;
    }
    return mask;
}

static double thirds(unsigned index)
{
    switch (index % 7u)
    {
    case 0u:
        return 0.0;
    case 1u:
        return 1.0 / 3.0;
    case 2u:
        return 2.0 / 3.0;
    case 3u:
        return 1.0 / 7.0;
    case 4u:
        return 3.0 / 7.0;
    case 5u:
        return 5.0 / 7.0;
    default:
        return 1.0;
    }
}

/* Strength ladders.  The tail the sweep used to drop is the set of opponents
 * above every hero, so the interesting cases are the ones where that set is
 * large: LADDER_HEROES_ABOVE fills the table as the walk reaches it,
 * LADDER_OPPONENTS_ABOVE makes the whole opponent range a tail, and
 * LADDER_MIXED has heroes both above and below the opponent range. */
#define LADDER_MIXED 0
#define LADDER_HEROES_ABOVE 1
#define LADDER_OPPONENTS_ABOVE 2

/* One case: `hero_n` heroes against `opp_n` opponents of the same width. */
static void run_case(unsigned hole_cards, size_t hero_n, size_t opp_n,
                     int ladder)
{
    mask_t *hero_masks = (mask_t *)calloc(hero_n, sizeof(*hero_masks));
    mask_t *opp_masks = (mask_t *)calloc(opp_n, sizeof(*opp_masks));
    int64_t *hero_strength = (int64_t *)calloc(hero_n, sizeof(*hero_strength));
    int64_t *opp_strength = (int64_t *)calloc(opp_n, sizeof(*opp_strength));
    double *hero_reach = (double *)calloc(hero_n, sizeof(*hero_reach));
    double *opp_reach = (double *)calloc(opp_n, sizeof(*opp_reach));
    double *mass = (double *)calloc(hero_n, sizeof(*mass));
    pe_value_vec_t sweep = {0};
    pe_value_vec_t pairwise = {0};
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    size_t i;
    size_t j;

    for (i = 0; i < hero_n; ++i)
    {
        hero_masks[i] = random_hand(0, hole_cards);
        hero_reach[i] = thirds((unsigned)i);
    }
    for (i = 0; i < opp_n; ++i)
    {
        opp_masks[i] = random_hand(0, hole_cards);
        opp_reach[i] = thirds((unsigned)i + 3u);
    }
    for (i = 0; i < opp_n; ++i)
        opp_strength[i] = ladder == LADDER_OPPONENTS_ABOVE
                              ? (int64_t)(i % 17u) * 1000 + 50000
                              : (int64_t)(i % 17u) * 1000 + (int64_t)(i % 5u);
    for (i = 0; i < hero_n; ++i)
        hero_strength[i] = ladder == LADDER_HEROES_ABOVE
                               ? (int64_t)(i % 13u) * 1100 + 90000
                               : (int64_t)(i % 13u) * 1100;

    CHECK(pe_vec_alloc(&sweep, hero_n) == PE_SOLVER_OK, "sweep alloc");
    CHECK(pe_vec_alloc(&pairwise, hero_n) == PE_SOLVER_OK, "pairwise alloc");
    CHECK(pe_showdown_wide_mass(hero_masks, hero_strength, hero_n, opp_masks,
                                opp_strength, opp_reach, opp_n, 0, 63.0,
                                hole_cards, &sweep, mass, &path) ==
              PE_SOLVER_OK,
          "wide sweep failed for %zu heroes / %zu opponents", hero_n, opp_n);
    CHECK(pe_showdown_vector_pairwise(hero_masks, hero_strength, hero_n,
                                      opp_masks, opp_strength, opp_reach, opp_n,
                                      0, 63.0, &pairwise) == PE_SOLVER_OK,
          "pairwise oracle failed");

    for (i = 0; i < hero_n; ++i)
    {
        double expected = 0.0;
        double scale = 0.0;

        for (j = 0; j < opp_n; ++j)
            if ((hero_masks[i] & opp_masks[j]) == 0)
                expected += opp_reach[j];
        for (j = 0; j < opp_n; ++j)
            scale += opp_reach[j];
        CHECK(fabs(mass[i] - expected) <= 1e-9 * (1.0 + scale),
              "%u holes, hero %zu: sweep mass %.17g, pairwise %.17g",
              hole_cards, i, mass[i], expected);
        CHECK(fabs(sweep.v[i] - pairwise.v[i]) <= 1e-9 * (1.0 + scale * 63.0),
              "%u holes, hero %zu: sweep value %.17g, pairwise %.17g",
              hole_cards, i, sweep.v[i], pairwise.v[i]);
    }
    CHECK(path == (hole_cards == 4u ? PE_SHOWDOWN_PATH_PLO4
                                    : PE_SHOWDOWN_PATH_PLO_WIDE),
          "%u holes reported path %d", hole_cards, (int)path);

    pe_vec_free(&sweep);
    pe_vec_free(&pairwise);
    free(mass);
    free(opp_reach);
    free(hero_reach);
    free(opp_strength);
    free(hero_strength);
    free(opp_masks);
    free(hero_masks);
}

int main(void)
{
    run_case(6u, 33, 38, LADDER_MIXED);
    run_case(6u, 38, 33, LADDER_HEROES_ABOVE);
    run_case(6u, 60, 60, LADDER_MIXED);
    run_case(6u, 33, 38, LADDER_OPPONENTS_ABOVE);
    run_case(5u, 34, 41, LADDER_MIXED);
    run_case(5u, 41, 34, LADDER_HEROES_ABOVE);
    run_case(5u, 34, 41, LADDER_OPPONENTS_ABOVE);
    run_case(4u, 40, 40, LADDER_MIXED);
    /* The four-card turn at 512 combos is the configuration the tail moved
       outside these unit cases; test_wide_sweep is what pins it here, as a
       whole opponent range above every hero. */
    run_case(4u, 40, 40, LADDER_OPPONENTS_ABOVE);
    run_case(4u, 24, 40, LADDER_OPPONENTS_ABOVE);

    if (g_failures != 0)
    {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("test_wide_sweep: ok\n");
    return 0;
}
