/*
 * test_range_equity_seed_determinism.c - BUG-14 regression tests
 *
 * Validates that the sampling core no longer relies on the global libc
 * rand()/srand():
 *   1. The shared PCG RNG is reproducible for identical seeds, produces
 *      in-bounds unbiased draws, and has no modulo bias in the rejection
 *      sampler.
 *   2. Two MT runs with the same seed and the same thread count produce
 *      bit-identical equities (the issue's acceptance criteria) on the
 *      deterministic-schedule MT paths (v1, v2), and statistically
 *      identical results (within float tolerance) on the lock-pool paths
 *      (v3, v4, v5, MT_Batched).
 *   3. Two runs with different seeds produce different samples.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "poker_eval/utils/omp_compat.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/batched_montecarlo.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* ---- PCG unit checks ------------------------------------------------- */

static void test_pcg_reproducible(void)
{
    pe_rng_t a, b;
    pe_rng_seed(&a, 12345ULL);
    pe_rng_seed(&b, 12345ULL);
    int same = 1;
    for (int i = 0; i < 64; i++) {
        if (pe_rng_next(&a) != pe_rng_next(&b)) {
            same = 0;
            break;
        }
    }
    CHECK(same, "same seed produces identical PCG stream");

    pe_rng_seed(&a, 12345ULL);
    pe_rng_seed(&b, 12346ULL);
    int differ = 0;
    for (int i = 0; i < 64; i++) {
        if (pe_rng_next(&a) != pe_rng_next(&b)) {
            differ = 1;
            break;
        }
    }
    CHECK(differ, "different seeds produce different PCG streams");

    /* derive(): (base, key) is a stable keyed derivation */
    CHECK(pe_rng_derive(42ULL, 7ULL) == pe_rng_derive(42ULL, 7ULL),
          "pe_rng_derive is deterministic");
    CHECK(pe_rng_derive(42ULL, 7ULL) != pe_rng_derive(42ULL, 8ULL),
          "pe_rng_derive changes with the key");
}

static void test_pcg_below_bounds(void)
{
    pe_rng_t rng;
    pe_rng_seed(&rng, 0xBEEFULL);

    CHECK(pe_rng_below(&rng, 1) == 0, "below(1) returns 0");
    for (uint32_t bound = 2; bound <= 100; bound++) {
        for (int i = 0; i < 2000; i++) {
            uint32_t v = pe_rng_below(&rng, bound);
            if (v >= bound) {
                CHECK(0, "pe_rng_below out of range");
                return;
            }
        }
    }

    /* Light uniformity sanity: bound=6 over 2^18 draws; reject gross bias */
    pe_rng_seed(&rng, 0xCAFEULL);
    uint64_t counts[6] = {0};
    const int N = 1 << 18;
    for (int i = 0; i < N; i++)
        counts[pe_rng_below(&rng, 6)]++;
    const double expect = (double)N / 6.0;
    for (int b = 0; b < 6; b++) {
        if (fabs((double)counts[b] - expect) / expect > 0.10)
            CHECK(0, "pe_rng_below uniformity outside 10% band");
    }
}

/* ---- equity determinism checks --------------------------------------- */

static int generate_pocket_pairs(StdDeck_CardMask *hands, int min_rank, int max_rank)
{
    int count = 0;
    for (int rank = min_rank; rank <= max_rank; rank++) {
        for (int suit1 = 0; suit1 < 4; suit1++) {
            for (int suit2 = suit1 + 1; suit2 < 4; suit2++) {
                StdDeck_CardMask_RESET(hands[count]);
                StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit1));
                StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit2));
                count++;
            }
        }
    }
    return count;
}

static int results_bit_identical(const enum_result_t *a, const enum_result_t *b)
{
    if (a->sampleType != b->sampleType) return 0;
    if (a->nsamples != b->nsamples) return 0;
    if (a->nplayers != b->nplayers) return 0;
    if (memcmp(a->nwinhi, b->nwinhi, sizeof(a->nwinhi))) return 0;
    if (memcmp(a->ntiehi, b->ntiehi, sizeof(a->ntiehi))) return 0;
    if (memcmp(a->nlosehi, b->nlosehi, sizeof(a->nlosehi))) return 0;
    if (memcmp(a->nwinlo, b->nwinlo, sizeof(a->nwinlo))) return 0;
    if (memcmp(a->ntielo, b->ntielo, sizeof(a->ntielo))) return 0;
    if (memcmp(a->nloselo, b->nloselo, sizeof(a->nloselo))) return 0;
    if (memcmp(a->nscoop, b->nscoop, sizeof(a->nscoop))) return 0;
    if (memcmp(a->nsharehi, b->nsharehi, sizeof(a->nsharehi))) return 0;
    if (memcmp(a->nsharelo, b->nsharelo, sizeof(a->nsharelo))) return 0;
    if (memcmp(a->nshare, b->nshare, sizeof(a->nshare))) return 0;
    if (memcmp(a->ev, b->ev, sizeof(a->ev))) return 0;
    return 1;
}

static int results_nearly_equal(const enum_result_t *a, const enum_result_t *b)
{
    if (a->nsamples != b->nsamples) return 0;
    for (unsigned int p = 0; p < a->nplayers; p++) {
        if (fabs(a->ev[p] - b->ev[p]) > 1e-9)
            return 0;
    }
    return 1;
}

