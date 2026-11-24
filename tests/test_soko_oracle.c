/*
 * test_soko_oracle.c - exhaustive Sökö oracle (#316 follow-up)
 *
 * Enumerates all C(52,5) = 2,598,960 five-card hands with the Sökö preset and
 * pins the resulting category histogram.  The expected counts were produced
 * by an independent reference implementation and cross-checked against the
 * textbook standard five-card frequencies: the nine shared categories keep
 * their standard counts, and the mass moved into the two Sökö-only categories
 * (110,940 four-card flushes + 92,208 four-card straights) is exactly what
 * one pair (1,098,240 -> 1,047,552) and high card (1,302,540 -> 1,150,080)
 * lose.
 *
 * This is the only test that checks the four-card categories at scale, not
 * just on hand-picked examples.
 */

#include "unity.h"
#include <string.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/distributions/hand_distribution.h>

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    const char *name;
    unsigned long long count;
} expected_category_t;

static const expected_category_t expected[] = {
    {"StraightFlush", 40ULL},
    {"FourOfAKind", 624ULL},
    {"FullHouse", 3744ULL},
    {"Flush", 5108ULL},
    {"Straight", 10200ULL},
    {"ThreeOfAKind", 54912ULL},
    {"TwoPair", 123552ULL},
    {"FourCardFlush", 110940ULL},
    {"FourCardStraight", 92208ULL},
    {"OnePair", 1047552ULL},
    {"HighCard", 1150080ULL},
};

#define EXPECTED_COUNT (sizeof(expected) / sizeof(expected[0]))

static const pe_category_stat_t *find_category(const pe_hand_distribution_t *dist,
                                               const char *name) {
    int i;

    for (i = 0; i < dist->num_categories; ++i)
        if (strcmp(dist->categories[i].category_name, name) == 0)
            return &dist->categories[i];
    return NULL;
}

static void test_soko_distribution_matches_oracle(void) {
    pe_hand_distribution_t dist;
    unsigned long long sum = 0;
    size_t i;
    int result;

    memset(&dist, 0, sizeof(dist));
    result = pe_compute_hand_distribution(game_soko, &dist);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "distribution failed");

    TEST_ASSERT_EQUAL_UINT64(2598960ULL, dist.total_combinations);
    TEST_ASSERT_EQUAL_INT((int)EXPECTED_COUNT, dist.num_categories);

    for (i = 0; i < EXPECTED_COUNT; ++i) {
        const pe_category_stat_t *stat = find_category(&dist, expected[i].name);

        TEST_ASSERT_NOT_NULL_MESSAGE(stat, expected[i].name);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(expected[i].count, stat->count,
                                         expected[i].name);
        sum += stat->count;
    }
    TEST_ASSERT_EQUAL_UINT64(dist.total_combinations, sum);

    /* Probabilities are exact counts over the total, within double rounding. */
    for (i = 0; i < EXPECTED_COUNT; ++i) {
        const pe_category_stat_t *stat = find_category(&dist, expected[i].name);
        double probability =
            (double)expected[i].count / (double)dist.total_combinations;

        TEST_ASSERT_DOUBLE_WITHIN(1e-12, probability, stat->probability);
    }
}

/* The two Sökö-only categories are exactly the hands standard rules call a
 * pair or high card; this ties the oracle to the ranking change itself. */
static void test_soko_only_mass_comes_from_pair_and_high_card(void) {
    pe_hand_distribution_t dist;

    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution(game_soko, &dist));

    TEST_ASSERT_EQUAL_UINT64(110940ULL,
                             find_category(&dist, "FourCardFlush")->count);
    TEST_ASSERT_EQUAL_UINT64(92208ULL,
                             find_category(&dist, "FourCardStraight")->count);

    /* Standard five-card counts: pair 1,098,240; high card 1,302,540. */
    TEST_ASSERT_EQUAL_UINT64(1098240ULL - 50688ULL,
                             find_category(&dist, "OnePair")->count);
    TEST_ASSERT_EQUAL_UINT64(1302540ULL - 152460ULL,
                             find_category(&dist, "HighCard")->count);
    TEST_ASSERT_EQUAL_UINT64(50688ULL + 152460ULL,
                             find_category(&dist, "FourCardFlush")->count +
                                 find_category(&dist, "FourCardStraight")->count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_soko_distribution_matches_oracle);
    RUN_TEST(test_soko_only_mass_comes_from_pair_and_high_card);
    return UNITY_END();
}
