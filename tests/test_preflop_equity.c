/*
 * test_preflop_equity.c - Unit tests for pre-flop equity calculation
 *
 * Tests the real equity calculation implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "unity.h"

#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity.h>
#include <poker_eval/range.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_short.h>

#define EPSILON 0.02  /* 2% tolerance for equity comparisons */

static preflop_lookup_table_t *g_lookup_table = NULL;

void setUp(void)
{
    if (!g_lookup_table) {
        g_lookup_table = preflop_lookup_table_load_default();
    }
    TEST_ASSERT_NOT_NULL(g_lookup_table);
}

void tearDown(void)
{
}

static void init_lookup_table(void)
{
    if (!g_lookup_table) {
        g_lookup_table = preflop_lookup_table_load_default();
    }
}

static void cleanup_lookup_table(void)
{
    preflop_lookup_table_free(g_lookup_table);
    g_lookup_table = NULL;
}

static int parse_card_token(enum_game_t game, const char *token, int *card)
{
    char buf[8];
    size_t len = strlen(token);
    if (len == 0 || len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, token, len + 1);

    if (game == game_sdholdem) {
        return ShortDeck_stringToCard(buf, card) > 0;
    }
    return StdDeck_stringToCard(buf, card) > 0;
}

static int build_hand_mask(enum_game_t game, const char *cards, StdDeck_CardMask *mask)
{
    StdDeck_CardMask_RESET(*mask);

    char *copy = strdup(cards);
    if (!copy) {
        return 0;
    }

    char *token = strtok(copy, " ");
    if (!token) {
        free(copy);
        return 0;
    }

    while (token) {
        int card = -1;
        if (!parse_card_token(game, token, &card)) {
            free(copy);
            return 0;
        }
        StdDeck_CardMask_SET(*mask, card);
        token = strtok(NULL, " ");
    }

    free(copy);
    return 1;
}

static pe_range_t *range_from_cards(enum_game_t game, const char *cards)
{
    pe_range_t *range = NULL;
    if (pe_range_create(game, &range) != PE_STATUS_OK) {
        return NULL;
    }

    StdDeck_CardMask hand;
    if (!build_hand_mask(game, cards, &hand)) {
        pe_range_free(range);
        return NULL;
    }

    range->combos[0].hand = hand;
    range->combos[0].weight = 1.0;
    range->count = 1;
    range->total_weight = 1.0;
    return range;
}

/* Test 1: AA vs KK classic matchup */
static void test_aa_vs_kk(void)
{
    printf("Test 1: AA vs KK... ");
    fflush(stdout);

    preflop_range_t range1, range2;
    preflop_range_parse("AA", &range1);
    preflop_range_parse("KK", &range2);

    preflop_equity_input_t input = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = g_lookup_table
    };

    preflop_equity_result_t result;
    (void)preflop_equity_calculate(&input, &result);

    /* AA should have ~82% equity vs KK */
    TEST_ASSERT_TRUE(result.equity1 >= 0.80 && result.equity1 <= 0.84);
    TEST_ASSERT_TRUE(result.equity2 >= 0.16 && result.equity2 <= 0.20);
    TEST_ASSERT_TRUE(result.num_matchups > 0);

    printf("PASSED (AA: %.2f%%, KK: %.2f%%)\n", result.equity1 * 100, result.equity2 * 100);
    preflop_equity_result_free(&result);
}

/* Test 2: Pair vs lower pair */
static void test_qq_vs_jj(void)
{
    printf("Test 2: QQ vs JJ... ");
    fflush(stdout);

    preflop_range_t range1, range2;
    preflop_range_parse("QQ", &range1);
    preflop_range_parse("JJ", &range2);

    preflop_equity_input_t input = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = g_lookup_table
    };

    preflop_equity_result_t result;
    (void)preflop_equity_calculate(&input, &result);

    /* QQ should dominate JJ (~80%) */
    TEST_ASSERT_TRUE(result.equity1 >= 0.78 && result.equity1 <= 0.84);
    TEST_ASSERT_TRUE(result.equity2 >= 0.16 && result.equity2 <= 0.22);

    printf("PASSED (QQ: %.2f%%, JJ: %.2f%%)\n", result.equity1 * 100, result.equity2 * 100);
    preflop_equity_result_free(&result);
}

