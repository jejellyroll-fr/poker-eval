/*
 * test_pe_regret_legacy_exp.c - ALG-05 legacy ECFR policy
 */

#include <poker_eval/solver/pe_regret_legacy_exp.h>

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_positive_regrets_match_legacy_reference(void)
{
    /* Layout is [action][combo]. Combo 0 has two positive regrets; combo 1
       has one, which also checks that non-positive regrets stay at zero. */
    const double regrets[] = { 1.0, 0.0, 2.0, 1.0, -1.0, -2.0 };
    double strategy[6] = {0.0};
    const double expected = exp(-1.0) / (1.0 + exp(-1.0));

    CHECK(pe_regret_match_legacy_exp_vector(regrets, strategy, 3u, 2u, 1.0) == 0,
          "legacy exponential matching should succeed");
    CHECK(fabs(strategy[0] - expected) < 1e-12,
          "lower positive regret should use the stabilized exponential weight");
    CHECK(fabs(strategy[2] - (1.0 - expected)) < 1e-12,
          "maximum positive regret should receive the complementary weight");
    CHECK(strategy[4] == 0.0, "negative regret must receive zero probability");
    CHECK(strategy[1] == 0.0 && strategy[3] == 1.0 && strategy[5] == 0.0,
          "single-positive combo should be deterministic");
}

static void test_uniform_fallback_and_validation(void)
{
    const double regrets[] = {-2.0, -1.0, 0.0};
    double strategy[] = {9.0, 9.0, 9.0};

    CHECK(pe_regret_match_legacy_exp_vector(regrets, strategy, 3u, 1u, 1.0) == 0,
          "all non-positive regrets should be accepted");
    CHECK(fabs(strategy[0] - 1.0 / 3.0) < 1e-12 &&
              fabs(strategy[1] - 1.0 / 3.0) < 1e-12 &&
              fabs(strategy[2] - 1.0 / 3.0) < 1e-12,
          "legacy policy should fall back to uniform when no regret is positive");
    CHECK(pe_regret_match_legacy_exp_vector(regrets, strategy, 3u, 1u, 0.0) == -1,
          "zero lambda must be rejected by the isolated operator");
}

int main(void)
{
    test_positive_regrets_match_legacy_reference();
    test_uniform_fallback_and_validation();
    if (failures != 0)
        return 1;
    puts("test_pe_regret_legacy_exp: all tests passed");
    return 0;
}
