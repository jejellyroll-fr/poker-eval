/*
 * test_pe_vector.c - RNG-04: per-combo vectors
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The operations are three-line loops; what is worth testing is not that they
 * loop but that they stay accurate at the sizes the vector lane actually uses.
 * 1326 is a Hold'em range, 270725 a PLO one, and the reduction over a full PLO
 * range is where a naive sum would first lose enough precision to matter —
 * showing up not as an obvious defect but as a solver that quietly stops
 * converging.
 *
 * So the accuracy checks are built against inputs with a known exact answer,
 * and compared to what naive summation gives on the same data, so the margin
 * is visible rather than asserted.
 */

#include <poker_eval/solver/pe_vector.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
            g_failures++;                                      \
        }                                                      \
    } while (0)

/* Exact equality is the intent where a result is representable: 1/3 * 3 is not,
   but 0.5 * 2 is, and a tolerance there would hide a wrong loop bound. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#define HOLDEM_COMBOS 1326u
#define PLO_COMBOS 270725u

static double naive_sum(const double *v, size_t n)
{
    double s = 0.0;
    size_t i;
    for (i = 0; i < n; ++i)
        s += v[i];
    return s;
}

/* ------------------------------------------------------------------ *
 * Lifetime
 * ------------------------------------------------------------------ */

