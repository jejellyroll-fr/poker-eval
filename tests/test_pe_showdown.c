/*
 * test_pe_showdown.c - VEC-06: sorted vector showdown
 */

#include <poker_eval/solver/pe_showdown.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

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

int main(void)
{
    test_multiple_ranges_and_boards();
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
