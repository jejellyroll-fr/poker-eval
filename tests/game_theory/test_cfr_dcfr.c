/*
 * test_cfr_dcfr.c - ALG-03 canonical DCFR factors.
 */

#include <poker_eval/solver/pe_regret_dcfr.h>

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

static void test_canonical_parameters(void)
{
    pe_dcfr_params_t params = pe_dcfr_params_default();
    CHECK(params.alpha == 1.5, "alpha default is %.17g", params.alpha);
    CHECK(params.beta == 0.0, "beta default is %.17g", params.beta);
    CHECK(params.gamma == 2.0, "gamma default is %.17g", params.gamma);
}

static void test_positive_and_negative_discount(void)
{
    pe_dcfr_params_t params = pe_dcfr_params_default();
    double regrets[] = {2.0, -2.0};
    double positive_factor;

    CHECK(pe_dcfr_discount_regrets(regrets, 2u, 1u, &params) == 0,
          "DCFR regret discount failed");
    CHECK(fabs(regrets[0] - 1.0) <= 1e-12 &&
              fabs(regrets[1] + 1.0) <= 1e-12,
          "iteration 1 should halve positive and negative regrets");

    positive_factor = pow(2.0, 1.5) / (pow(2.0, 1.5) + 1.0);
    regrets[0] = 2.0;
    regrets[1] = -2.0;
    CHECK(pe_dcfr_discount_regrets(regrets, 2u, 2u, &params) == 0,
          "second DCFR regret discount failed");
    CHECK(fabs(regrets[0] - 2.0 * positive_factor) <= 1e-12 &&
              fabs(regrets[1] + 1.0) <= 1e-12,
          "canonical positive/negative factors are incorrect");
}

static void test_average_weight(void)
{
    pe_dcfr_params_t params = pe_dcfr_params_default();
    double weight = 0.0;

    CHECK(pe_dcfr_average_weight(2u, params.gamma, &weight) == 0,
          "DCFR averaging weight failed");
    CHECK(fabs(weight - 4.0 / 9.0) <= 1e-12,
          "gamma=2 averaging weight is %.17g, expected 4/9", weight);
}

static void test_rejects_invalid_input_without_mutation(void)
{
    pe_dcfr_params_t params = pe_dcfr_params_default();
    double regrets[] = {2.0, -2.0};
    const double before[] = {2.0, -2.0};

    params.beta = -1.0;
    CHECK(pe_dcfr_discount_regrets(regrets, 2u, 1u, &params) == -1,
          "negative beta must be rejected");
    CHECK(fabs(regrets[0] - before[0]) <= 1e-12 &&
              fabs(regrets[1] - before[1]) <= 1e-12,
          "invalid DCFR parameters must not mutate regrets");
    CHECK(pe_dcfr_average_weight(0u, 2.0, &regrets[0]) == -1,
          "zero iteration must be rejected");
}

int main(void)
{
    test_canonical_parameters();
    test_positive_and_negative_discount();
    test_average_weight();
    test_rejects_invalid_input_without_mutation();
    if (failures != 0)
    {
        fprintf(stderr, "test_cfr_dcfr: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_cfr_dcfr: canonical DCFR factors passed");
    return 0;
}
