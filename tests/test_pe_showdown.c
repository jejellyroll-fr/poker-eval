/*
 * test_pe_showdown.c - VEC-06: sorted vector showdown
 */

#include <poker_eval/solver/pe_showdown.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);       \
            fprintf(stderr, __VA_ARGS__);                                 \
            fputc('\n', stderr);                                           \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static mask_t hand(int first, int second)
{
    return mask_set(mask_set(MASK_EMPTY, first), second);
}

static void compare_case(const char *label,
                         const mask_t *hero_masks,
                         const int64_t *hero_strength,
                         size_t hero_n,
                         const mask_t *opp_masks,
                         const int64_t *opp_strength,
                         const double *opp_reach,
                         size_t opp_n)
{
    const mask_t boards[] = {
        MASK_EMPTY,
        mask_set(MASK_EMPTY, 0),
        mask_set(MASK_EMPTY, 4),
        mask_set(MASK_EMPTY, 7),
        mask_set(mask_set(MASK_EMPTY, 2), 8)
    };
    size_t board_index;

    for (board_index = 0; board_index < sizeof(boards) / sizeof(boards[0]);
         ++board_index)
    {
        pe_value_vec_t sorted = {0};
        pe_value_vec_t pairwise = {0};
        pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
        size_t i;

        CHECK(pe_vec_alloc(&sorted, hero_n) == PE_SOLVER_OK,
              "%s board %zu: sorted allocation failed", label, board_index);
        CHECK(pe_vec_alloc(&pairwise, hero_n) == PE_SOLVER_OK,
              "%s board %zu: pairwise allocation failed", label, board_index);
        if (!sorted.v || !pairwise.v)
        {
            pe_vec_free(&sorted);
            pe_vec_free(&pairwise);
            continue;
        }

        CHECK(pe_showdown_vector(hero_masks, hero_strength, hero_n,
                                 opp_masks, opp_strength, opp_reach, opp_n,
                                 boards[board_index], 3.5, &sorted, &path)
                  == PE_SOLVER_OK,
              "%s board %zu: sorted showdown failed", label, board_index);
        CHECK(path == PE_SHOWDOWN_PATH_SORTED,
              "%s board %zu: did not use sorted path", label, board_index);
        CHECK(pe_showdown_vector_pairwise(hero_masks, hero_strength, hero_n,
                                          opp_masks, opp_strength, opp_reach,
                                          opp_n, boards[board_index], 3.5,
                                          &pairwise) == PE_SOLVER_OK,
              "%s board %zu: pairwise showdown failed", label, board_index);
        for (i = 0; i < hero_n; ++i)
            CHECK(fabs(sorted.v[i] - pairwise.v[i]) <= 1e-12,
                  "%s board %zu combo %zu: sorted %.17g pairwise %.17g",
                  label, board_index, i, sorted.v[i], pairwise.v[i]);

        pe_vec_free(&sorted);
        pe_vec_free(&pairwise);
    }
}

static void test_multiple_ranges_and_boards(void)
{
    const mask_t hero_masks[] = {
        hand(0, 1), hand(0, 2), hand(0, 3),
        hand(1, 2), hand(1, 3), hand(2, 3)
    };
    const mask_t opp_masks[] = {
        hand(0, 4), hand(1, 4), hand(2, 4),
        hand(3, 4), hand(5, 6), hand(7, 8)
    };
    const int64_t hero_strength_a[] = {10, 20, 30, 40, 50, 60};
    const int64_t opp_strength_a[] = {15, 25, 35, 45, 55, 60};
    const int64_t hero_strength_b[] = {60, 10, 50, 20, 40, 30};
    const int64_t opp_strength_b[] = {60, 30, 20, 50, 10, 40};
    const int64_t hero_strength_c[] = {7, 7, 9, 12, 12, 15};
    const int64_t opp_strength_c[] = {7, 8, 9, 12, 14, 15};
    const double reach_a[] = {0.10, 0.20, 0.30, 0.15, 0.05, 0.20};
    const double reach_b[] = {0.35, 0.05, 0.15, 0.25, 0.10, 0.10};
    const double reach_c[] = {0.05, 0.15, 0.25, 0.20, 0.30, 0.05};

    compare_case("range A", hero_masks, hero_strength_a, 6,
                 opp_masks, opp_strength_a, reach_a, 6);
    compare_case("range B", hero_masks, hero_strength_b, 6,
                 opp_masks, opp_strength_b, reach_b, 6);
    compare_case("range C with ties", hero_masks, hero_strength_c, 6,
                 opp_masks, opp_strength_c, reach_c, 6);
}

