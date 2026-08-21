/*
 * test_pe_rng.c - CTR-05 reproducible stream derivation
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The property that matters is not "the numbers look random" — PCG32 is
 * already tested elsewhere — but that a stream is a pure function of its
 * coordinate. Everything downstream depends on it: PAR-04 asks for a
 * bit-identical result across 1, 2, 4 and 8 threads, and that is only possible
 * if the numbers a worker draws are decided by which coordinate it was given,
 * never by when it asked or by what another worker did first.
 *
 * The sharpest check here is the one for field collisions. A derivation that
 * folded the four coordinates together commutatively would pass every "is it
 * deterministic" test and still hand two different threads the same stream.
 */

#include <poker_eval/solver/pe_rng.h>

#include <stdio.h>
#include <string.h>

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

#define DRAWS 64

static void take(pe_rng_t rng, uint32_t *out)
{
    int i;
    for (i = 0; i < DRAWS; ++i)
        out[i] = pe_rng_next(&rng);
}

static int same_sequence(const uint32_t *a, const uint32_t *b)
{
    return memcmp(a, b, sizeof(uint32_t) * DRAWS) == 0;
}

/* ------------------------------------------------------------------ *
 * A stream is a pure function of its coordinate
 * ------------------------------------------------------------------ */

static void test_same_coordinate_same_stream(void)
{
    uint32_t a[DRAWS];
    uint32_t b[DRAWS];

    take(pe_solver_rng_stream(42, 3, 100, 1, 7), a);
    take(pe_solver_rng_stream(42, 3, 100, 1, 7), b);
    CHECK(same_sequence(a, b), "the same coordinate produced two different streams");

    take(pe_solver_rng_root(42), a);
    take(pe_solver_rng_root(42), b);
    CHECK(same_sequence(a, b), "the same seed produced two different root streams");
}

static void test_seed_changes_the_stream(void)
{
    uint32_t a[DRAWS];
    uint32_t b[DRAWS];

    take(pe_solver_rng_stream(1, 0, 0, 0, 0), a);
    take(pe_solver_rng_stream(2, 0, 0, 0, 0), b);
    CHECK(!same_sequence(a, b), "two seeds produced the same stream");
}

/* ------------------------------------------------------------------ *
 * Coordinates do not collide, field by field
 * ------------------------------------------------------------------ */

static void test_fields_are_distinguishable(void)
{
    /* Four coordinates carrying the same value in a different field. A
       commutative derivation would give all four the same key. */
    uint64_t k_thread    = pe_solver_rng_key(1, 0, 0, 0);
    uint64_t k_iteration = pe_solver_rng_key(0, 1, 0, 0);
    uint64_t k_player    = pe_solver_rng_key(0, 0, 1, 0);
    uint64_t k_sample    = pe_solver_rng_key(0, 0, 0, 1);

    CHECK(k_thread != k_iteration, "thread and iteration collide");
    CHECK(k_thread != k_player, "thread and player collide");
    CHECK(k_thread != k_sample, "thread and sample collide");
    CHECK(k_iteration != k_player, "iteration and player collide");
    CHECK(k_iteration != k_sample, "iteration and sample collide");
    CHECK(k_player != k_sample, "player and sample collide");
}

static void test_neighbouring_threads_are_independent(void)
{
    uint32_t a[DRAWS];
    uint32_t b[DRAWS];
    int t;

    /* Adjacent thread ids are the case that matters: a weak derivation often
       leaves them correlated, and that is exactly how a parallel run acquires
       a bias nobody notices. */
    for (t = 0; t < 16; ++t)
    {
        take(pe_solver_rng_stream(7, (uint32_t)t, 5, 0, 0), a);
        take(pe_solver_rng_stream(7, (uint32_t)(t + 1), 5, 0, 0), b);
        CHECK(!same_sequence(a, b), "threads %d and %d share a stream", t, t + 1);
        CHECK(a[0] != b[0], "threads %d and %d start on the same value", t, t + 1);
    }
}

