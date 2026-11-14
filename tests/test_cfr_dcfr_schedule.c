/*
 * test_cfr_dcfr_schedule.c - HS-DCFR parameter schedules (issue #284)
 *
 * The training-free Hyperparameter Schedules of Zhang et al. (AAAI 2026)
 * replace DCFR's fixed exponents with
 *
 *   alpha(t) = 1 + 3 * min(t, n) / n
 *   beta(t)  = -1 - 2 * min(t, n) / n
 *   gamma(t) = G - 5 * min(t, n) / n     with G = 15 or 30
 *
 * where t is the iteration and n the horizon. These checks pin the endpoints,
 * the clamping past the horizon, the parsing surface, and the fact that the
 * resulting negative beta is actually usable by the discount primitive.
 */

#include <poker_eval/solver/pe_regret_dcfr.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                     \
    do                                                            \
    {                                                             \
        if (!(condition))                                         \
        {                                                         \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                           \
        }                                                         \
    } while (0)

static int near(double left, double right, double tolerance)
{
    return fabs(left - right) <= tolerance * (1.0 + fabs(right));
}

static void test_fixed_control(void)
{
    pe_dcfr_params_t fixed = { 1.5, 0.0, 2.0 };
    pe_dcfr_params_t resolved;

    CHECK(pe_dcfr_params_resolve(&fixed, PE_DCFR_SCHEDULE_FIXED, 0u, 7u,
                                 &resolved) == 0,
          "fixed schedule rejected a valid control");
    CHECK(resolved.alpha == 1.5 && resolved.beta == 0.0 &&
              resolved.gamma == 2.0,
          "fixed schedule changed the exponents");
    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_FIXED, 0u, 7u,
                                 &resolved) != 0,
          "fixed schedule accepted a NULL control");
    fixed.gamma = -1.0;
    CHECK(pe_dcfr_params_resolve(&fixed, PE_DCFR_SCHEDULE_FIXED, 0u, 7u,
                                 &resolved) != 0,
          "fixed schedule accepted a negative gamma");
    fixed.gamma = 2.0;
    fixed.alpha = -0.5;
    CHECK(pe_dcfr_params_resolve(&fixed, PE_DCFR_SCHEDULE_FIXED, 0u, 7u,
                                 &resolved) != 0,
          "fixed schedule accepted a negative alpha");
    fixed.alpha = 1.5;
    fixed.beta = NAN;
    CHECK(pe_dcfr_params_resolve(&fixed, PE_DCFR_SCHEDULE_FIXED, 0u, 7u,
                                 &resolved) != 0,
          "fixed schedule accepted a non-finite beta");
}

static void test_schedule_endpoints(void)
{
    pe_dcfr_params_t resolved;

    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS15, 1000u, 1u,
                                 &resolved) == 0,
          "hs15 rejected its first iteration");
    CHECK(near(resolved.alpha, 1.003, 1e-12),
          "hs15 alpha at t=1 is %.17g", resolved.alpha);
    CHECK(near(resolved.beta, -1.002, 1e-12),
          "hs15 beta at t=1 is %.17g", resolved.beta);
    CHECK(near(resolved.gamma, 14.995, 1e-12),
          "hs15 gamma at t=1 is %.17g", resolved.gamma);

    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS30, 1000u, 1000u,
                                 &resolved) == 0,
          "hs30 rejected its horizon");
    CHECK(near(resolved.alpha, 4.0, 1e-12), "hs30 alpha at n is %.17g",
          resolved.alpha);
    CHECK(near(resolved.beta, -3.0, 1e-12), "hs30 beta at n is %.17g",
          resolved.beta);
    CHECK(near(resolved.gamma, 25.0, 1e-12), "hs30 gamma at n is %.17g",
          resolved.gamma);

    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS15, 1000u, 500u,
                                 &resolved) == 0,
          "hs15 rejected its midpoint");
    CHECK(near(resolved.alpha, 2.5, 1e-12), "hs15 alpha at n/2 is %.17g",
          resolved.alpha);
    CHECK(near(resolved.beta, -2.0, 1e-12), "hs15 beta at n/2 is %.17g",
          resolved.beta);
    CHECK(near(resolved.gamma, 12.5, 1e-12), "hs15 gamma at n/2 is %.17g",
          resolved.gamma);

    /* Past the horizon the parameters stay at their final values. */
    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS30, 1000u, 4000u,
                                 &resolved) == 0,
          "hs30 rejected an iteration past the horizon");
    CHECK(near(resolved.alpha, 4.0, 1e-12) &&
              near(resolved.beta, -3.0, 1e-12) &&
              near(resolved.gamma, 25.0, 1e-12),
          "hs30 extrapolated past the horizon: %f %f %f", resolved.alpha,
          resolved.beta, resolved.gamma);
}