static void test_lifetime(void)
{
    pe_vec_t v;
    size_t i;

    CHECK(pe_vec_alloc(&v, 8) == PE_SOLVER_OK, "alloc failed");
    CHECK(v.n == 8 && v.v != NULL, "alloc produced %zu components", v.n);
    for (i = 0; i < v.n; ++i)
        CHECK(v.v[i] == 0.0, "component %zu is %.17g, expected 0", i, v.v[i]);
    pe_vec_free(&v);
    CHECK(v.v == NULL && v.n == 0, "free left the vector usable");
    pe_vec_free(&v);      /* twice: documented as safe */
    pe_vec_free(NULL);

    /* Zero length is a caller who has not counted their combos. */
    CHECK(pe_vec_alloc(&v, 0) == PE_SOLVER_ERR_INVALID_CONFIG,
          "a zero-length vector was allocated");
    CHECK(v.v == NULL && v.n == 0, "a refused alloc left something behind");
    CHECK(pe_vec_alloc(NULL, 4) == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL out");

    /* A wrapped vector borrows. */
    {
        double storage[4] = { 1.0, 2.0, 3.0, 4.0 };
        pe_vec_t w = pe_vec_wrap(storage, 4);
        CHECK(w.v == storage && w.n == 4, "wrap did not borrow");
        pe_vec_scale(&w, 2.0);
        CHECK(storage[3] == 8.0, "wrap did not write through");
        w = pe_vec_wrap(NULL, 4);
        CHECK(w.n == 0, "wrapping nothing produced a non-empty vector");
    }
}

/* ------------------------------------------------------------------ *
 * The DoD's uniform case
 * ------------------------------------------------------------------ */

static void test_uniform_reach_times_uniform_strategy(void)
{
    /* The vector lane's defining operation: a strategy is one probability per
       combo, so advancing a reach is component-wise rather than scalar. */
    pe_vec_t reach;
    pe_vec_t strategy;
    size_t i;
    int bad = 0;

    CHECK(pe_vec_alloc(&reach, HOLDEM_COMBOS) == PE_SOLVER_OK, "alloc failed");
    CHECK(pe_vec_alloc(&strategy, HOLDEM_COMBOS) == PE_SOLVER_OK, "alloc failed");

    pe_vec_fill(&reach, 1.0);
    pe_vec_fill(&strategy, 1.0 / 3.0);
    pe_vec_mul(&reach, &strategy);

    for (i = 0; i < reach.n && !bad; ++i)
        if (fabs(reach.v[i] - 1.0 / 3.0) > 1e-14)
        {
            CHECK(0, "component %zu is %.17g, expected 1/3", i, reach.v[i]);
            bad = 1;
        }

    /* Three actions cover the range exactly once. */
    CHECK(fabs(pe_vec_sum(&reach) * 3.0 - (double)HOLDEM_COMBOS) < 1e-9,
          "three uniform actions do not sum back to the range: %.17g",
          pe_vec_sum(&reach) * 3.0);

    pe_vec_free(&reach);
    pe_vec_free(&strategy);
}

/* ------------------------------------------------------------------ *
 * Accuracy at the sizes that matter
 * ------------------------------------------------------------------ */

/*
 * A vector whose exact sum is known and whose terms span many magnitudes:
 * one large weight and a long tail of tiny ones, which is the shape a reach
 * vector takes once a few combos dominate.
 */
static void fill_wide_magnitudes(double *v, size_t n, double *out_exact)
{
    size_t i;
    v[0] = 1.0e8;
    for (i = 1; i < n; ++i)
        v[i] = 1.0e-8;
    *out_exact = 1.0e8 + (double)(n - 1) * 1.0e-8;
}

static void check_sum_accuracy(const char *label, size_t n)
{
    pe_vec_t v;
    double exact = 0.0;
    double compensated, naive, err_c, err_n;

    CHECK(pe_vec_alloc(&v, n) == PE_SOLVER_OK, "%s: alloc failed", label);
    if (v.v == NULL) return;

    fill_wide_magnitudes(v.v, n, &exact);
    compensated = pe_vec_sum(&v);
    naive = naive_sum(v.v, n);

    err_c = fabs(compensated - exact) / exact;
    err_n = fabs(naive - exact) / exact;

    CHECK(err_c <= 1e-14,
          "%s: compensated sum is off by %.3e relative (%.17g against %.17g)",
          label, err_c, compensated, exact);

    printf("    %-10s n=%-7zu compensated %.3e   naive %.3e\n",
           label, n, err_c, err_n);

    pe_vec_free(&v);
}

/*
 * The case that separates Neumaier from plain Kahan.
 *
 * [1, 1e100, 1, -1e100] sums to exactly 2, and the order is the whole point.
 * When the 1e100 arrives the running total is 1; Kahan computes its
 * compensation as (sum - t) + x, and (1 - 1e100) rounds to exactly -1e100, so
 * adding 1e100 back gives zero and the 1 is gone. Neumaier notices the running
 * total is the smaller of the two and takes the compensation from the other
 * side, keeping it. Kahan returns 1 here, Neumaier 2.
 *
 * Three attempts were needed to find an ordering that discriminates: a large
 * term first, then a large term last, then this. In the first two the running
 * total already dominated and plain Kahan was exact, so the suite passed with
 * the weaker implementation substituted — which would have left reach.c's
 * claim to Neumaier untested and unjustified.
 */
static void check_neumaier_beats_kahan(void)
{
    double storage[4] = { 1.0, 1.0e100, 1.0, -1.0e100 };
    pe_vec_t v = pe_vec_wrap(storage, 4);
    double got = pe_vec_sum(&v);

    CHECK(got == 2.0,
          "sum of [1, 1e100, 1, -1e100] is %.17g, expected exactly 2 "
          "(plain Kahan gives 1 here)", got);
    printf("    kahan-trap [1,1e100,1,-1e100] -> %.17g (Kahan gives 1)\n", got);
}

static void test_sum_accuracy(void)
{
    printf("  relative error against the exact sum:\n");
    check_sum_accuracy("holdem", HOLDEM_COMBOS);
    check_sum_accuracy("plo", PLO_COMBOS);
    check_neumaier_beats_kahan();
}

static void test_dot_accuracy(void)
{
    /*
     * The same shape as the sum test, because it is the one that discriminates:
     * one large product and a long tail of tiny ones, so the running total
     * dwarfs each new term.
     *
     * An earlier version used alternating terms that cancelled in pairs. It
     * passed, and it proved nothing — adjacent cancellation keeps the running
     * total small, so naive summation was exact too. A test that the naive
     * implementation also passes is not testing the compensation.
     */
    pe_vec_t a, b;
    size_t i;
    double d, naive = 0.0, exact, err_c, err_n;

    CHECK(pe_vec_alloc(&a, PLO_COMBOS) == PE_SOLVER_OK, "alloc failed");
    CHECK(pe_vec_alloc(&b, PLO_COMBOS) == PE_SOLVER_OK, "alloc failed");
    if (!a.v || !b.v) { pe_vec_free(&a); pe_vec_free(&b); return; }

    a.v[0] = 1.0e4;
    b.v[0] = 1.0e4;                      /* product 1e8 */
    for (i = 1; i < PLO_COMBOS; ++i)
    {
        a.v[i] = 1.0e-4;
        b.v[i] = 1.0e-4;                 /* product 1e-8 */
    }
    exact = 1.0e8 + (double)(PLO_COMBOS - 1) * 1.0e-8;

    d = pe_vec_dot(&a, &b);
    for (i = 0; i < PLO_COMBOS; ++i)
        naive += a.v[i] * b.v[i];

    err_c = fabs(d - exact) / exact;
    err_n = fabs(naive - exact) / exact;

    CHECK(err_c <= 1e-14, "compensated dot is off by %.3e relative", err_c);
    CHECK(err_n > err_c,
          "the naive dot was no worse (%.3e against %.3e): this case does not "
          "discriminate and the test proves nothing", err_n, err_c);

    printf("    dot        n=%-7u compensated %.3e   naive %.3e\n",
           PLO_COMBOS, err_c, err_n);

    /* And the simple case still holds. */
    pe_vec_fill(&a, 2.0);
    pe_vec_fill(&b, 3.0);
    d = pe_vec_dot(&a, &b);
    CHECK(fabs(d - 6.0 * (double)PLO_COMBOS) <= 1e-14 * 6.0 * (double)PLO_COMBOS,
          "dot is %.17g, expected %.17g", d, 6.0 * (double)PLO_COMBOS);

    pe_vec_free(&a);
    pe_vec_free(&b);
}

/* ------------------------------------------------------------------ *
 * Element-wise behaviour
 * ------------------------------------------------------------------ */

static void test_element_ops(void)
{
    pe_vec_t a, b;
    size_t i;

    CHECK(pe_vec_alloc(&a, 5) == PE_SOLVER_OK, "alloc failed");
    CHECK(pe_vec_alloc(&b, 5) == PE_SOLVER_OK, "alloc failed");

    pe_vec_fill(&a, 0.5);
    pe_vec_scale(&a, 2.0);
    for (i = 0; i < a.n; ++i)
        CHECK(a.v[i] == 1.0, "scale gave %.17g at %zu", a.v[i], i);

    pe_vec_fill(&b, 3.0);
    pe_vec_copy(&a, &b);
    for (i = 0; i < a.n; ++i)
        CHECK(a.v[i] == 3.0, "copy gave %.17g at %zu", a.v[i], i);

    pe_vec_fill(&a, 1.0);
    pe_vec_axpy(&a, 2.0, &b);          /* 1 + 2*3 */
    for (i = 0; i < a.n; ++i)
        CHECK(a.v[i] == 7.0, "axpy gave %.17g at %zu", a.v[i], i);

    pe_vec_free(&a);
    pe_vec_free(&b);
}

static void test_mismatched_lengths_do_nothing(void)
{
    pe_vec_t a, b;
    size_t i;

    CHECK(pe_vec_alloc(&a, 4) == PE_SOLVER_OK, "alloc failed");
    CHECK(pe_vec_alloc(&b, 7) == PE_SOLVER_OK, "alloc failed");
    pe_vec_fill(&a, 1.0);
    pe_vec_fill(&b, 9.0);

    /* Length is a caller error the hot path cannot report; what it must not do
       is write past the shorter vector or half-apply an operation. */
    pe_vec_copy(&a, &b);
    pe_vec_mul(&a, &b);
    pe_vec_axpy(&a, 2.0, &b);
    for (i = 0; i < a.n; ++i)
        CHECK(a.v[i] == 1.0, "a mismatched operation wrote %.17g at %zu", a.v[i], i);

    CHECK(pe_vec_dot(&a, &b) == 0.0, "a mismatched dot returned a value");

    pe_vec_free(&a);
    pe_vec_free(&b);
}

static void test_null_safety(void)
{
    pe_vec_t z;
    z.v = NULL;
    z.n = 0;

    pe_vec_fill(NULL, 1.0);
    pe_vec_fill(&z, 1.0);
    pe_vec_scale(NULL, 2.0);
    pe_vec_scale(&z, 2.0);
    pe_vec_mul(&z, &z);
    pe_vec_axpy(&z, 1.0, &z);
    pe_vec_copy(&z, &z);
    CHECK(pe_vec_sum(NULL) == 0.0, "sum of nothing");
    CHECK(pe_vec_sum(&z) == 0.0, "sum of an empty vector");
    CHECK(pe_vec_dot(NULL, &z) == 0.0, "dot with nothing");
}

int main(void)
{
    test_lifetime();
    test_uniform_reach_times_uniform_strategy();
    test_sum_accuracy();
    test_dot_accuracy();
    test_element_ops();
    test_mismatched_lengths_do_nothing();
    test_null_safety();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_vector: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_vector: per-combo vectors are accurate at range scale\n");
    return 0;
}
