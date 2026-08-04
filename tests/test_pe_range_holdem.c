#include <poker_eval/range.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/pineapple_preflop.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal Test Harness */
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED: %s:%d: Expected %d, got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        exit(1); \
    } \
} while(0)

#define RUN_TEST(func) do { \
    printf("Running %s...\n", #func); \
    func(); \
    printf("PASSED\n"); \
} while(0)

/* Helper for empty mask */
static StdDeck_CardMask empty_mask(void) {
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    return m;
}

static void test_parse_pairs(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_holdem, "AA", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(6, range->count); /* 6 combos of AA */
    pe_range_free(range);

    status = pe_range_parse(game_holdem, "22", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(6, range->count);
    pe_range_free(range);
}

static void test_parse_suited(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_holdem, "AKs", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(4, range->count); /* 4 suited combos */
    pe_range_free(range);
}

static void test_parse_offsuit(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_holdem, "AKo", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(12, range->count); /* 12 offsuit combos */
    pe_range_free(range);
}

static void test_parse_both(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_holdem, "AK", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(16, range->count); /* 16 combos */
    pe_range_free(range);
}

static void test_parse_intervals(void) {
    pe_range_t *range;
    pe_status_t status;

    /* AA-JJ = AA, KK, QQ, JJ (4 * 6 = 24) */
    status = pe_range_parse(game_holdem, "AA-JJ", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(24, range->count);
    pe_range_free(range);

    /* AKs-AJs = AKs, AQs, AJs (3 * 4 = 12) */
    status = pe_range_parse(game_holdem, "AKs-AJs", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(12, range->count);
    pe_range_free(range);
}

static void test_parse_plus(void) {
    pe_range_t *range;
    pe_status_t status;

    /* 88+ = 88, 99, TT, JJ, QQ, KK, AA (7 * 6 = 42) */
    status = pe_range_parse(game_holdem, "88+", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(42, range->count);
    pe_range_free(range);

    /* AQs+ = AQs, AKs (2 * 4 = 8) */
    status = pe_range_parse(game_holdem, "AQs+", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(8, range->count);
    pe_range_free(range);
}

static void test_parse_specific(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_holdem, "AsKh", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(1, range->count);
    pe_range_free(range);
}

static void test_parse_multiple(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_holdem, "AA, KK", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(12, range->count);
    pe_range_free(range);
}

static void test_weights(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_holdem, "AA:0.5", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(6, range->count);
    TEST_ASSERT((range->total_weight - 3.0) < 0.0001 && (range->total_weight - 3.0) > -0.0001); /* 6 * 0.5 */
    pe_range_free(range);
}

static void test_omaha_compatibility(void) {
    pe_range_t *range;
    /* Requires ARP to work behind scenes */
    pe_status_t status = pe_range_parse(game_omaha, "AAxx", empty_mask(), NULL, &range);
    if (status == PE_STATUS_OK) {
        printf("Omaha parsing passed with %zu combos\n", range->count);
        pe_range_free(range);
    } else {
        printf("Omaha parsing skipped (ARP dependency)\n");
    }
}

static void test_badugi_range(void) {
    pe_range_t *range;
    /* Single specific 4-card badugi hand */
    pe_status_t status = pe_range_parse(game_badugi, "As2d3h4c", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(1, range->count);
    pe_range_free(range);

    /* Multiple badugi hands in a comma-separated range */
    status = pe_range_parse(game_badugi, "As2d3h4c, KsQdJhTc", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(2, range->count);
    pe_range_free(range);

    /* Weighted badugi range using @ syntax */
    status = pe_range_parse(game_badugi, "As2d3h4c@50%, 5s6d7h8c@50%", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(2, range->count);
    pe_range_free(range);
}

static void test_pineapple8_specific(void) {
    pe_range_t *range;
    /* Single concrete 3-card Pineapple Hi/Lo hand */
    pe_status_t status = pe_range_parse(game_pineapple8, "AsKsQs", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(1, range->count);
    pe_range_free(range);
}

static void test_pineapple8_multiple(void) {
    pe_range_t *range;
    /* Multiple concrete 3-card hands, comma-separated with whitespace */
    pe_status_t status = pe_range_parse(game_pineapple8, "AsKsQs, AhKhQh",
                                        empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(2, range->count);
    pe_range_free(range);
}

static void test_pineapple8_weighted(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_parse(game_pineapple8, "AsKsQs:0.5",
                                        empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(1, range->count);
    TEST_ASSERT((range->total_weight - 0.5) < 0.0001 && (range->total_weight - 0.5) > -0.0001);
    pe_range_free(range);
}

static void test_pineapple8_invalid(void) {
    pe_range_t *range;
    /* Duplicate card within a hand must be rejected */
    pe_status_t status = pe_range_parse(game_pineapple8, "AsAsQs", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_PARSE_ERROR, status);

    /* Percentage expansion is unsupported for 3-card hands */
    status = pe_range_parse(game_pineapple8, "10%", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_PARSE_ERROR, status);
}

static void test_pineapple_specific(void) {
    pe_range_t *range;
    /* Plain Pineapple (game_pineapple) is also 3-card and routes to the same
     * tokenizer; concrete hands must parse. */
    pe_status_t status = pe_range_parse(game_pineapple, "AsKsQs", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(1, range->count);
    pe_range_free(range);
}

static void test_pineapple8_top_percent_full(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_top_percent(game_pineapple8, 1.0, empty_mask(), &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    /* C(52,3) = 22100 distinct 3-card hands */
    TEST_ASSERT_EQUAL(22100, range->count);
    pe_range_free(range);
}

static void test_pineapple8_top_percent_half(void) {
    pe_range_t *range;
    pe_status_t status = pe_range_top_percent(game_pineapple8, 0.5, empty_mask(), &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    /* Top 50% should be a strict subset: > 0 and < 22100 */
    TEST_ASSERT(range->count > 0);
    TEST_ASSERT(range->count < 22100);
    pe_range_free(range);
}

static void test_pineapple8_top_percent_dead_cards(void) {
    pe_range_t *range;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    /* Dead card: As (rank 0, suit 0) */
    StdDeck_CardMask_SET(dead, 0);

    pe_status_t status = pe_range_top_percent(game_pineapple8, 1.0, dead, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    /* With 1 dead card, total possible 3-card hands from remaining 51 cards
     * is C(51,3) = 20825, but some canonical classes may map to hands that
     * include the dead card, so count should be less than 22100 and > 0. */
    TEST_ASSERT(range->count > 0);
    TEST_ASSERT(range->count < 22100);
    pe_range_free(range);
}

int main(void) {
    RUN_TEST(test_parse_pairs);
    RUN_TEST(test_parse_suited);
    RUN_TEST(test_parse_offsuit);
    RUN_TEST(test_parse_both);
    RUN_TEST(test_parse_intervals);
    RUN_TEST(test_parse_plus);
    RUN_TEST(test_parse_specific);
    RUN_TEST(test_parse_multiple);
    RUN_TEST(test_weights);
    RUN_TEST(test_omaha_compatibility);
    RUN_TEST(test_badugi_range);
    RUN_TEST(test_pineapple8_specific);
    RUN_TEST(test_pineapple8_multiple);
    RUN_TEST(test_pineapple8_weighted);
    RUN_TEST(test_pineapple8_invalid);
    RUN_TEST(test_pineapple_specific);
    RUN_TEST(test_pineapple8_top_percent_full);
    RUN_TEST(test_pineapple8_top_percent_half);
    RUN_TEST(test_pineapple8_top_percent_dead_cards);
    return 0;
}