/* Test 3: Premium range vs premium range */
static void test_range_vs_range(void)
{
    printf("Test 3: AA,KK,QQ vs JJ,TT,99... ");
    fflush(stdout);

    preflop_range_t range1, range2;
    preflop_range_parse("AA,KK,QQ", &range1);
    preflop_range_parse("JJ,TT,99", &range2);

    TEST_ASSERT_TRUE(range1.num_hands == 3);
    TEST_ASSERT_TRUE(range2.num_hands == 3);

    preflop_equity_input_t input = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = g_lookup_table
    };

    preflop_equity_result_t result;
    (void)preflop_equity_calculate(&input, &result);

    /* Higher range should dominate (~80%+) */
    TEST_ASSERT_TRUE(result.equity1 >= 0.78);
    TEST_ASSERT_TRUE(result.equity2 <= 0.22);

    printf("PASSED (High: %.2f%%, Low: %.2f%%)\n", result.equity1 * 100, result.equity2 * 100);
    preflop_equity_result_free(&result);
}

/* Test 4: Hand canonicalization */
static void test_hand_canonicalization(void)
{
    printf("Test 4: Hand canonicalization... ");
    fflush(stdout);

    /* Test pairs */
    char str[8];
    preflop_hand_to_string(0, str);
    TEST_ASSERT_TRUE(strcmp(str, "AA") == 0);
    preflop_hand_to_string(1, str);
    TEST_ASSERT_TRUE(strcmp(str, "KK") == 0);
    preflop_hand_to_string(12, str);
    TEST_ASSERT_TRUE(strcmp(str, "22") == 0);

    /* Test suited */
    preflop_hand_to_string(13, str);
    TEST_ASSERT_TRUE(strcmp(str, "AKs") == 0);

    /* Test offsuit */
    preflop_hand_to_string(91, str);
    TEST_ASSERT_TRUE(strcmp(str, "AKo") == 0);

    /* Test string parsing */
    TEST_ASSERT_TRUE(preflop_string_to_hand("AA") == 0);
    TEST_ASSERT_TRUE(preflop_string_to_hand("KK") == 1);
    TEST_ASSERT_TRUE(preflop_string_to_hand("AKs") == 13);
    TEST_ASSERT_TRUE(preflop_string_to_hand("AKo") == 91);

    printf("PASSED\n");
}

/* Test 7: Strength classification */
static void test_strength_classification(void)
{
    printf("Test 7: Strength classification... ");
    fflush(stdout);

    /* Test Premium */
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("AA")) == PREFLOP_STRENGTH_PREMIUM);
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("KK")) == PREFLOP_STRENGTH_PREMIUM);
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("AKs")) == PREFLOP_STRENGTH_PREMIUM);

    /* Test Strong */
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("JJ")) == PREFLOP_STRENGTH_STRONG);
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("AKo")) == PREFLOP_STRENGTH_STRONG);

    /* Test Medium */
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("99")) == PREFLOP_STRENGTH_MEDIUM);
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("AJs")) == PREFLOP_STRENGTH_MEDIUM);

    /* Test Weak */
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("22")) == PREFLOP_STRENGTH_WEAK);
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("54s")) == PREFLOP_STRENGTH_WEAK);

    /* Test Trash */
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("72o")) == PREFLOP_STRENGTH_TRASH);
    TEST_ASSERT_TRUE(preflop_classify(preflop_string_to_hand("T2o")) == PREFLOP_STRENGTH_TRASH);

    printf("PASSED\n");
}

