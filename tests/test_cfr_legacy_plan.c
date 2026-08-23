/* API-02: legacy CFR booleans translate to the v3 configuration axes. */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <stdio.h>
#include <string.h>

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

int main(void)
{
    cfr_config_t legacy;
    pe_solver_config_t plan;

    memset(&legacy, 0, sizeof(legacy));
    legacy.max_iterations = 1234;
    legacy.seed = -7;
    legacy.enable_dcfr = 1;
    legacy.enable_linear_avg = 1;
    legacy.dcfr_alpha = 1.5;
    legacy.dcfr_beta = 0.25;
    legacy.dcfr_gamma = 2.0;
    legacy.ecfr_lambda = 0.0;

    CHECK(cfr_config_to_pe_solver_config(&legacy, &plan) == 0,
          "DCFR legacy config conversion failed");
    CHECK(plan.algorithm.traversal == PE_TRAVERSAL_FULL_SCALAR,
          "legacy traversal was not mapped to scalar");
    CHECK(plan.algorithm.regret == PE_REGRET_DCFR &&
              plan.algorithm.policy == PE_POLICY_REGRET_MATCHING &&
              plan.algorithm.averaging == PE_AVG_POWER,
          "DCFR axes were not mapped correctly");
    CHECK(plan.max_iterations == 1234u && plan.seed == (uint64_t)(uint32_t)-7,
          "legacy execution fields were not preserved");

    memset(&legacy, 0, sizeof(legacy));
    legacy.enable_ecfr = 1;
    legacy.ecfr_lambda = 2.5;
    CHECK(cfr_config_to_pe_solver_config(&legacy, &plan) == 0 &&
              plan.algorithm.regret == PE_REGRET_LEGACY_EXP &&
              plan.algorithm.policy == PE_POLICY_EXPONENTIAL &&
              plan.algorithm.exponential_lambda == 2.5,
          "ECFR axes were not mapped correctly");

    memset(&legacy, 0, sizeof(legacy));
    legacy.enable_linear_avg = 1;
    CHECK(cfr_config_to_pe_solver_config(&legacy, &plan) == 0 &&
              plan.algorithm.regret == PE_REGRET_VANILLA &&
              plan.algorithm.averaging == PE_AVG_LINEAR,
          "linear averaging legacy config was not mapped correctly");

    CHECK(cfr_config_to_pe_solver_config(NULL, &plan) == -1,
          "NULL legacy config was accepted");
    CHECK(cfr_config_to_pe_solver_config(&legacy, NULL) == -1,
          "NULL output was accepted");
    return failures != 0;
}
