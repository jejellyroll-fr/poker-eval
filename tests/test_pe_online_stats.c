/*
 * test_pe_online_stats.c - running statistics and the adaptive-variance
 * policy rules (issue #256).
 *
 * Welford against known datasets, zero and high variance, a large offset that
 * defeats the sum-of-squares formula, tiny and large n, merging, pooled
 * variance, confidence quantiles, the tolerance and boundary tests, then the
 * policy: names, defaults, invalid settings, option parsing and the budget
 * rule with its min/max clamps.
 */

#include <poker_eval/solver/pe_online_stats.h>
#include <poker_eval/solver/pe_sampling_policy.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

/* Exact equality without -Wfloat-equal: these values are exactly
   representable, and the check is that nothing perturbed them. */
static int exact(double a, double b) { return !(a < b) && !(a > b); }

static int close_to(double a, double b, double tol)
{
    return fabs(a - b) <= tol * (1.0 + fabs(b));
}

static void test_known_dataset(void)
{
    static const double data[] = {2, 4, 4, 4, 5, 5, 7, 9};
    pe_online_stats_t s;
    pe_online_stats_reset(&s);
    for (size_t i = 0; i < sizeof(data) / sizeof(data[0]); ++i)
        pe_online_stats_add(&s, data[i]);
    /* mean 5; sum of squared deviations 32; sample variance 32/7 */
    CHECK(s.n == 8u && close_to(s.mean, 5.0, 1e-15), "mean %f", s.mean);
    CHECK(close_to(pe_online_stats_variance(&s), 32.0 / 7.0, 1e-14),
          "variance %.17g", pe_online_stats_variance(&s));
    CHECK(close_to(pe_online_stats_std_error(&s), sqrt(32.0 / 7.0 / 8.0), 1e-14),
          "standard error");
    CHECK(close_to(pe_online_stats_half_width(&s, 2.0),
                   2.0 * sqrt(32.0 / 7.0 / 8.0), 1e-14),
          "half-width");
}

static void test_small_and_degenerate(void)
{
    pe_online_stats_t s;
    pe_online_stats_reset(&s);
    CHECK(exact(pe_online_stats_variance(&s), 0.0) &&
              exact(pe_online_stats_std_error(&s), 0.0),
          "empty: no spread");
    CHECK(!pe_online_stats_resolved(&s, 1.96, 1.0, 0.0),
          "empty is never resolved");
    CHECK(pe_online_stats_side(&s, 1.96, 0.0) == 0, "empty has no side");

    pe_online_stats_add(&s, 3.0);
    CHECK(s.n == 1u && exact(s.mean, 3.0) && exact(pe_online_stats_variance(&s), 0.0),
          "one observation: mean, no variance");
    CHECK(!pe_online_stats_resolved(&s, 1.96, 100.0, 0.0),
          "one observation cannot be resolved, whatever the tolerance");

    pe_online_stats_add(&s, NAN);
    pe_online_stats_add(&s, INFINITY);
    CHECK(s.n == 1u && exact(s.mean, 3.0), "non-finite values are ignored");

    /* Zero variance. */
    pe_online_stats_reset(&s);
    for (int i = 0; i < 100; ++i)
        pe_online_stats_add(&s, 7.25);
    CHECK(exact(s.mean, 7.25) && exact(pe_online_stats_variance(&s), 0.0) &&
              exact(pe_online_stats_half_width(&s, 3.0), 0.0),
          "constant data: exact mean, zero spread");
    CHECK(pe_online_stats_resolved(&s, 3.0, 0.0, 0.0),
          "zero spread meets even a zero tolerance");
    CHECK(pe_online_stats_side(&s, 3.0, 7.0) == 1 &&
              pe_online_stats_side(&s, 3.0, 8.0) == -1,
          "a zero-width interval is on one side of any other value");
}