/* Test 5: Range parsing */
static void test_range_parsing(void)
{
    printf("Test 5: Range parsing... ");
    fflush(stdout);

    preflop_range_t range;
    preflop_range_parse("AA,KK,AKs", &range);

    TEST_ASSERT_TRUE(range.num_hands == 3);
    TEST_ASSERT_TRUE(preflop_range_contains(&range, 0));   /* AA */
    TEST_ASSERT_TRUE(preflop_range_contains(&range, 1));   /* KK */
    TEST_ASSERT_TRUE(preflop_range_contains(&range, 13));  /* AKs */
    TEST_ASSERT_TRUE(!preflop_range_contains(&range, 2));  /* QQ not in range */

    /* Test combination counting */
    (void)preflop_range_count_combinations(&range);

    printf("PASSED (3 hands, 16 combos)\n");
}

/* Test 12: Range parsing with plus notation */
static void test_range_parsing_plus(void)
{
    printf("Test 12: Range parsing with plus... ");
    fflush(stdout);

    preflop_range_t range;
    preflop_range_parse("QQ+", &range);

    /* QQ+ should be QQ, KK, AA */
    TEST_ASSERT_TRUE(range.num_hands == 3);
    TEST_ASSERT_TRUE(preflop_range_contains(&range, 0));   /* AA */
    TEST_ASSERT_TRUE(preflop_range_contains(&range, 1));   /* KK */
    TEST_ASSERT_TRUE(preflop_range_contains(&range, 2));   /* QQ */
    TEST_ASSERT_TRUE(!preflop_range_contains(&range, 3));  /* JJ not in range */

    printf("PASSED\n");
}

/* Test 6: Equity symmetry */
static void test_equity_symmetry(void)
{
    printf("Test 6: Equity symmetry... ");
    fflush(stdout);

    preflop_range_t range1, range2;
    preflop_range_parse("AA", &range1);
    preflop_range_parse("KK", &range2);

    /* Calculate AA vs KK */
    preflop_equity_input_t input1 = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = g_lookup_table
    };

    preflop_equity_result_t result1;
    preflop_equity_calculate(&input1, &result1);

    /* Calculate KK vs AA (reversed) */
    preflop_equity_input_t input2 = {
        .range1 = range2,
        .range2 = range1,
        .num_samples = 0,
        .lookup_table = g_lookup_table
    };

    preflop_equity_result_t result2;
    preflop_equity_calculate(&input2, &result2);

    /* Equities should be symmetric */
    TEST_ASSERT_TRUE(fabs(result1.equity1 - result2.equity2) < 0.001);
    TEST_ASSERT_TRUE(fabs(result1.equity2 - result2.equity1) < 0.001);

    preflop_equity_result_free(&result1);
    preflop_equity_result_free(&result2);

    printf("PASSED\n");
}

/* Test 8: Lookup table integration */
static void test_lookup_table(void)
{
    printf("Test 8: Lookup table integration... ");
    fflush(stdout);

    preflop_lookup_table_t *table = g_lookup_table;
    TEST_ASSERT_TRUE(table != NULL);

    /* Check AA vs KK in table */
    double equity_aa_kk = preflop_lookup_table_get(table, 0, 1);
    (void)equity_aa_kk;
    TEST_ASSERT_TRUE(equity_aa_kk >= 0.80 && equity_aa_kk <= 0.84);

    /* Check AKs vs 22 in table (~49%) */
    double equity_aks_22 = preflop_lookup_table_get(table, 13, 12);
    (void)equity_aks_22;
    TEST_ASSERT_TRUE(equity_aks_22 >= 0.45 && equity_aks_22 <= 0.55);

    /* Use table in calculation */
    preflop_range_t r1, r2;
    preflop_range_parse("AA", &r1);
    preflop_range_parse("KK", &r2);

    preflop_equity_input_t input = {
        .range1 = r1,
        .range2 = r2,
        .num_samples = 0, /* Should use table regardless */
        .lookup_table = table
    };

    preflop_equity_result_t result;
    preflop_equity_calculate(&input, &result);

    TEST_ASSERT_TRUE(fabs(result.equity1 - equity_aa_kk) < 0.0001);

    printf("PASSED\n");
}