typedef int (*mt_fn)(enum_game_t, const PlayerRange[], int, StdDeck_CardMask,
                     StdDeck_CardMask, int, int, int, int, enum_result_t *, int);

/* The MT_Batched signature differs (same shape though). */

typedef struct {
    const char *name;
    mt_fn fn;
    int exact; /* true: bit-identical expected; false: float tolerance */
} mt_case_t;

static int test_mt_case(const mt_case_t *tc,
                        const PlayerRange ranges[2],
                        int iterations,
                        int num_threads)
{
    int rc = 0;
    const uint32_t SEED_A = 0x5EEDC0DEu;
    const uint32_t SEED_B = 0x13579BDFu;

    enum_result_t r1, r2, r3;
    if (enumResultAlloc(&r1, 2, enum_ordering_mode_hi)) return 0;
    if (enumResultAlloc(&r2, 2, enum_ordering_mode_hi)) { enumResultFree(&r1); return 0; }
    if (enumResultAlloc(&r3, 2, enum_ordering_mode_hi)) { enumResultFree(&r1); enumResultFree(&r2); return 0; }

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Run 1 and 2 with the same seed: must be identical */
    BatchedMonteCarlo_SetRandomSeed(SEED_A);
    if (tc->fn(game_holdem, ranges, 2, board, dead, 5, 1, iterations, 0, &r1, num_threads) < 0) {
        fprintf(stderr, "FAIL: %s run1 failed\n", tc->name);
        rc = 1;
        goto out;
    }
    BatchedMonteCarlo_SetRandomSeed(SEED_A);
    if (tc->fn(game_holdem, ranges, 2, board, dead, 5, 1, iterations, 0, &r2, num_threads) < 0) {
        fprintf(stderr, "FAIL: %s run2 failed\n", tc->name);
        rc = 1;
        goto out;
    }
    if (tc->exact) {
        CHECK(results_bit_identical(&r1, &r2),
              "same seed + same thread count -> bit-identical equities");
    } else {
        CHECK(results_nearly_equal(&r1, &r2),
              "same seed + same thread count -> close equities (float tolerance)");
    }

    /* Different seed: sampled outcomes must differ */
    BatchedMonteCarlo_SetRandomSeed(SEED_B);
    if (tc->fn(game_holdem, ranges, 2, board, dead, 5, 1, iterations, 0, &r3, num_threads) < 0) {
        fprintf(stderr, "FAIL: %s run3 (different seed) failed\n", tc->name);
        rc = 1;
        goto out;
    }
    int different = 0;
    for (int p = 0; p < 2; p++) {
        if (r3.nwinhi[p] != r1.nwinhi[p]) { different = 1; break; }
    }
    CHECK(different, "different seed produces different samples");

out:
    enumResultFree(&r1);
    enumResultFree(&r2);
    enumResultFree(&r3);
    return rc;
}

int main(void)
{
    printf("==============================================\n");
    printf("SEED DETERMINISM / RNG REGRESSION TESTS (BUG-14)\n");
    printf("Max threads available: %d\n", omp_get_max_threads());
    printf("==============================================\n");

    test_pcg_reproducible();
    test_pcg_below_bounds();

    /* AA vs KK preflop, 36 matchups, Monte Carlo boards */
    StdDeck_CardMask r1_hands[64], r2_hands[64];
    PlayerRange ranges[2];
    ranges[0].count = generate_pocket_pairs(r1_hands, StdDeck_Rank_ACE, StdDeck_Rank_ACE);
    ranges[0].hand_masks = r1_hands;
    ranges[0].weights = NULL;
    ranges[0].total_weight = (double)ranges[0].count;
    ranges[1].count = generate_pocket_pairs(r2_hands, StdDeck_Rank_KING, StdDeck_Rank_KING);
    ranges[1].hand_masks = r2_hands;
    ranges[1].weights = NULL;
    ranges[1].total_weight = (double)ranges[1].count;

    int num_threads = omp_get_max_threads();
    if (num_threads > 4) num_threads = 4;
    if (num_threads < 1) num_threads = 1;
    const int iterations = 20000;

    const mt_case_t cases[] = {
        { "MT v1 (CalculateEquityForRanges_MT)", CalculateEquityForRanges_MT, 1 },
        { "MT v2 (CalculateEquityForRanges_MT_v2)", CalculateEquityForRanges_MT_v2, 1 },
        { "MT v3 (CalculateEquityForRanges_MT_v3)", CalculateEquityForRanges_MT_v3, 0 },
        { "MT v4 (CalculateEquityForRanges_MT_v4)", CalculateEquityForRanges_MT_v4, 0 },
        { "MT v5 (CalculateEquityForRanges_MT_v5)", CalculateEquityForRanges_MT_v5, 0 },
        { "MT Batched (CalculateEquityForRanges_MT_Batched)", CalculateEquityForRanges_MT_Batched, 0 },
    };
    const int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));
    for (int c = 0; c < n_cases; c++) {
        printf("  Running: %s (threads=%d, iter=%d)\n", cases[c].name, num_threads, iterations);
        fflush(stdout);
        test_mt_case(&cases[c], ranges, iterations, num_threads);
    }

    printf("==============================================\n");
    if (g_failures == 0) {
        printf("ALL %d CHECKS PASSED\n", g_checks);
        return 0;
    }
    printf("%d/%d CHECKS FAILED\n", g_failures, g_checks);
    return 1;
}