static void test_no_duplicate_keys_over_a_grid(void)
{
    /* 8 threads x 32 iterations x 4 players x 4 samples = 4096 coordinates.
       Every key must be distinct; a duplicate means two parts of one solve
       drawing identical numbers. */
    enum { N = 8 * 32 * 4 * 4 };
    static uint64_t keys[N];
    int n = 0;
    uint32_t th;
    uint64_t it;
    uint32_t pl;
    uint64_t sa;
    int i;
    int j;
    int duplicates = 0;

    for (th = 0; th < 8; ++th)
        for (it = 0; it < 32; ++it)
            for (pl = 0; pl < 4; ++pl)
                for (sa = 0; sa < 4; ++sa)
                    keys[n++] = pe_solver_rng_key(th, it, pl, sa);

    CHECK(n == N, "grid size mismatch");

    for (i = 0; i < n && duplicates == 0; ++i)
        for (j = i + 1; j < n; ++j)
            if (keys[i] == keys[j])
            {
                duplicates++;
                CHECK(0, "keys %d and %d collide (0x%llx)", i, j,
                      (unsigned long long)keys[i]);
                break;
            }
}

/* ------------------------------------------------------------------ *
 * No shared state
 * ------------------------------------------------------------------ */

static void test_generators_do_not_interfere(void)
{
    pe_rng_t x = pe_solver_rng_stream(11, 0, 0, 0, 0);
    pe_rng_t y = pe_solver_rng_stream(11, 1, 0, 0, 0);
    uint32_t interleaved_x[DRAWS];
    uint32_t interleaved_y[DRAWS];
    uint32_t alone_x[DRAWS];
    uint32_t alone_y[DRAWS];
    int i;

    /* Drawing from two generators in alternation must give each of them
       exactly what it would have produced alone. Any shared state — a global
       counter, a thread-local cache — breaks this and nothing else would
       notice until a parallel run stopped reproducing. */
    for (i = 0; i < DRAWS; ++i)
    {
        interleaved_x[i] = pe_rng_next(&x);
        interleaved_y[i] = pe_rng_next(&y);
    }

    take(pe_solver_rng_stream(11, 0, 0, 0, 0), alone_x);
    take(pe_solver_rng_stream(11, 1, 0, 0, 0), alone_y);

    CHECK(same_sequence(interleaved_x, alone_x),
          "interleaving changed what the first generator produced");
    CHECK(same_sequence(interleaved_y, alone_y),
          "interleaving changed what the second generator produced");
}

static void test_derivation_ignores_generator_progress(void)
{
    pe_rng_t root = pe_solver_rng_root(99);
    uint32_t before[DRAWS];
    uint32_t after[DRAWS];
    int i;

    take(pe_solver_rng_stream(99, 2, 3, 1, 0), before);

    /* Advance a generator seeded from the same root a long way. Derivation
       takes the seed, not a live state, so nothing about this can matter. */
    for (i = 0; i < 100000; ++i)
        (void)pe_rng_next(&root);

    take(pe_solver_rng_stream(99, 2, 3, 1, 0), after);
    CHECK(same_sequence(before, after),
          "stream derivation depends on how far a generator has advanced");
}

/* ------------------------------------------------------------------ *
 * The bounded draw stays usable
 * ------------------------------------------------------------------ */

static void test_bounded_draws(void)
{
    pe_rng_t rng = pe_solver_rng_stream(5, 0, 0, 0, 0);
    int counts[6];
    int i;
    int min = 1 << 30;
    int max = 0;

    memset(counts, 0, sizeof(counts));
    for (i = 0; i < 60000; ++i)
    {
        uint32_t v = pe_rng_below(&rng, 6);
        CHECK(v < 6, "pe_rng_below(6) returned %u", (unsigned)v);
        if (v < 6)
            counts[v]++;
    }

    for (i = 0; i < 6; ++i)
    {
        if (counts[i] < min) min = counts[i];
        if (counts[i] > max) max = counts[i];
    }

    /* Loose on purpose: this checks the derived stream is not degenerate, not
       the quality of PCG32, which is tested with the generator itself. */
    CHECK(min > 9000 && max < 11000,
          "6-sided draws are skewed: min %d, max %d over 60000", min, max);

    for (i = 0; i < 1000; ++i)
    {
        double u = pe_rng_uniform01(&rng);
        CHECK(u >= 0.0 && u < 1.0, "uniform01 returned %f", u);
    }
}

int main(void)
{
    test_same_coordinate_same_stream();
    test_seed_changes_the_stream();
    test_fields_are_distinguishable();
    test_neighbouring_threads_are_independent();
    test_no_duplicate_keys_over_a_grid();
    test_generators_do_not_interfere();
    test_derivation_ignores_generator_progress();
    test_bounded_draws();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_rng: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_rng: stream derivation is reproducible and collision-free\n");
    return 0;
}
