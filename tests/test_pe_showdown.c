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

int main(void)
{
    test_multiple_ranges_and_boards();
    test_wide_hand_fallback_and_invalid_inputs();
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_showdown: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_pe_showdown: sorted showdown matches exhaustive pairing");
    return 0;
}
