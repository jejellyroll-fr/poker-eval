/*
 * test_cfr_plus.c - ALG-01 CFR+ regret clamp.
 */

#include <poker_eval/solver/pe_regret.h>

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);       \
            fprintf(stderr, __VA_ARGS__);                                 \
            fputc('\n', stderr);                                          \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static void test_clamps_once_after_batch(void)
{
    double regrets[] = {1.0, -2.0, 0.5, 4.0};
    const double delta[] = {-3.0, 1.0, -0.5, -1.0};

    CHECK(pe_regret_plus_apply_delta(regrets, delta, 4u) == 0,
          "CFR+ delta application failed");
    CHECK(regrets[0] == 0.0 && regrets[1] == 0.0 &&
              regrets[2] == 0.0 && regrets[3] == 3.0,
          "CFR+ did not clamp the complete cumulative span");
}

static void test_matching_plus_uses_positive_regrets(void)
{
    const double regrets[] = {2.0, 0.0, 1.0, 3.0};
    double strategy[4];

    CHECK(pe_regret_match_plus_vector(regrets, strategy, 2u, 2u) == 0,
          "CFR+ strategy matching failed");
    CHECK(fabs(strategy[0] - 2.0 / 3.0) < 1e-12 &&
              fabs(strategy[1]) < 1e-12 &&
              fabs(strategy[2] - 1.0 / 3.0) < 1e-12 &&
              fabs(strategy[3] - 1.0) < 1e-12,
          "CFR+ strategy probabilities are incorrect");
}

static void test_rejects_nonfinite_without_mutation(void)
{
    double regrets[] = {1.0, 2.0};
    const double delta[] = {NAN, 1.0};

    CHECK(pe_regret_plus_apply_delta(regrets, delta, 2u) == -1,
          "non-finite delta should be rejected");
    CHECK(regrets[0] == 1.0 && regrets[1] == 2.0,
          "rejected batch must not partially mutate regrets");
}

int main(void)
{
    test_clamps_once_after_batch();
    test_matching_plus_uses_positive_regrets();
    test_rejects_nonfinite_without_mutation();
    if (failures != 0)
    {
        fprintf(stderr, "test_cfr_plus: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_cfr_plus: CFR+ clamp and matching contract passed");
    return 0;
}
