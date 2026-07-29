#include "unity.h"
#include <poker_eval/core/card.h>

void setUp(void) {}
void tearDown(void) {}

static void test_char_to_rank(void) {
    TEST_ASSERT_EQUAL_INT(12, CharToRank('A'));
    TEST_ASSERT_EQUAL_INT(11, CharToRank('K'));
    TEST_ASSERT_EQUAL_INT(10, CharToRank('Q'));
    TEST_ASSERT_EQUAL_INT(9, CharToRank('J'));
    TEST_ASSERT_EQUAL_INT(8, CharToRank('T'));
    TEST_ASSERT_EQUAL_INT(0, CharToRank('2'));
    TEST_ASSERT_EQUAL_INT(-1, CharToRank('1')); // Invalid
}

static void test_char_to_suit(void) {
    TEST_ASSERT_EQUAL_INT(3, CharToSuit('S'));
    TEST_ASSERT_EQUAL_INT(2, CharToSuit('C'));
    TEST_ASSERT_EQUAL_INT(1, CharToSuit('D'));
    TEST_ASSERT_EQUAL_INT(0, CharToSuit('H'));
    TEST_ASSERT_EQUAL_INT(-1, CharToSuit('X')); // Invalid
}

static void test_rank_to_char(void) {
    TEST_ASSERT_EQUAL_CHAR('A', RankToChar(12));
    TEST_ASSERT_EQUAL_CHAR('K', RankToChar(11));
    TEST_ASSERT_EQUAL_CHAR('Q', RankToChar(10));
    TEST_ASSERT_EQUAL_CHAR('J', RankToChar(9));
    TEST_ASSERT_EQUAL_CHAR('T', RankToChar(8));
    TEST_ASSERT_EQUAL_CHAR('2', RankToChar(0));
}

static void test_suit_to_char(void) {
    TEST_ASSERT_EQUAL_CHAR('s', SuitToChar(3));
    TEST_ASSERT_EQUAL_CHAR('c', SuitToChar(2));
    TEST_ASSERT_EQUAL_CHAR('d', SuitToChar(1));
    TEST_ASSERT_EQUAL_CHAR('h', SuitToChar(0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_char_to_rank);
    RUN_TEST(test_char_to_suit);
    RUN_TEST(test_rank_to_char);
    RUN_TEST(test_suit_to_char);
    return UNITY_END();
}