static void test_stability(void)
{
    /* 1e9 + {1..10}: variance of 1..10 is 55/6. The sum-of-squares formula
       subtracts two numbers near 1e19 and keeps no digit of it. */
    pe_online_stats_t s;
    pe_online_stats_reset(&s);
    for (int i = 1; i <= 10; ++i)
        pe_online_stats_add(&s, 1e9 + (double)i);
    CHECK(close_to(pe_online_stats_variance(&s), 55.0 / 6.0, 1e-9),
          "large offset: variance %.17g, want %.17g",
          pe_online_stats_variance(&s), 55.0 / 6.0);
    CHECK(close_to(s.mean, 1e9 + 5.5, 1e-15), "large offset: mean");

    /* High variance next to a tiny mean. */
    pe_online_stats_reset(&s);
    pe_online_stats_add(&s, -1e6);
    pe_online_stats_add(&s, 1e6);
    CHECK(exact(s.mean, 0.0) && close_to(pe_online_stats_variance(&s), 2e12, 1e-15),
          "high variance: %.17g", pe_online_stats_variance(&s));

    /* Large n: a million alternating +-1. Sample variance n/(n-1). */
    pe_online_stats_reset(&s);
    for (int i = 0; i < 1000000; ++i)
        pe_online_stats_add(&s, (i & 1) ? 1.0 : -1.0);
    CHECK(fabs(s.mean) < 1e-12 &&
              close_to(pe_online_stats_variance(&s), 1e6 / (1e6 - 1.0), 1e-12),
          "large n: mean %.3g variance %.17g", s.mean,
          pe_online_stats_variance(&s));
}

static void test_merge_and_pooled(void)
{
    pe_online_stats_t all, a, b, empty;
    pe_online_stats_reset(&all);
    pe_online_stats_reset(&a);
    pe_online_stats_reset(&b);
    pe_online_stats_reset(&empty);
    for (int i = 0; i < 37; ++i)
    {
        double x = sin((double)i) * 100.0 + (double)(i % 5);
        pe_online_stats_add(&all, x);
        pe_online_stats_add(i < 11 ? &a : &b, x);
    }
    pe_online_stats_merge(&a, &b);
    CHECK(a.n == all.n && close_to(a.mean, all.mean, 1e-13) &&
              close_to(a.m2, all.m2, 1e-12),
          "merge equals one pass (mean %.17g/%.17g, m2 %.17g/%.17g)", a.mean,
          all.mean, a.m2, all.m2);
    pe_online_stats_merge(&a, &empty);
    CHECK(a.n == all.n, "merging an empty accumulator changes nothing");
    pe_online_stats_merge(&empty, &all);
    CHECK(empty.n == all.n && exact(empty.mean, all.mean),
          "merging into an empty accumulator copies");

    /* Pooled: {1,3} (m2 2) and {10,12,14} (m2 8): 10 over 1 + 2 dof. The
       groups' means (2 and 12) are far apart; that must not count. */
    pe_pooled_variance_t pooled = {0, 0.0};
    pe_online_stats_t g;
    pe_online_stats_reset(&g);
    pe_online_stats_add(&g, 1.0);
    pe_online_stats_add(&g, 3.0);
    pe_pooled_variance_add(&pooled, &g);
    pe_online_stats_reset(&g);
    pe_online_stats_add(&g, 10.0);
    pe_online_stats_add(&g, 12.0);
    pe_online_stats_add(&g, 14.0);
    pe_pooled_variance_add(&pooled, &g);
    pe_online_stats_reset(&g);
    pe_online_stats_add(&g, 99.0); /* a single draw adds nothing */
    pe_pooled_variance_add(&pooled, &g);
    CHECK(pooled.dof == 3u && close_to(pe_pooled_variance_value(&pooled),
                                       10.0 / 3.0, 1e-15),
          "pooled variance %.17g over %llu dof",
          pe_pooled_variance_value(&pooled), (unsigned long long)pooled.dof);
}