static void test_wide_hand_fallback_and_invalid_inputs(void)
{
    const mask_t hero_masks[] = {mask_set(mask_set(mask_set(MASK_EMPTY, 0), 1), 2)};
    const mask_t opp_masks[] = {hand(3, 4), hand(0, 5)};
    const int64_t hero_strength[] = {20};
    const int64_t opp_strength[] = {10, 30};
    const double reach[] = {0.25, 0.75};
    pe_value_vec_t values;
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;

    CHECK(pe_vec_alloc(&values, 1) == PE_SOLVER_OK, "fallback allocation failed");
    if (!values.v)
        return;
    CHECK(pe_showdown_vector(hero_masks, hero_strength, 1, opp_masks,
                             opp_strength, reach, 2, MASK_EMPTY, 2.0,
                             &values, &path) == PE_SOLVER_OK,
          "wide-hand fallback failed");
    CHECK(path == PE_SHOWDOWN_PATH_PAIRWISE, "wide hand did not use fallback");
    CHECK(fabs(values.v[0] - 0.5) <= 1e-12,
          "wide-hand value is %.17g, expected 0.5", values.v[0]);
    CHECK(pe_showdown_vector(NULL, hero_strength, 1, opp_masks, opp_strength,
                             reach, 2, MASK_EMPTY, 1.0, &values, NULL)
              == PE_SOLVER_ERR_NULL_ARGUMENT,
          "NULL hero masks accepted");
    CHECK(pe_showdown_vector_pairwise(hero_masks, hero_strength, 1,
                                      opp_masks, opp_strength, reach, 2,
                                      MASK_EMPTY, INFINITY, &values)
              == PE_SOLVER_ERR_INVALID_CONFIG,
          "infinite pot accepted by pairwise showdown");
    CHECK(pe_showdown_vector_pairwise(hero_masks, hero_strength, 1,
                                      opp_masks, opp_strength,
                                      (const double[]){INFINITY, 0.75}, 2,
                                      MASK_EMPTY, 1.0, &values)
              == PE_SOLVER_ERR_INVALID_CONFIG,
          "infinite opponent reach accepted by pairwise showdown");
    pe_vec_free(&values);
}

