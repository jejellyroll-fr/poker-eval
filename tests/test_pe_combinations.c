/*
 * test_pe_combinations.c - CHN-02: a flop is a combination, not a sequence
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The property the flop chance node depends on: the map from index to
 * three-card subset is a bijection. If two indices produced the same three
 * cards in a different order, that flop would be dealt twice and weighted
 * twice — six times, for all six orderings — which is six times the
 * probability the deck actually gives it. Nothing would crash; every solve
 * from a preflop start would simply be wrong.
 *
 * So this checks the whole of C(48,3): 17296 indices, 17296 distinct subsets,
 * each strictly increasing, and every one of them found again by the inverse.
 */

#include <poker_eval/solver/pe_combinations.h>

#include <stdio.h>
#include <stdlib.h>
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

static void test_counts(void)
{
    CHECK(pe_comb_count(48, 3) == 17296, "C(48,3) is %llu, expected 17296",
          (unsigned long long)pe_comb_count(48, 3));
    CHECK(pe_comb_count(52, 3) == 22100, "C(52,3) is %llu, expected 22100",
          (unsigned long long)pe_comb_count(52, 3));
    CHECK(pe_comb_count(52, 2) == 1326, "C(52,2) is %llu, expected 1326",
          (unsigned long long)pe_comb_count(52, 2));
    CHECK(pe_comb_count(52, 4) == 270725, "C(52,4) is %llu, expected 270725",
          (unsigned long long)pe_comb_count(52, 4));
    CHECK(pe_comb_count(52, 5) == 2598960, "C(52,5) is %llu, expected 2598960",
          (unsigned long long)pe_comb_count(52, 5));

    CHECK(pe_comb_count(3, 5) == 0, "choosing more than there is");
    CHECK(pe_comb_count(5, 0) == 1, "the empty subset");
    CHECK(pe_comb_count(5, 5) == 1, "the whole set");
}

/*
 * The bijection, checked over the entire flop space.
 *
 * A bitset of the 3-card masks catches a repeat wherever it comes from — a
 * duplicated subset, or the same cards emitted in another order. Counting
 * distinct subsets and comparing to 17296 would not: two indices could collide
 * while a third produced something outside the space, and the totals would
 * still match.
 */
static void test_flop_space_is_a_bijection(void)
{
    const unsigned n = 48;
    const unsigned k = 3;
    uint64_t total = pe_comb_count(n, k);
    unsigned char *seen;
    uint64_t r;
    uint64_t duplicates = 0;
    uint64_t misordered = 0;
    uint64_t bad_roundtrip = 0;

    CHECK(total == 17296, "the flop space is %llu wide", (unsigned long long)total);

    /* One bit per (c0,c1,c2) triple, addressed as c0*48*48 + c1*48 + c2. */
    seen = (unsigned char *)calloc((size_t)n * n * n, 1);
    CHECK(seen != NULL, "allocation failed");
    if (!seen) return;

    for (r = 0; r < total; ++r)
    {
        unsigned c[PE_COMB_MAX_K];
        size_t slot;
        uint64_t back = 0;

        if (pe_comb_unrank(n, k, r, c) != PE_SOLVER_OK)
        {
            CHECK(0, "unrank failed at %llu", (unsigned long long)r);
            break;
        }

        if (!(c[0] < c[1] && c[1] < c[2] && c[2] < n))
        {
            misordered++;
            continue;
        }

        slot = (size_t)c[0] * n * n + (size_t)c[1] * n + (size_t)c[2];
        if (seen[slot])
            duplicates++;
        seen[slot] = 1;

        if (pe_comb_rank(n, k, c, &back) != PE_SOLVER_OK || back != r)
            bad_roundtrip++;
    }

    CHECK(misordered == 0, "%llu subsets came back unordered",
          (unsigned long long)misordered);
    CHECK(duplicates == 0, "%llu flops were produced more than once",
          (unsigned long long)duplicates);
    CHECK(bad_roundtrip == 0, "%llu subsets did not rank back to their index",
          (unsigned long long)bad_roundtrip);

    printf("    C(48,3) = %llu flops, all distinct, all strictly increasing\n",
           (unsigned long long)total);
    free(seen);
}

/*
 * Colexicographic order has one property the flop node relies on: the ranks of
 * the k-subsets of a smaller deck are a prefix of those of a larger one. The
 * number of unused cards changes from node to node, and without this an index
 * would mean a different flop depending on how many cards were left.
 */
static void test_prefix_stability(void)
{
    unsigned small[PE_COMB_MAX_K];
    unsigned large[PE_COMB_MAX_K];
    uint64_t r;
    uint64_t limit = pe_comb_count(20, 3);

    for (r = 0; r < limit; ++r)
    {
        CHECK(pe_comb_unrank(20, 3, r, small) == PE_SOLVER_OK, "small unrank");
        CHECK(pe_comb_unrank(52, 3, r, large) == PE_SOLVER_OK, "large unrank");
        if (memcmp(small, large, 3 * sizeof(unsigned)) != 0)
        {
            CHECK(0, "rank %llu means {%u,%u,%u} in a 20-card deck and "
                     "{%u,%u,%u} in a 52-card one",
                  (unsigned long long)r, small[0], small[1], small[2],
                  large[0], large[1], large[2]);
            break;
        }
    }
}

static void test_degenerate(void)
{
    unsigned out[PE_COMB_MAX_K];
    uint64_t rank = 0;
    unsigned values[3] = { 0, 1, 2 };
    unsigned unsorted[3] = { 2, 1, 0 };
    unsigned repeated[3] = { 1, 1, 2 };

    CHECK(pe_comb_unrank(48, 3, 17296, out) == PE_SOLVER_ERR_INVALID_CONFIG,
          "a rank past the end was accepted");
    CHECK(pe_comb_unrank(48, 0, 0, out) == PE_SOLVER_ERR_INVALID_CONFIG, "k = 0");
    CHECK(pe_comb_unrank(48, PE_COMB_MAX_K + 1, 0, out)
              == PE_SOLVER_ERR_INVALID_CONFIG, "k above the limit");
    CHECK(pe_comb_unrank(2, 3, 0, out) == PE_SOLVER_ERR_INVALID_CONFIG, "k > n");
    CHECK(pe_comb_unrank(48, 3, 0, NULL) == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL out");

    CHECK(pe_comb_rank(48, 3, values, &rank) == PE_SOLVER_OK && rank == 0,
          "the first subset does not rank 0");
    /* Order is part of the contract: a caller that hands over a set in the
       wrong order gets an error, not a plausible wrong index. */
    CHECK(pe_comb_rank(48, 3, unsorted, &rank) == PE_SOLVER_ERR_INVALID_CONFIG,
          "a descending subset was ranked");
    CHECK(pe_comb_rank(48, 3, repeated, &rank) == PE_SOLVER_ERR_INVALID_CONFIG,
          "a subset with a repeat was ranked");
    CHECK(pe_comb_rank(48, 3, NULL, &rank) == PE_SOLVER_ERR_NULL_ARGUMENT, "NULL");
}

int main(void)
{
    test_counts();
    test_flop_space_is_a_bijection();
    test_prefix_stability();
    test_degenerate();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_pe_combinations: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_pe_combinations: the flop space is indexed without repeats\n");
    return 0;
}
