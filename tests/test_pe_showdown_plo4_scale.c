/*
 * test_pe_showdown_plo4_scale.c - PLO4 sweep on a full river range
 *
 * A PLO4 river leaves 47 live cards after a five-card board, and C(47, 4) is
 * exactly 178,365: the range size the #282 acceptance criteria talk about.
 * This test runs the sweep with both ranges at that size, compares a sample of
 * heroes against the pairwise oracle, and prints the wall-clock time and the
 * peak RSS increase, so the speedup in the pull request is a measurement
 * rather than an extrapolation.
 *
 * Strengths are synthetic (a cheap hash folded to 200 values) so the range has
 * realistic tie groups without paying for 178k Omaha evaluations; the sweep
 * only ever compares strengths, it never interprets them.
 */

#include <poker_eval/solver/pe_showdown.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

#define LIVE_CARDS 47
#define RANGE_COMBOS 178365u /* C(47, 4) */
#define SAMPLE_HEROES 64u

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                            \
        }                                                          \
    } while (0)

static double peak_rss_mb(void)
{
#if defined(_WIN32)
    return 0.0;
#else
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0.0;
#if defined(__APPLE__)
    return (double)usage.ru_maxrss / (1024.0 * 1024.0);
#else
    return (double)usage.ru_maxrss / 1024.0;
#endif
#endif
}

/* Every four-card hand from the live cards, in lexicographic order. */
static size_t fill_range(mask_t *hands, int64_t *strength)
{
    size_t count = 0u;
    int a, b, c, d;

    for (a = 0; a < LIVE_CARDS; ++a)
        for (b = a + 1; b < LIVE_CARDS; ++b)
            for (c = b + 1; c < LIVE_CARDS; ++c)
                for (d = c + 1; d < LIVE_CARDS; ++d)
                {
                    uint32_t mixed;
                    mask_t hand = ((mask_t)1 << a) | ((mask_t)1 << b) |
                                  ((mask_t)1 << c) | ((mask_t)1 << d);
                    if (count >= RANGE_COMBOS)
                        return count;
                    hands[count] = hand;
                    mixed = (uint32_t)(hand ^ (hand >> 32));
                    mixed ^= mixed >> 13;
                    mixed *= 2654435761u;
                    strength[count] = (int64_t)(mixed % 200u);
                    ++count;
                }
    return count;
}