/* Test 9: AKs vs AKo */
static void test_aks_vs_ako(void)
{
    printf("Test 9: AKs vs AKo... ");
    fflush(stdout);

    preflop_range_t range1, range2;
    preflop_range_parse("AKs", &range1);
    preflop_range_parse("AKo", &range2);

    preflop_equity_input_t input = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = g_lookup_table
    };

    preflop_equity_result_t result;
    preflop_equity_calculate(&input, &result);

    /* AKs vs AKo should be close to 50/50 with slight edge to suited */
    TEST_ASSERT_TRUE(result.equity1 >= 0.52 && result.equity1 <= 0.54);
    TEST_ASSERT_TRUE(result.equity2 >= 0.46 && result.equity2 <= 0.48);

    printf("PASSED (AKs: %.2f%%, AKo: %.2f%%)\n",
           result.equity1 * 100, result.equity2 * 100);
    preflop_equity_result_free(&result);
}

/* Test 10: 22 vs AK (classic race) */
static void test_22_vs_ak(void)
{
    printf("Test 10: 22 vs AK... ");
    fflush(stdout);

    preflop_range_t range1, range2;
    preflop_range_parse("22", &range1);
    preflop_range_parse("AKs", &range2);

    preflop_equity_input_t input = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = g_lookup_table
    };

    preflop_equity_result_t result;
    preflop_equity_calculate(&input, &result);

    /* 22 should be slight favorite (~52%) vs AK */
    TEST_ASSERT_TRUE(result.equity1 >= 0.50 && result.equity1 <= 0.54);
    TEST_ASSERT_TRUE(result.equity2 >= 0.46 && result.equity2 <= 0.50);

    printf("PASSED (22: %.2f%%, AK: %.2f%%)\n",
           result.equity1 * 100, result.equity2 * 100);
    preflop_equity_result_free(&result);
}

/* Test 11: Monte Carlo vs Exhaustive accuracy */
static void test_monte_carlo_accuracy(void)
{
    printf("Test 11: Monte Carlo accuracy... ");
    fflush(stdout);

    preflop_range_t range1, range2;
    preflop_range_parse("AA", &range1);
    preflop_range_parse("KK", &range2);

    /* Exhaustive calculation */
    preflop_equity_input_t input_exhaustive = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = NULL
    };

    preflop_equity_result_t result_exhaustive;
    preflop_equity_calculate(&input_exhaustive, &result_exhaustive);

    /* Monte Carlo with 100k samples */
    preflop_equity_input_t input_mc = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 100000,
        .lookup_table = NULL
    };

    preflop_equity_result_t result_mc;
    preflop_equity_calculate(&input_mc, &result_mc);

    /* Should be within 1% of each other */
    double diff = fabs(result_exhaustive.equity1 - result_mc.equity1);
    TEST_ASSERT_TRUE(diff < 0.01);

    printf("PASSED (Exhaustive: %.2f%%, MC: %.2f%%, Δ=%.2f%%)\n",
           result_exhaustive.equity1 * 100,
           result_mc.equity1 * 100,
           diff * 100);

    preflop_equity_result_free(&result_exhaustive);
    preflop_equity_result_free(&result_mc);
}

/* Test 13: Modern API Hold'em preflop */
static void test_modern_holdem_preflop(void)
{
    printf("Test 13: Modern API Hold'em preflop... ");
    fflush(stdout);

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_range_t *r1 = NULL;
    pe_range_t *r2 = NULL;
    pe_status_t status = pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    TEST_ASSERT_TRUE(status == PE_STATUS_OK && r1 != NULL);
    status = pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
    TEST_ASSERT_TRUE(status == PE_STATUS_OK && r2 != NULL);

    const pe_range_t *ranges[] = {r1, r2};
    pe_equity_result_multi_t result;
    memset(&result, 0, sizeof(result));

    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 1;
    opts.iterations = 30000;

    status = pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
    TEST_ASSERT_TRUE(status == PE_STATUS_OK);

    TEST_ASSERT_TRUE(result.results[0].equity > 0.78 && result.results[0].equity < 0.86);
    TEST_ASSERT_TRUE(result.results[1].equity > 0.14 && result.results[1].equity < 0.22);
    TEST_ASSERT_TRUE(result.samples > 0);

    printf("PASSED (AA: %.2f%%, KK: %.2f%%)\n",
           result.results[0].equity * 100.0,
           result.results[1].equity * 100.0);

    pe_range_free(r1);
    pe_range_free(r2);
}

