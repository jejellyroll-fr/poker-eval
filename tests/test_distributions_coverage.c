/*
 * test_distributions_coverage.c -- tests for hand distribution generation
 *
 * Validates:
 * - HoldemAgnosticHand.c: Holdem range parsing and instantiation
 * - OmahaDistributions.c: Omaha hand pattern parsing and instantiation
 * - StudDistributions.c: Stud hand pattern parsing and instantiation
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/distributions/HoldemAgnosticHand.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/distributions/stud_distributions.h>

/* Helper to count cards in a mask */
static int count_cards(StdDeck_CardMask mask)
{
    int count = 0;
    for (int i = 0; i < 52; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            count++;
        }
    }
    return count;
}

void setUp(void) {}
void tearDown(void) {}

/* ===== HOLDEM AGNOSTIC HAND TESTS ===== */

/*
 * Test parsing pocket pair: "AA"
 */
void test_holdem_parse_pair(void)
{
    int result = HoldemAgnosticHand_Parse("AA", "");
    TEST_ASSERT_EQUAL_INT(1, result); /* Parse should succeed */

    /* Verify it's recognized as a pair */
    TEST_ASSERT_TRUE(HoldemAgnosticHand_IsPair("AA"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsSuited("AA"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsOffSuit("AA"));
}

/*
 * Test instantiating pocket pair: "AA" produces 6 combos
 */
void test_holdem_instantiate_pair(void)
{
    HandList handList;
    handList.count = 0;

    int combos = HoldemAgnosticHand_Instantiate("AA", "", &handList);

    TEST_ASSERT_EQUAL_INT(6, combos); /* C(4,2) = 6 */
    TEST_ASSERT_EQUAL_INT(6, handList.count);

    /* Verify each hand has exactly 2 cards */
    for (int i = 0; i < handList.count; i++) {
        TEST_ASSERT_EQUAL_INT(2, count_cards(handList.hands[i]));
    }
}

/*
 * Test parsing suited hands: "AKs"
 */
void test_holdem_parse_suited(void)
{
    int result = HoldemAgnosticHand_Parse("AKs", "");
    TEST_ASSERT_EQUAL_INT(1, result);

    TEST_ASSERT_TRUE(HoldemAgnosticHand_IsSuited("AKs"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsPair("AKs"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsOffSuit("AKs"));
}

/*
 * Test instantiating suited hands: "AKs" produces 4 combos
 */
void test_holdem_instantiate_suited(void)
{
    HandList handList;
    handList.count = 0;

    int combos = HoldemAgnosticHand_Instantiate("AKs", "", &handList);

    TEST_ASSERT_EQUAL_INT(4, combos); /* 4 suits */
    TEST_ASSERT_EQUAL_INT(4, handList.count);
}

/*
 * Test parsing offsuit hands: "AKo"
 */
void test_holdem_parse_offsuit(void)
{
    int result = HoldemAgnosticHand_Parse("AKo", "");
    TEST_ASSERT_EQUAL_INT(1, result);

    TEST_ASSERT_TRUE(HoldemAgnosticHand_IsOffSuit("AKo"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsPair("AKo"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsSuited("AKo"));
}

/*
 * Test instantiating offsuit hands: "AKo" produces 12 combos
 */
void test_holdem_instantiate_offsuit(void)
{
    HandList handList;
    handList.count = 0;

    int combos = HoldemAgnosticHand_Instantiate("AKo", "", &handList);

    TEST_ASSERT_EQUAL_INT(12, combos); /* 4 * 3 = 12 */
    TEST_ASSERT_EQUAL_INT(12, handList.count);
}

/*
 * Test parsing any hands (suited or offsuit): "AK"
 */
void test_holdem_parse_any(void)
{
    int result = HoldemAgnosticHand_Parse("AK", "");
    TEST_ASSERT_EQUAL_INT(1, result);

    TEST_ASSERT_TRUE(HoldemAgnosticHand_IsInclusive("AK"));
}

/*
 * Test instantiating any hands: "AK" produces 16 combos
 */
void test_holdem_instantiate_any(void)
{
    HandList handList;
    handList.count = 0;

    int combos = HoldemAgnosticHand_Instantiate("AK", "", &handList);

    TEST_ASSERT_EQUAL_INT(16, combos); /* 4 suited + 12 offsuit */
    TEST_ASSERT_EQUAL_INT(16, handList.count);
}

/*
 * Test instantiating with dead cards removes blocked combos
 */
void test_holdem_instantiate_with_dead(void)
{
    HandList handList;
    handList.count = 0;

    /* Dead: Ac (blocks 3 AA combos) */
    int combos = HoldemAgnosticHand_Instantiate("AA", "Ac", &handList);

    TEST_ASSERT_EQUAL_INT(3, combos); /* 6 - 3 = 3 */
    TEST_ASSERT_EQUAL_INT(3, handList.count);
}

void test_holdem_rejects_invalid_dead_text(void)
{
    HandList handList;
    char too_long[32];

    handList.count = 0;
    memset(too_long, 'A', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';

    TEST_ASSERT_EQUAL_INT(0, HoldemAgnosticHand_Parse("AA", "AsK"));
    TEST_ASSERT_EQUAL_INT(0, HoldemAgnosticHand_Parse("AA", "Az"));
    TEST_ASSERT_EQUAL_INT(0, HoldemAgnosticHand_Parse("AA", too_long));
    TEST_ASSERT_EQUAL_INT(0, HoldemAgnosticHand_Instantiate("AA", "AsK", &handList));
}

/*
 * Test instantiating random: "XxXx" (all 2-card combos)
 */
void test_holdem_instantiate_random(void)
{
    HandList handList;
    handList.count = 0;

    int combos = HoldemAgnosticHand_Instantiate("XxXx", "", &handList);

    /* C(52,2) = 1326 */
    TEST_ASSERT_EQUAL_INT(1326, combos);
    TEST_ASSERT_EQUAL_INT(1326, handList.count);
}

/*
 * Test parsing plus notation: "TT+"
 */
void test_holdem_parse_plus_notation(void)
{
    int result = HoldemAgnosticHand_Parse("TT+", "");
    TEST_ASSERT_EQUAL_INT(1, result);
}

/*
 * Test instantiating plus notation: "TT+" (TT-AA = 5 pairs)
 */
void test_holdem_instantiate_plus_notation(void)
{
    HandList handList;
    handList.count = 0;

    int combos = HoldemAgnosticHand_Instantiate("TT+", "", &handList);

    /* TT, JJ, QQ, KK, AA = 5 pairs * 6 combos each = 30 */
    TEST_ASSERT_EQUAL_INT(30, combos);
    TEST_ASSERT_EQUAL_INT(30, handList.count);
}

/*
 * Test parsing range notation: "22-55"
 */
void test_holdem_parse_range_notation(void)
{
    int result = HoldemAgnosticHand_Parse("22-55", "");
    TEST_ASSERT_EQUAL_INT(1, result);
}

/*
 * Test instantiating range notation: "22-55" (22,33,44,55 = 4 pairs)
 */
void test_holdem_instantiate_range_notation(void)
{
    HandList handList;
    handList.count = 0;

    int combos = HoldemAgnosticHand_Instantiate("22-55", "", &handList);

    /* 22, 33, 44, 55 = 4 pairs * 6 combos each = 24 */
    TEST_ASSERT_EQUAL_INT(24, combos);
    TEST_ASSERT_EQUAL_INT(24, handList.count);
}

/*
 * Test specific hand recognition: "AhKs"
 */
void test_holdem_is_specific_hand(void)
{
    TEST_ASSERT_TRUE(HoldemAgnosticHand_IsSpecificHand("AhKs"));
    TEST_ASSERT_TRUE(HoldemAgnosticHand_IsSpecificHand("2c3d"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsSpecificHand("AA"));
    TEST_ASSERT_FALSE(HoldemAgnosticHand_IsSpecificHand("AKs"));
}

/* ===== OMAHA DISTRIBUTION TESTS ===== */

/*
 * Test parsing AAxx pattern
 */
void test_omaha_parse_aaxx(void)
{
    OmahaHandQuery query;
    int result = OmahaHand_Parse("AAxx", &query);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT(1, query.num_required_ranks); /* Only A */
    TEST_ASSERT_EQUAL_INT(2, query.num_rank_wildcards); /* Two x's */
}

/*
 * Test parsing double-suited: "AAds"
 */
void test_omaha_parse_double_suited(void)
{
    OmahaHandQuery query;
    int result = OmahaHand_Parse("AAxxds", &query);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT(OMAHA_SUIT_PROPERTY_DS, query.suit_property);
}

/*
 * Test parsing rainbow: "AKQJr"
 */
void test_omaha_parse_rainbow(void)
{
    OmahaHandQuery query;
    int result = OmahaHand_Parse("AKQJr", &query);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT(OMAHA_SUIT_PROPERTY_RAINBOW, query.suit_property);
    TEST_ASSERT_EQUAL_INT(4, query.num_required_ranks);
}

/*
 * Test instantiating AAxx produces valid hands
 */
void test_omaha_instantiate_aaxx(void)
{
    OmahaHandQuery query;
    OmahaHandList handList;
    StdDeck_CardMask dead;

    StdDeck_CardMask_RESET(dead);

    int parse_result = OmahaHand_Parse("AAxx", &query);
    TEST_ASSERT_EQUAL_INT(1, parse_result);

    OmahaHandList_Init(&handList, 1000);

    int count = OmahaHand_Instantiate(&query, dead, &handList);

    TEST_ASSERT_GREATER_THAN(0, count);
    TEST_ASSERT_EQUAL_INT(count, handList.count);

    /* Verify each hand has exactly 4 cards */
    for (int i = 0; i < handList.count && i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(4, count_cards(handList.hands[i]));
    }

    OmahaHandList_Free(&handList);
}

/*
 * Test instantiating AKQJ (all 4 specific ranks)
 */
void test_omaha_instantiate_akqj(void)
{
    OmahaHandQuery query;
    OmahaHandList handList;
    StdDeck_CardMask dead;

    StdDeck_CardMask_RESET(dead);

    int parse_result = OmahaHand_Parse("AKQJ", &query);
    TEST_ASSERT_EQUAL_INT(1, parse_result);

    OmahaHandList_Init(&handList, 500);

    int count = OmahaHand_Instantiate(&query, dead, &handList);

    /* 4^4 = 256 combinations of suits */
    TEST_ASSERT_EQUAL_INT(256, count);

    OmahaHandList_Free(&handList);
}

/*
 * Test instantiating AKQJ rainbow (only 4! = 24 combos)
 */
void test_omaha_instantiate_akqj_rainbow(void)
{
    OmahaHandQuery query;
    OmahaHandList handList;
    StdDeck_CardMask dead;

    StdDeck_CardMask_RESET(dead);

    int parse_result = OmahaHand_Parse("AKQJr", &query);
    TEST_ASSERT_EQUAL_INT(1, parse_result);

    OmahaHandList_Init(&handList, 100);

    int count = OmahaHand_Instantiate(&query, dead, &handList);

    /* Rainbow: 4! = 24 */
    TEST_ASSERT_EQUAL_INT(24, count);

    OmahaHandList_Free(&handList);
}

/*
 * Test OmahaHandList memory management
 */
void test_omaha_handlist_memory(void)
{
    OmahaHandList handList;

    /* Init */
    int init_result = OmahaHandList_Init(&handList, 10);
    TEST_ASSERT_EQUAL_INT(1, init_result);
    TEST_ASSERT_EQUAL_INT(0, handList.count);
    TEST_ASSERT_EQUAL_INT(10, handList.capacity);

    /* Add hands */
    StdDeck_CardMask dummy;
    StdDeck_CardMask_RESET(dummy);
    for (int i = 0; i < 15; i++) {
        int add_result = OmahaHandList_AddHand(&handList, dummy);
        TEST_ASSERT_EQUAL_INT(1, add_result);
    }

    TEST_ASSERT_EQUAL_INT(15, handList.count);
    TEST_ASSERT_GREATER_OR_EQUAL(15, handList.capacity);

    /* Clear */
    OmahaHandList_Clear(&handList);
    TEST_ASSERT_EQUAL_INT(0, handList.count);

    /* Free */
    OmahaHandList_Free(&handList);
    TEST_ASSERT_NULL(handList.hands);
}

/* ===== STUD DISTRIBUTION TESTS ===== */

/*
 * Test parsing pair pattern: "(AA)xxxxx"
 */
void test_stud_parse_pair(void)
{
    StudHandQuery query;
    int result = StudHand_Parse("(AA)xxxxx", 7, &query);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT(7, query.game_total_cards);
    TEST_ASSERT_EQUAL_INT(2, query.num_known_cards); /* Two aces */
}

/*
 * Test parsing trips pattern: "(AAA)xxxx"
 */
void test_stud_parse_trips(void)
{
    StudHandQuery query;
    int result = StudHand_Parse("(AAA)xxxx", 7, &query);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT(3, query.num_known_cards); /* Three aces */
}

/*
 * Test instantiating pair pattern
 */
void test_stud_instantiate_pair(void)
{
    StudHandQuery query;
    StudHandList handList;
    StdDeck_CardMask dead;

    StdDeck_CardMask_RESET(dead);
    handList.count = 0;

    int parse_result = StudHand_Parse("(AA)xxxxx", 7, &query);
    TEST_ASSERT_EQUAL_INT(1, parse_result);

    int count = StudHand_Instantiate(&query, dead, &handList);

    /* Should produce some combos */
    TEST_ASSERT_GREATER_THAN(0, count);

    /* Verify each hand has exactly 7 cards */
    for (int i = 0; i < handList.count && i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(7, count_cards(handList.hands[i]));
    }
}

/*
 * Test instantiating trips pattern
 */
void test_stud_instantiate_trips(void)
{
    StudHandQuery query;
    StudHandList handList;
    StdDeck_CardMask dead;

    StdDeck_CardMask_RESET(dead);
    handList.count = 0;

    int parse_result = StudHand_Parse("(AAA)xxxx", 7, &query);
    TEST_ASSERT_EQUAL_INT(1, parse_result);

    int count = StudHand_Instantiate(&query, dead, &handList);

    /* Should produce some combos */
    TEST_ASSERT_GREATER_THAN(0, count);

    /* Verify each hand has exactly 7 cards */
    for (int i = 0; i < handList.count && i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(7, count_cards(handList.hands[i]));
    }
}

/*
 * Test parsing specific card: "(AsAh)xxxxx"
 */
void test_stud_parse_specific(void)
{
    StudHandQuery query;
    int result = StudHand_Parse("(AsAh)xxxxx", 7, &query);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT(2, query.num_known_cards);
}

/*
 * Test 5-card stud pattern
 */
void test_stud_5card(void)
{
    StudHandQuery query;
    int result = StudHand_Parse("(AA)xxx", 5, &query);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT(5, query.game_total_cards);
}

int main(void)
{
    UNITY_BEGIN();

    /* Holdem Agnostic Hand tests */
    RUN_TEST(test_holdem_parse_pair);
    RUN_TEST(test_holdem_instantiate_pair);
    RUN_TEST(test_holdem_parse_suited);
    RUN_TEST(test_holdem_instantiate_suited);
    RUN_TEST(test_holdem_parse_offsuit);
    RUN_TEST(test_holdem_instantiate_offsuit);
    RUN_TEST(test_holdem_parse_any);
    RUN_TEST(test_holdem_instantiate_any);
    RUN_TEST(test_holdem_instantiate_with_dead);
    RUN_TEST(test_holdem_rejects_invalid_dead_text);
    RUN_TEST(test_holdem_instantiate_random);
    RUN_TEST(test_holdem_parse_plus_notation);
    RUN_TEST(test_holdem_instantiate_plus_notation);
    RUN_TEST(test_holdem_parse_range_notation);
    RUN_TEST(test_holdem_instantiate_range_notation);
    RUN_TEST(test_holdem_is_specific_hand);

    /* Omaha Distribution tests */
    RUN_TEST(test_omaha_parse_aaxx);
    RUN_TEST(test_omaha_parse_double_suited);
    RUN_TEST(test_omaha_parse_rainbow);
    RUN_TEST(test_omaha_instantiate_aaxx);
    RUN_TEST(test_omaha_instantiate_akqj);
    RUN_TEST(test_omaha_instantiate_akqj_rainbow);
    RUN_TEST(test_omaha_handlist_memory);

    /* Stud Distribution tests */
    RUN_TEST(test_stud_parse_pair);
    RUN_TEST(test_stud_parse_trips);
    RUN_TEST(test_stud_instantiate_pair);
    RUN_TEST(test_stud_instantiate_trips);
    RUN_TEST(test_stud_parse_specific);
    RUN_TEST(test_stud_5card);

    return UNITY_END();
}