int main(void)
{
    mask_t *hero_masks = (mask_t *)malloc((size_t)RANGE_COMBOS * sizeof(*hero_masks));
    mask_t *opp_masks = (mask_t *)malloc((size_t)RANGE_COMBOS * sizeof(*opp_masks));
    int64_t *hero_strength =
        (int64_t *)malloc((size_t)RANGE_COMBOS * sizeof(*hero_strength));
    int64_t *opp_strength =
        (int64_t *)malloc((size_t)RANGE_COMBOS * sizeof(*opp_strength));
    double *opp_reach = (double *)malloc((size_t)RANGE_COMBOS * sizeof(*opp_reach));
    mask_t sample_masks[SAMPLE_HEROES];
    int64_t sample_strength[SAMPLE_HEROES];
    double *expected = (double *)malloc(SAMPLE_HEROES * sizeof(*expected));
    pe_value_vec_t sweep = {0};
    pe_value_vec_t oracle = {0};
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    size_t combos;
    size_t i;
    size_t k;
    double rss_before;
    double rss_after;
    clock_t start;
    double sweep_ms;
    double oracle_ms;
    size_t mismatches = 0u;

    CHECK(hero_masks && opp_masks && hero_strength && opp_strength && opp_reach &&
              expected,
          "range allocation failed");
    if (!hero_masks || !opp_masks || !hero_strength || !opp_strength ||
        !opp_reach || !expected)
        goto done;

    combos = fill_range(hero_masks, hero_strength);
    CHECK(combos == RANGE_COMBOS, "enumerated %zu hands, expected %u", combos,
          (unsigned)RANGE_COMBOS);
    if (combos != RANGE_COMBOS)
        goto done;
    memcpy(opp_masks, hero_masks, combos * sizeof(*opp_masks));
    memcpy(opp_strength, hero_strength, combos * sizeof(*opp_strength));
    for (i = 0; i < combos; ++i)
        opp_reach[i] = 0.5 + (double)(i % 7u) * 0.05;

    CHECK(pe_vec_alloc(&sweep, combos) == PE_SOLVER_OK,
          "sweep allocation failed");
    CHECK(pe_vec_alloc(&oracle, SAMPLE_HEROES) == PE_SOLVER_OK,
          "oracle allocation failed");
    if (!sweep.v || !oracle.v)
        goto done;

    rss_before = peak_rss_mb();
    start = clock();
    CHECK(pe_showdown_vector(hero_masks, hero_strength, combos, opp_masks,
                             opp_strength, opp_reach, combos, MASK_EMPTY, 7.5,
                             &sweep, &path) == PE_SOLVER_OK,
          "full-range sweep failed");
    sweep_ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
    rss_after = peak_rss_mb();
    CHECK(path == PE_SHOWDOWN_PATH_PLO4, "full-range path is %d", (int)path);

    for (k = 0; k < SAMPLE_HEROES; ++k)
    {
        size_t index = (k * combos) / SAMPLE_HEROES;

        sample_masks[k] = hero_masks[index];
        sample_strength[k] = hero_strength[index];
        expected[k] = sweep.v[index];
    }
    start = clock();
    CHECK(pe_showdown_vector_pairwise(sample_masks, sample_strength,
                                      SAMPLE_HEROES, opp_masks, opp_strength,
                                      opp_reach, combos, MASK_EMPTY, 7.5,
                                      &oracle) == PE_SOLVER_OK,
          "sampled oracle failed");
    oracle_ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
    for (k = 0; k < SAMPLE_HEROES; ++k)
        if (fabs(expected[k] - oracle.v[k]) >
            1e-12 * (1.0 + fabs(oracle.v[k])))
        {
            if (mismatches < 3u)
                fprintf(stderr, "  hero %zu: sweep %.17g oracle %.17g\n", k,
                        expected[k], oracle.v[k]);
            ++mismatches;
        }
    CHECK(mismatches == 0u, "%zu sampled heroes disagree with the oracle",
          mismatches);

    /* The oracle's cost is exactly hero_n * opp_n, so the rate measured on 64
       heroes gives the full-range figure without running it. */
    {
        double pairs = (double)SAMPLE_HEROES * (double)combos;
        double oracle_ns_per_pair = oracle_ms * 1e6 / pairs;
        double full_seconds =
            oracle_ns_per_pair * (double)combos * (double)combos / 1e9;

        printf("test_pe_showdown_plo4_scale: %u x %u combos, sweep %.1f ms, "
               "peak RSS +%.1f MiB (%u sampled heroes)\n",
               (unsigned)combos, (unsigned)combos, sweep_ms,
               rss_after > rss_before ? rss_after - rss_before : 0.0,
               (unsigned)SAMPLE_HEROES);
        printf("test_pe_showdown_plo4_scale: oracle %.1f ns/pair => full "
               "pairwise ~%.1f s\n",
               oracle_ns_per_pair, full_seconds);
    }
    /* The subset table is capped at 2^20 slots and the two tie tables exist
       alongside it; 64 MiB is a loose ceiling that still catches an accidental
       per-pair allocation. */
    CHECK(rss_after - rss_before < 64.0,
          "sweep grew peak RSS by %.1f MiB", rss_after - rss_before);

done:
    pe_vec_free(&oracle);
    pe_vec_free(&sweep);
    free(expected);
    free(opp_reach);
    free(opp_strength);
    free(hero_strength);
    free(opp_masks);
    free(hero_masks);
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_showdown_plo4_scale: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