static void test_confidence(void)
{
    CHECK(close_to(pe_confidence_z(0.95), 1.959963984540054, 1e-12),
          "z(0.95) %.15f", pe_confidence_z(0.95));
    CHECK(close_to(pe_confidence_z(0.99), 2.5758293035489, 1e-12),
          "z(0.99) %.15f", pe_confidence_z(0.99));
    CHECK(close_to(pe_confidence_z(0.5), 0.6744897501960817, 1e-12),
          "z(0.5) %.15f", pe_confidence_z(0.5));
    CHECK(isnan(pe_confidence_z(0.0)) && isnan(pe_confidence_z(1.0)) &&
              isnan(pe_confidence_z(-0.5)) && isnan(pe_confidence_z(NAN)),
          "levels outside (0, 1) have no quantile");

    /* {0, 2}: mean 1, variance 2, standard error 1. With z = 2 the
       half-width is exactly 2. */
    pe_online_stats_t s;
    pe_online_stats_reset(&s);
    pe_online_stats_add(&s, 0.0);
    pe_online_stats_add(&s, 2.0);
    CHECK(close_to(pe_online_stats_half_width(&s, 2.0), 2.0, 1e-15),
          "half-width 2");
    CHECK(pe_online_stats_resolved(&s, 2.0, 2.0, 0.0),
          "half-width equal to the absolute tolerance is resolved");
    CHECK(!pe_online_stats_resolved(&s, 2.0, 1.999, 0.0),
          "just below it is not");
    CHECK(pe_online_stats_resolved(&s, 2.0, 0.0, 2.0) &&
              !pe_online_stats_resolved(&s, 2.0, 0.0, 1.99),
          "relative tolerance scales with |mean| = 1");
    CHECK(pe_online_stats_resolved(&s, 2.0, 2.0, 0.5),
          "the larger of the two tolerances applies");
    /* Interval [-1, 3] with z = 2. */
    CHECK(pe_online_stats_side(&s, 2.0, 0.0) == 0,
          "straddles 0");
    CHECK(pe_online_stats_side(&s, 2.0, -1.5) == 1 &&
              pe_online_stats_side(&s, 2.0, 3.5) == -1,
          "clear of -1.5 and of 3.5");
    CHECK(pe_online_stats_side(&s, 0.5, 0.0) == 1,
          "a narrower interval, [0.5, 1.5], clears 0");
}