static void test_three_player_showdown_and_fold(void)
{
    const mask_t masks0[] = {hand(0, 1), hand(2, 3), hand(3, 4)};
    const mask_t masks1[] = {hand(5, 6), hand(0, 7), hand(8, 9)};
    const mask_t masks2[] = {hand(10, 11), hand(1, 12), hand(13, 14)};
    const int64_t strength0[] = {30, 20, 40};
    const int64_t strength1[] = {25, 35, 15};
    const int64_t strength2[] = {30, 10, 45};
    const double reach0[] = {0.40, 0.35, 0.25};
    const double reach1[] = {0.30, 0.45, 0.25};
    const double reach2[] = {0.50, 0.20, 0.30};
    const pe_showdown_player_t players[] = {
        {masks0, strength0, reach0, 3},
        {masks1, strength1, reach1, 3},
        {masks2, strength2, reach2, 3}
    };
    const mask_t dead = mask_set(MASK_EMPTY, 4);
    pe_value_vec_t values[3] = {{0}};
    pe_value_vec_t fold_values[3] = {{0}};
    double expected[3][3] = {{0}};
    double expected_fold[3] = {0};
    double joint_mass = 0.0;
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    const pe_showdown_sidepot_t sidepots[] = {
        {2.0, 0x07u},
        {1.0, 0x03u}
    };
    size_t c0;
    size_t c1;
    size_t c2;
    unsigned winners;
    int64_t best;
    double weighted_total = 0.0;

    for (c0 = 0; c0 < 3u; ++c0)
    {
        for (c1 = 0; c1 < 3u; ++c1)
        {
            for (c2 = 0; c2 < 3u; ++c2)
            {
                mask_t used = dead;
                double mass;

                if ((masks0[c0] & used) != 0)
                    continue;
                used |= masks0[c0];
                if ((masks1[c1] & used) != 0)
                    continue;
                used |= masks1[c1];
                if ((masks2[c2] & used) != 0)
                    continue;
                mass = reach0[c0] * reach1[c1] * reach2[c2];
                joint_mass += mass;
                best = strength0[c0];
                if (strength1[c1] > best) best = strength1[c1];
                if (strength2[c2] > best) best = strength2[c2];
                winners = (strength0[c0] == best) +
                          (strength1[c1] == best) +
                          (strength2[c2] == best);
                if (strength0[c0] == best)
                    expected[0][c0] += 3.0 * mass / (winners * reach0[c0]);
                if (strength1[c1] == best)
                    expected[1][c1] += 3.0 * mass / (winners * reach1[c1]);
                if (strength2[c2] == best)
                    expected[2][c2] += 3.0 * mass / (winners * reach2[c2]);
            }
        }
    }

    for (c0 = 0; c0 < 3u; ++c0)
        for (c1 = 0; c1 < 3u; ++c1)
            for (c2 = 0; c2 < 3u; ++c2)
            {
                mask_t used = dead;
                if ((masks0[c0] & used) != 0) continue;
                used |= masks0[c0];
                if ((masks1[c1] & used) != 0) continue;
                used |= masks1[c1];
                if ((masks2[c2] & used) != 0) continue;
                expected_fold[c0] += 3.0 * reach1[c1] * reach2[c2];
            }

    CHECK(pe_vec_alloc(&values[0], 3) == PE_SOLVER_OK, "multiway out 0 alloc");
    CHECK(pe_vec_alloc(&values[1], 3) == PE_SOLVER_OK, "multiway out 1 alloc");
    CHECK(pe_vec_alloc(&values[2], 3) == PE_SOLVER_OK, "multiway out 2 alloc");
    CHECK(pe_vec_alloc(&fold_values[0], 3) == PE_SOLVER_OK, "fold out 0 alloc");
    CHECK(pe_vec_alloc(&fold_values[1], 3) == PE_SOLVER_OK, "fold out 1 alloc");
    CHECK(pe_vec_alloc(&fold_values[2], 3) == PE_SOLVER_OK, "fold out 2 alloc");
    if (!values[0].v || !values[1].v || !values[2].v || !fold_values[0].v ||
        !fold_values[1].v || !fold_values[2].v)
        goto cleanup;

    CHECK(pe_showdown_multiway_vector(players, 3, dead, INFINITY, values,
                                      &path) == PE_SOLVER_ERR_INVALID_CONFIG,
          "infinite multiway pot accepted");
    {
        const pe_showdown_sidepot_t invalid_sidepot[] = {
            {INFINITY, 0x07u}
        };
        CHECK(pe_showdown_multiway_sidepots(players, 3, dead,
                                            invalid_sidepot, 1, values,
                                            &path) == PE_SOLVER_ERR_INVALID_CONFIG,
              "infinite side-pot amount accepted");
    }
    CHECK(pe_fold_multiway_vector(players, 3, 0, dead, INFINITY, fold_values,
                                  &path) == PE_SOLVER_ERR_INVALID_CONFIG,
          "infinite fold pot accepted");

    CHECK(pe_showdown_multiway_vector(players, 3, dead, 3.0, values, &path)
              == PE_SOLVER_OK, "multiway showdown failed");
    CHECK(path == PE_SHOWDOWN_PATH_MULTIWAY, "multiway path not reported");
    for (c0 = 0; c0 < 3u; ++c0)
    {
        CHECK(fabs(values[0].v[c0] - expected[0][c0]) <= 1e-10,
              "player 0 combo %zu is %.17g, expected %.17g",
              c0, values[0].v[c0], expected[0][c0]);
        CHECK(fabs(values[1].v[c0] - expected[1][c0]) <= 1e-10,
              "player 1 combo %zu is %.17g, expected %.17g",
              c0, values[1].v[c0], expected[1][c0]);
        CHECK(fabs(values[2].v[c0] - expected[2][c0]) <= 1e-10,
              "player 2 combo %zu is %.17g, expected %.17g",
              c0, values[2].v[c0], expected[2][c0]);
        weighted_total += reach0[c0] * values[0].v[c0];
        weighted_total += reach1[c0] * values[1].v[c0];
        weighted_total += reach2[c0] * values[2].v[c0];
    }
    CHECK(fabs(weighted_total - 3.0 * joint_mass) <= 1e-10,
          "conservation is %.17g, expected %.17g",
          weighted_total, 3.0 * joint_mass);

    CHECK(pe_showdown_multiway_sidepots(players, 3, dead, sidepots, 2,
                                        values, &path) == PE_SOLVER_OK,
          "side-pot showdown failed");
    weighted_total = 0.0;
    for (c0 = 0; c0 < 3u; ++c0)
    {
        weighted_total += reach0[c0] * values[0].v[c0];
        weighted_total += reach1[c0] * values[1].v[c0];
        weighted_total += reach2[c0] * values[2].v[c0];
    }
    CHECK(fabs(weighted_total - 3.0 * joint_mass) <= 1e-10,
          "side-pot conservation is %.17g, expected %.17g",
          weighted_total, 3.0 * joint_mass);

    CHECK(pe_fold_multiway_vector(players, 3, 0, dead, 3.0, fold_values, &path)
              == PE_SOLVER_OK, "multiway fold failed");
    CHECK(path == PE_SHOWDOWN_PATH_MULTIWAY, "multiway fold path not reported");
    for (c0 = 0; c0 < 3u; ++c0)
    {
        CHECK(fabs(fold_values[0].v[c0] - expected_fold[c0]) <= 1e-10,
              "fold combo %zu is %.17g, expected %.17g",
              c0, fold_values[0].v[c0], expected_fold[c0]);
        CHECK(fold_values[1].v[c0] == 0.0 && fold_values[2].v[c0] == 0.0,
              "fold wrote values for a non-selected player");
    }

cleanup:
    pe_vec_free(&values[0]);
    pe_vec_free(&values[1]);
    pe_vec_free(&values[2]);
    pe_vec_free(&fold_values[0]);
    pe_vec_free(&fold_values[1]);
    pe_vec_free(&fold_values[2]);
}

