#include <poker_eval/range.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
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

static void test_compile(void) {
    pe_range_t *range;
    /* Parse redundant range: AA, AA, AA */
    pe_status_t status = pe_range_parse(game_holdem, "AA, AA, AA", empty_mask(), NULL, &range);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(18, range->count); /* 3 * 6 = 18 */

    pe_compiled_range_t *compiled;
    status = pe_range_compile(range, NULL, &compiled);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(6, compiled->count); /* Should dedup to 6 unique hands */

    /* Weights should sum up (default 1.0 -> 3.0) */
    TEST_ASSERT((compiled->combos[0].weight - 3.0) < 0.0001);

    pe_range_free(range);
    pe_range_free(compiled);
}

static void test_union(void) {
    pe_range_t *r1, *r2, *res;
    /* AA + KK */
    pe_range_parse(game_holdem, "AA", empty_mask(), NULL, &r1);
    pe_range_parse(game_holdem, "KK", empty_mask(), NULL, &r2);

    pe_status_t status = pe_range_combine(r1, r2, PE_OP_UNION, &res);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(12, res->count); /* 6+6 */

    pe_range_free(r1);
    pe_range_free(r2);
    pe_range_free(res);
}

static void test_intersection(void) {
    pe_range_t *r1, *r2, *res;
    /* AA,KK vs KK,QQ -> Intersect is KK */
    pe_range_parse(game_holdem, "AA, KK", empty_mask(), NULL, &r1);
    pe_range_parse(game_holdem, "KK, QQ", empty_mask(), NULL, &r2);

    pe_status_t status = pe_range_combine(r1, r2, PE_OP_INTERSECT, &res);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(6, res->count); /* KK only */

    pe_range_free(r1);
    pe_range_free(r2);
    pe_range_free(res);
}

static void test_difference(void) {
    pe_range_t *r1, *r2, *res;
    /* AA,KK minus KK -> AA */
    pe_range_parse(game_holdem, "AA, KK", empty_mask(), NULL, &r1);
    pe_range_parse(game_holdem, "KK", empty_mask(), NULL, &r2);

    pe_status_t status = pe_range_combine(r1, r2, PE_OP_DIFFERENCE, &res);
    TEST_ASSERT_EQUAL(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL(6, res->count); /* AA only */

    pe_range_free(r1);
    pe_range_free(r2);
    pe_range_free(res);
}

int main(void) {
    RUN_TEST(test_compile);
    RUN_TEST(test_union);
    RUN_TEST(test_intersection);
    RUN_TEST(test_difference);
    return 0;
}