static void test_policy(void)
{
    pe_sampling_policy_t p;
    const pe_sampling_policy_t all[] = {PE_SAMPLING_STANDARD,
                                        PE_SAMPLING_STREET_BALANCED,
                                        PE_SAMPLING_ADAPTIVE_VARIANCE};
    for (size_t i = 0; i < 3; ++i)
    {
        const char *name = pe_sampling_policy_name(all[i]);
        CHECK(pe_sampling_policy_parse(name, &p) == 0 && p == all[i],
              "name round trip: %s", name);
    }
    CHECK(strcmp(pe_sampling_policy_name(PE_SAMPLING_ADAPTIVE_VARIANCE),
                 "adaptive-variance") == 0,
          "adaptive name");
    CHECK(pe_sampling_policy_parse("adaptive", &p) != 0,
          "an unknown name is refused");
    /* The fixed-replicate rule is unchanged for the new policy's value. */
    CHECK(pe_sampling_replicates_for(PE_SAMPLING_ADAPTIVE_VARIANCE, NULL, 1) == 1u,
          "the street table is not read by the adaptive policy");

    /* Defaults from a zero struct and from NULL. */
    pe_adaptive_sampling_t zero, r;
    double z = 0.0;
    memset(&zero, 0, sizeof(zero));
    CHECK(pe_adaptive_sampling_resolve(&zero, &r, &z) == 0 &&
              r.min_samples == 2u && r.max_samples == 32u &&
              r.check_interval == 64u && exact(r.confidence_level, 0.95) &&
              exact(r.absolute_tolerance, 0.0) && exact(r.relative_tolerance, 0.05) &&
              close_to(z, 1.959963984540054, 1e-12),
          "zero settings resolve to the defaults");
    CHECK(pe_adaptive_sampling_resolve(NULL, &r, NULL) == 0 &&
              r.min_samples == 2u, "NULL settings resolve to the defaults");

    pe_adaptive_sampling_t s = zero;
    s.min_samples = 1u;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) == 0 && r.min_samples == 2u,
          "a minimum of one becomes two");
    s = zero;
    s.min_samples = 40u;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) == 0 && r.max_samples == 40u,
          "a minimum above the default maximum lifts the maximum");
    s = zero;
    s.absolute_tolerance = 0.5;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) == 0 &&
              exact(r.relative_tolerance, 0.0),
          "an absolute tolerance alone keeps the relative one at zero");

    s = zero;
    s.min_samples = 10u;
    s.max_samples = 4u;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) != 0, "min above max");
    s = zero;
    s.max_samples = PE_ADAPTIVE_MAX_SAMPLES_CEILING + 1u;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) != 0, "max above ceiling");
    s = zero;
    s.confidence_level = 1.0;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) != 0, "confidence 1");
    s = zero;
    s.relative_tolerance = -0.1;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) != 0, "negative tolerance");
    s = zero;
    s.absolute_tolerance = NAN;
    CHECK(pe_adaptive_sampling_resolve(&s, &r, NULL) != 0, "NaN tolerance");

    /* Option parsing. */
    s = zero;
    CHECK(pe_adaptive_sampling_parse_option(&s, "min-samples", "3") == 0 &&
              pe_adaptive_sampling_parse_option(&s, "max-samples", "12") == 0 &&
              pe_adaptive_sampling_parse_option(&s, "check-interval", "7") == 0 &&
              pe_adaptive_sampling_parse_option(&s, "confidence", "0.9") == 0 &&
              pe_adaptive_sampling_parse_option(&s, "absolute-tolerance", "0.25") == 0 &&
              pe_adaptive_sampling_parse_option(&s, "relative-tolerance", "1e-2") == 0 &&
              s.min_samples == 3u && s.max_samples == 12u &&
              s.check_interval == 7u && exact(s.confidence_level, 0.9) &&
              exact(s.absolute_tolerance, 0.25) && exact(s.relative_tolerance, 0.01),
          "every option parses");
    CHECK(pe_adaptive_sampling_parse_option(&s, "max-samples", "-3") != 0 &&
              pe_adaptive_sampling_parse_option(&s, "max-samples", "4x") != 0 &&
              pe_adaptive_sampling_parse_option(&s, "confidence", "abc") != 0 &&
              pe_adaptive_sampling_parse_option(&s, "confidence", "1e999") != 0 &&
              pe_adaptive_sampling_parse_option(&s, "samples", "3") != 0,
          "bad values and unknown keys are refused");

    /* Budget: R = ceil(z^2 variance / tolerance^2), clamped. */
    s = zero;
    s.min_samples = 2u;
    s.max_samples = 32u;
    s.absolute_tolerance = 1.0;
    pe_adaptive_sampling_resolve(&s, &r, NULL);
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 4.0, 0.0) == 16u,
          "z 2, variance 4, tolerance 1: 16 draws");
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 4.25, 0.0) == 17u,
          "a fraction rounds up: 17");
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 100.0, 0.0) == 32u,
          "the maximum caps it");
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 0.1, 0.0) == 2u,
          "the minimum floors it");
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 0.0, 0.0) == 2u,
          "no variance needs the minimum");
    s.absolute_tolerance = 0.0;
    s.relative_tolerance = 0.5;
    pe_adaptive_sampling_resolve(&s, &r, NULL);
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 4.0, 4.0) == 4u,
          "relative tolerance 0.5 of a mean of 4 is 2: 4 draws");
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 4.0, 0.0) == 32u,
          "a zero mean leaves a zero tolerance: the maximum");
    CHECK(pe_adaptive_sampling_budget(&r, 2.0, 4.0, -4.0) == 4u,
          "the tolerance uses |mean|");
}

int main(void)
{
    printf("test_pe_online_stats: running statistics and adaptive policy rules\n");
    test_known_dataset();
    test_small_and_degenerate();
    test_stability();
    test_merge_and_pooled();
    test_confidence();
    test_policy();
    if (g_failures)
    {
        fprintf(stderr, "test_pe_online_stats: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_pe_online_stats: all checks passed\n");
    return 0;
}