/* --------------------------------------------------------------------------
 * PLO4: the blocker-aware sweep must agree with the pairwise oracle.
 * ------------------------------------------------------------------------ */

static uint32_t plo4_random_state;

static uint32_t plo4_random_next(void)
{
    plo4_random_state = plo4_random_state * 1664525u + 1013904223u;
    return plo4_random_state;
}

static mask_t random_hand(int hole)
{
    mask_t hand = MASK_EMPTY;

    while (mask_popcount(hand) < hole)
        hand = mask_set(hand, (int)(plo4_random_next() % 52u));
    return hand;
}

static void fill_random_hands(mask_t *masks, size_t n, int hole, uint64_t seed)
{
    size_t i;

    plo4_random_state = (uint32_t)seed;
    for (i = 0; i < n; ++i)
        masks[i] = random_hand(hole);
}

static void test_plo4_matches_pairwise(void)
{
    enum { HERO_N = 200, OPP_N = 2000 };
    mask_t *hero_masks = (mask_t *)malloc(HERO_N * sizeof(*hero_masks));
    mask_t *opp_masks = (mask_t *)malloc(OPP_N * sizeof(*opp_masks));
    int64_t *hero_strength = (int64_t *)malloc(HERO_N * sizeof(*hero_strength));
    int64_t *opp_strength = (int64_t *)malloc(OPP_N * sizeof(*opp_strength));
    double *opp_reach = (double *)malloc(OPP_N * sizeof(*opp_reach));
    const mask_t dead_cards[] = {
        MASK_EMPTY,
        mask_set(MASK_EMPTY, 0),
        mask_set(mask_set(MASK_EMPTY, 5), 51)
    };
    size_t i;

    CHECK(hero_masks && opp_masks && hero_strength && opp_strength && opp_reach,
          "PLO4 fixture allocation failed");
    if (!hero_masks || !opp_masks || !hero_strength || !opp_strength || !opp_reach)
        goto cleanup;

    fill_random_hands(hero_masks, HERO_N, 4, UINT64_C(0x51A0));
    fill_random_hands(opp_masks, OPP_N, 4, UINT64_C(0xB0B0));
    for (i = 0; i < HERO_N; ++i)
        hero_strength[i] = (int64_t)((i * 37u) % 23u) - 5;
    for (i = 0; i < OPP_N; ++i)
    {
        opp_strength[i] = (int64_t)((i * 53u) % 29u) - 6;
        /* Zero reach and fractional weights, so the sweep is not measured on
           an all-live range. */
        opp_reach[i] = (i % 13u) == 0u
                           ? 0.0
                           : 0.05 + (double)((i * 29u) % 17u) / 20.0;
    }
    /* Duplicated opponent hands at different reaches: the sweep must count
       each entry, not each distinct mask. */
    for (i = OPP_N / 2u; i < OPP_N; i += 7u)
        opp_masks[i] = opp_masks[i / 2u];

    for (i = 0; i < sizeof(dead_cards) / sizeof(dead_cards[0]); ++i)
    {
        pe_value_vec_t sweep = {0};
        pe_value_vec_t pairwise = {0};
        pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
        size_t combo;
        size_t mismatches = 0u;

        CHECK(pe_vec_alloc(&sweep, HERO_N) == PE_SOLVER_OK,
              "PLO4 sweep allocation failed");
        CHECK(pe_vec_alloc(&pairwise, HERO_N) == PE_SOLVER_OK,
              "PLO4 pairwise allocation failed");
        if (!sweep.v || !pairwise.v)
        {
            pe_vec_free(&sweep);
            pe_vec_free(&pairwise);
            continue;
        }
        CHECK(pe_showdown_vector(hero_masks, hero_strength, HERO_N, opp_masks,
                                 opp_strength, opp_reach, OPP_N, dead_cards[i],
                                 7.5, &sweep, &path) == PE_SOLVER_OK,
              "PLO4 dead %zu: sweep failed", i);
        CHECK(path == PE_SHOWDOWN_PATH_PLO4,
              "PLO4 dead %zu: path %d is not the sweep", i, (int)path);
        CHECK(pe_showdown_vector_pairwise(
                  hero_masks, hero_strength, HERO_N, opp_masks, opp_strength,
                  opp_reach, OPP_N, dead_cards[i], 7.5, &pairwise) ==
                  PE_SOLVER_OK,
              "PLO4 dead %zu: pairwise failed", i);
        /* Relative tolerance: the totals scale with the range size, so an
           absolute bound would fail on large masses that agree to 1e-14. */
        for (combo = 0; combo < HERO_N; ++combo)
            if (fabs(sweep.v[combo] - pairwise.v[combo]) >
                1e-12 * (1.0 + fabs(pairwise.v[combo])))
            {
                if (mismatches < 3u)
                    fprintf(stderr,
                            "  dead %zu combo %zu: sweep %.17g pairwise %.17g\n",
                            i, combo, sweep.v[combo], pairwise.v[combo]);
                ++mismatches;
            }
        CHECK(mismatches == 0u, "PLO4 dead %zu: %zu combos disagree", i,
              mismatches);
        pe_vec_free(&sweep);
        pe_vec_free(&pairwise);
    }

    /* Measured on the same ranges, so the speedup claim has a number behind
       it. Printed rather than asserted: wall-clock ratios do not belong in a
       pass/fail test. */
    {
        pe_value_vec_t sweep = {0};
        pe_value_vec_t pairwise = {0};
        pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
        clock_t start;
        double sweep_ms;
        double pairwise_ms;

        if (pe_vec_alloc(&sweep, HERO_N) == PE_SOLVER_OK &&
            pe_vec_alloc(&pairwise, HERO_N) == PE_SOLVER_OK)
        {
            start = clock();
            (void)pe_showdown_vector(hero_masks, hero_strength, HERO_N,
                                     opp_masks, opp_strength, opp_reach,
                                     OPP_N, MASK_EMPTY, 7.5, &sweep, &path);
            sweep_ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
            start = clock();
            (void)pe_showdown_vector_pairwise(hero_masks, hero_strength,
                                              HERO_N, opp_masks, opp_strength,
                                              opp_reach, OPP_N, MASK_EMPTY, 7.5,
                                              &pairwise);
            pairwise_ms =
                (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
            printf("test_pe_showdown: PLO4 %d x %d sweep %.2f ms, "
                   "pairwise %.2f ms\n",
                   (int)HERO_N, (int)OPP_N, sweep_ms, pairwise_ms);
        }
        pe_vec_free(&sweep);
        pe_vec_free(&pairwise);
    }
    /* A second, range-sized measurement: the pairwise oracle is quadratic, so
       the gap only shows up once the ranges stop being toy-sized. */
    {
        enum { BIG_HERO = 2000, BIG_OPP = 40000 };
        mask_t *hero_big = (mask_t *)malloc(BIG_HERO * sizeof(*hero_big));
        mask_t *opp_big = (mask_t *)malloc(BIG_OPP * sizeof(*opp_big));
        int64_t *hero_big_strength =
            (int64_t *)malloc(BIG_HERO * sizeof(*hero_big_strength));
        int64_t *opp_big_strength =
            (int64_t *)malloc(BIG_OPP * sizeof(*opp_big_strength));
        double *opp_big_reach =
            (double *)malloc(BIG_OPP * sizeof(*opp_big_reach));
        pe_value_vec_t sweep = {0};
        pe_value_vec_t pairwise = {0};
        pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
        clock_t start;
        double sweep_ms = 0.0;
        double pairwise_ms = 0.0;
        size_t k;

        if (hero_big && opp_big && hero_big_strength && opp_big_strength &&
            opp_big_reach &&
            pe_vec_alloc(&sweep, BIG_HERO) == PE_SOLVER_OK &&
            pe_vec_alloc(&pairwise, BIG_HERO) == PE_SOLVER_OK)
        {
            fill_random_hands(hero_big, BIG_HERO, 4, UINT64_C(0xA1));
            fill_random_hands(opp_big, BIG_OPP, 4, UINT64_C(0xB2));
            for (k = 0; k < BIG_HERO; ++k)
                hero_big_strength[k] = (int64_t)((k * 31u) % 997u);
            for (k = 0; k < BIG_OPP; ++k)
            {
                opp_big_strength[k] = (int64_t)((k * 17u) % 1009u);
                opp_big_reach[k] = 0.1 + (double)(k % 11u) / 11.0;
            }
            start = clock();
            (void)pe_showdown_vector(hero_big, hero_big_strength, BIG_HERO,
                                     opp_big, opp_big_strength, opp_big_reach,
                                     BIG_OPP, MASK_EMPTY, 7.5, &sweep, &path);
            sweep_ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
            start = clock();
            (void)pe_showdown_vector_pairwise(
                hero_big, hero_big_strength, BIG_HERO, opp_big,
                opp_big_strength, opp_big_reach, BIG_OPP, MASK_EMPTY, 7.5,
                &pairwise);
            pairwise_ms =
                (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
            printf("test_pe_showdown: PLO4 %d x %d sweep %.2f ms, "
                   "pairwise %.1f ms\n",
                   (int)BIG_HERO, (int)BIG_OPP, sweep_ms, pairwise_ms);
            for (k = 0; k < BIG_HERO; ++k)
                if (fabs(sweep.v[k] - pairwise.v[k]) >
                    1e-12 * (1.0 + fabs(pairwise.v[k])))
                {
                    CHECK(0, "range-sized combo %zu: sweep %.17g pairwise "
                             "%.17g",
                          k, sweep.v[k], pairwise.v[k]);
                    break;
                }
        }
        pe_vec_free(&sweep);
        pe_vec_free(&pairwise);
        free(opp_big_reach);
        free(opp_big_strength);
        free(hero_big_strength);
        free(opp_big);
        free(hero_big);
    }

cleanup:
    free(opp_reach);
    free(opp_strength);
    free(hero_strength);
    free(opp_masks);
    free(hero_masks);
}

static void test_plo4_threshold_and_fallbacks(void)
{
    enum { HERO_N = 8, OPP_N = 40 };
    mask_t hero_masks[HERO_N];
    mask_t opp_masks[OPP_N];
    int64_t hero_strength[HERO_N];
    int64_t opp_strength[OPP_N];
    double opp_reach[OPP_N];
    pe_value_vec_t values;
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    size_t i;

    fill_random_hands(hero_masks, HERO_N, 4, UINT64_C(0x11));
    fill_random_hands(opp_masks, OPP_N, 4, UINT64_C(0x22));
    for (i = 0; i < HERO_N; ++i)
        hero_strength[i] = (int64_t)i;
    for (i = 0; i < OPP_N; ++i)
    {
        opp_strength[i] = (int64_t)(i % 7u);
        opp_reach[i] = 0.5;
    }
    CHECK(pe_vec_alloc(&values, HERO_N) == PE_SOLVER_OK,
          "threshold allocation failed");
    if (!values.v)
        return;

    /* Fewer opponents than the threshold: the setup cannot pay for itself. */
    CHECK(pe_showdown_vector(hero_masks, hero_strength, HERO_N, opp_masks,
                             opp_strength, opp_reach,
                             PE_SHOWDOWN_PLO4_MIN_OPPONENTS - 1u, MASK_EMPTY,
                             1.0, &values, &path) == PE_SOLVER_OK,
          "below-threshold call failed");
    CHECK(path == PE_SHOWDOWN_PATH_PAIRWISE,
          "below-threshold range used path %d", (int)path);

    CHECK(pe_showdown_vector(hero_masks, hero_strength, HERO_N, opp_masks,
                             opp_strength, opp_reach,
                             PE_SHOWDOWN_PLO4_MIN_OPPONENTS, MASK_EMPTY, 1.0,
                             &values, &path) == PE_SOLVER_OK,
          "at-threshold call failed");
    CHECK(path == PE_SHOWDOWN_PATH_PLO4,
          "at-threshold range used path %d", (int)path);

    /* Three-card hands are outside the PLO4 contract and stay on the oracle. */
    fill_random_hands(opp_masks, OPP_N, 3, UINT64_C(0x33));
    CHECK(pe_showdown_vector(hero_masks, hero_strength, HERO_N, opp_masks,
                             opp_strength, opp_reach, OPP_N, MASK_EMPTY, 1.0,
                             &values, &path) == PE_SOLVER_OK,
          "three-card fallback failed");
    CHECK(path == PE_SHOWDOWN_PATH_PAIRWISE,
          "three-card range used path %d", (int)path);
    pe_vec_free(&values);
}

int main(void)
{
    test_multiple_ranges_and_boards();
    test_plo4_matches_pairwise();
    test_plo4_threshold_and_fallbacks();
    test_wide_hand_fallback_and_invalid_inputs();
    test_three_player_showdown_and_fold();
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_showdown: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_pe_showdown: sorted showdown matches exhaustive pairing");
    return 0;
}