/* Test 14: Modern API Omaha preflop */
static void test_modern_omaha_preflop(void)
{
    printf("Test 14: Modern API Omaha preflop... ");
    fflush(stdout);

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_range_t *r1 = range_from_cards(game_omaha, "Ah Ad 2c 3c");
    pe_range_t *r2 = range_from_cards(game_omaha, "Kh Kd 2s 3s");
    TEST_ASSERT_TRUE(r1 != NULL && r2 != NULL);

    const pe_range_t *ranges[] = {r1, r2};
    pe_equity_result_multi_t result;
    memset(&result, 0, sizeof(result));

    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 1;
    opts.iterations = 15000;

    pe_status_t status = pe_equity_preflop(game_omaha, ranges, 2, &opts, &result);
    TEST_ASSERT_TRUE(status == PE_STATUS_OK);

    TEST_ASSERT_TRUE(result.results[0].equity > 0.55 && result.results[0].equity < 0.85);
    TEST_ASSERT_TRUE(result.results[1].equity > 0.15 && result.results[1].equity < 0.45);
    TEST_ASSERT_TRUE(result.samples > 0);

    printf("PASSED (AA23: %.2f%%, KK23: %.2f%%)\n",
           result.results[0].equity * 100.0,
           result.results[1].equity * 100.0);

    pe_range_free(r1);
    pe_range_free(r2);
}

/* Test 15: Modern API Short Deck preflop */
static void test_modern_shortdeck_preflop(void)
{
    printf("Test 15: Modern API Short Deck preflop... ");
    fflush(stdout);

    pe_range_t *r1 = range_from_cards(game_sdholdem, "As Ah");
    pe_range_t *r2 = range_from_cards(game_sdholdem, "Ks Kh");
    TEST_ASSERT_TRUE(r1 != NULL && r2 != NULL);

    const pe_range_t *ranges[] = {r1, r2};
    pe_equity_result_multi_t result;
    memset(&result, 0, sizeof(result));

    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 1;
    opts.iterations = 20000;

    pe_status_t status = pe_equity_preflop(game_sdholdem, ranges, 2, &opts, &result);
    TEST_ASSERT_TRUE(status == PE_STATUS_OK);

    TEST_ASSERT_TRUE(result.results[0].equity > 0.52 && result.results[0].equity < 0.80);
    TEST_ASSERT_TRUE(result.results[1].equity < 0.48);
    TEST_ASSERT_TRUE(result.samples > 0);

    printf("PASSED (AA: %.2f%%, KK: %.2f%%)\n",
           result.results[0].equity * 100.0,
           result.results[1].equity * 100.0);

    pe_range_free(r1);
    pe_range_free(r2);
}

/* Main test runner */
int main(void)
{
    printf("=== Pre-flop Equity Unit Tests ===\n\n");

    UNITY_BEGIN();

    init_lookup_table();

    RUN_TEST(test_hand_canonicalization);
    RUN_TEST(test_range_parsing);
    RUN_TEST(test_aa_vs_kk);
    RUN_TEST(test_qq_vs_jj);
    RUN_TEST(test_range_vs_range);
    RUN_TEST(test_equity_symmetry);
    RUN_TEST(test_strength_classification);
    RUN_TEST(test_lookup_table);
    RUN_TEST(test_aks_vs_ako);
    RUN_TEST(test_22_vs_ak);
    RUN_TEST(test_monte_carlo_accuracy);
    RUN_TEST(test_range_parsing_plus);
    RUN_TEST(test_modern_holdem_preflop);
    RUN_TEST(test_modern_omaha_preflop);
    RUN_TEST(test_modern_shortdeck_preflop);

    {
        int failures = UNITY_END();
        cleanup_lookup_table();
        if (failures == 0) {
            printf("\nAll pre-flop equity tests PASSED (15/15)\n");
        }
        return failures;
    }
}