static void test_invalid_inputs(void)
{
    pe_dcfr_params_t resolved;

    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS15, 0u, 1u,
                                 &resolved) != 0,
          "a zero horizon was accepted");
    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS15, 10u, 0u,
                                 &resolved) != 0,
          "iteration zero was accepted");
    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS15, 10u, 1u,
                                 NULL) != 0,
          "a NULL output was accepted");
    CHECK(pe_dcfr_params_resolve(NULL, (pe_dcfr_schedule_t)99, 10u, 1u,
                                 &resolved) != 0,
          "an unknown schedule was accepted");
}

static void test_names_and_parsing(void)
{
    pe_dcfr_schedule_t schedule;

    CHECK(strcmp(pe_dcfr_schedule_name(PE_DCFR_SCHEDULE_FIXED), "fixed") == 0,
          "fixed name changed");
    CHECK(strcmp(pe_dcfr_schedule_name(PE_DCFR_SCHEDULE_HS15), "hs15") == 0,
          "hs15 name changed");
    CHECK(strcmp(pe_dcfr_schedule_name(PE_DCFR_SCHEDULE_HS30), "hs30") == 0,
          "hs30 name changed");

    CHECK(pe_dcfr_schedule_parse("fixed", &schedule) == 0 &&
              schedule == PE_DCFR_SCHEDULE_FIXED,
          "fixed did not parse");
    CHECK(pe_dcfr_schedule_parse("hs15", &schedule) == 0 &&
              schedule == PE_DCFR_SCHEDULE_HS15,
          "hs15 did not parse");
    CHECK(pe_dcfr_schedule_parse("hs30", &schedule) == 0 &&
              schedule == PE_DCFR_SCHEDULE_HS30,
          "hs30 did not parse");
    CHECK(pe_dcfr_schedule_parse("hs20", &schedule) != 0,
          "an unknown schedule parsed");
    CHECK(pe_dcfr_schedule_parse(NULL, &schedule) != 0,
          "NULL text parsed");
    CHECK(pe_dcfr_schedule_parse("hs30", NULL) != 0,
          "NULL output parsed");
}

/* The scheduled beta is negative; the discount primitive must accept it and
   produce the documented factor t^beta / (t^beta + 1). */
static void test_negative_beta_discount(void)
{
    pe_dcfr_params_t resolved;
    double regret = -2.0;
    double expected;

    CHECK(pe_dcfr_params_resolve(NULL, PE_DCFR_SCHEDULE_HS30, 1000u, 1000u,
                                 &resolved) == 0,
          "hs30 resolution failed");
    CHECK(pe_dcfr_discount_regrets(&regret, 1u, 1000u, &resolved) == 0,
          "the scheduled negative beta was rejected");
    expected = -2.0 * (pow(1000.0, resolved.beta) /
                       (pow(1000.0, resolved.beta) + 1.0));
    CHECK(near(regret, expected, 1e-12),
          "discounted regret is %.17g, expected %.17g", regret, expected);
}

int main(void)
{
    test_fixed_control();
    test_schedule_endpoints();
    test_invalid_inputs();
    test_names_and_parsing();
    test_negative_beta_discount();

    if (failures != 0)
    {
        fprintf(stderr, "test_cfr_dcfr_schedule: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_cfr_dcfr_schedule: HS-DCFR schedules match the paper");
    return 0;
}
