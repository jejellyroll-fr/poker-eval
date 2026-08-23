/*
 * test_pe_vector_cfr.c - VEC-03: per-combo regret matching
 */

#include <poker_eval/solver/pe_regret.h>

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

static void test_per_combo_normalization(void)
{
    /* Three actions, four combos; each column has a different positive mass. */
    const double regrets[] = {
         2.0, -1.0,  0.0,  4.0,
        -1.0,  3.0,  0.0, -2.0,
         1.0, -2.0,  0.0,  2.0
    };
    double strategy[sizeof(regrets) / sizeof(regrets[0])];
    uint16_t action;
    uint16_t combo;

    CHECK(pe_regret_match_vector(regrets, strategy, 3u, 4u) == 0,
          "vector regret matching failed");
    for (combo = 0; combo < 4u; ++combo)
    {
        double sum = 0.0;
        for (action = 0; action < 3u; ++action)
            sum += strategy[(size_t)action * 4u + combo];
        CHECK(fabs(sum - 1.0) <= 1e-12,
              "combo %u sums to %.17g, expected 1", combo, sum);
    }

    /* combo 0: 2/(2+1), 0, 1/(2+1). */
    CHECK(fabs(strategy[0] - 2.0 / 3.0) <= 1e-12,
          "combo 0 action 0 is %.17g", strategy[0]);
    CHECK(strategy[4] == 0.0 && fabs(strategy[8] - 1.0 / 3.0) <= 1e-12,
          "combo 0 was not normalized from positive regrets");
    /* combo 1: only action 1 is positive. */
    CHECK(strategy[1] == 0.0 && strategy[5] == 1.0 && strategy[9] == 0.0,
          "combo 1 did not select its sole positive regret");
}

static void test_uniform_fallback_and_invalid_inputs(void)
{
    const double regrets[] = {-3.0, -2.0, -1.0, 0.0, -4.0, -5.0};
    double strategy[6];
    size_t i;

    memset(strategy, 0, sizeof(strategy));
    CHECK(pe_regret_match_vector(regrets, strategy, 3u, 2u) == 0,
          "uniform fallback failed");
    for (i = 0; i < 6u; ++i)
        CHECK(fabs(strategy[i] - 1.0 / 3.0) <= 1e-12,
              "uniform fallback at %zu is %.17g", i, strategy[i]);

    CHECK(pe_regret_match_vector(NULL, strategy, 3u, 2u) != 0,
          "NULL regrets accepted");
    CHECK(pe_regret_match_vector(regrets, NULL, 3u, 2u) != 0,
          "NULL strategy accepted");
    CHECK(pe_regret_match_vector(regrets, strategy, 0u, 2u) != 0,
          "zero actions accepted");
    CHECK(pe_regret_match_vector(regrets, strategy, 3u, 0u) != 0,
          "zero combos accepted");
}

int main(void)
{
    test_per_combo_normalization();
    test_uniform_fallback_and_invalid_inputs();
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_vector_cfr: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_pe_vector_cfr: regret matching is normalized per combo");
    return 0;
}
