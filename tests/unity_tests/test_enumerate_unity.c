#include "unity.h"
#include <poker_eval/core/enumerate.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/poker_defs.h>

void setUp(void) {}
void tearDown(void) {}

// Test basic enumeration functionality
static void test_enumerate_1_card(void)
{
    int count = 0;
    StdDeck_CardMask cards;

    DECK_ENUMERATE_1_CARDS(StdDeck, cards, {
        count++;
        (void)cards; // Suppress unused variable warning
    });

    TEST_ASSERT_EQUAL_INT(52, count);
}

static void test_enumerate_2_cards(void)
{
    int count = 0;
    StdDeck_CardMask cards;

    DECK_ENUMERATE_2_CARDS(StdDeck, cards, {
        count++;
        (void)cards; // Suppress unused variable warning
    });

    // C(52,2) = 52*51/2 = 1326
    TEST_ASSERT_EQUAL_INT(1326, count);
}

static void test_enumerate_3_cards(void)
{
    int count = 0;
    StdDeck_CardMask cards;

    DECK_ENUMERATE_3_CARDS(StdDeck, cards, {
        count++;
        (void)cards; // Suppress unused variable warning
    });

    // C(52,3) = 52*51*50/(3*2*1) = 22100
    TEST_ASSERT_EQUAL_INT(22100, count);
}

static void test_enumerate_with_dead_cards(void)
{
    int count = 0;
    StdDeck_CardMask cards, dead_cards;

    // Set up dead cards (Ace of Spades and King of Hearts)
    StdDeck_CardMask_RESET(dead_cards);
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    DECK_ENUMERATE_1_CARDS_D(StdDeck, cards, dead_cards, {
        count++;
        (void)cards; // Suppress unused variable warning
    });

    // Should be 52 - 2 = 50 cards
    TEST_ASSERT_EQUAL_INT(50, count);
}

static void test_enumerate_2_cards_with_dead(void)
{
    int count = 0;
    StdDeck_CardMask cards, dead_cards;

    // Set up dead cards (Ace of Spades)
    StdDeck_CardMask_RESET(dead_cards);
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

    DECK_ENUMERATE_2_CARDS_D(StdDeck, cards, dead_cards, {
        count++;
        (void)cards; // Suppress unused variable warning
    });

    // C(51,2) = 51*50/2 = 1275
    TEST_ASSERT_EQUAL_INT(1275, count);
}

static void test_montecarlo_enumeration(void)
{
    int count = 0;
    StdDeck_CardMask cards, dead_cards;

    StdDeck_CardMask_RESET(dead_cards);

    DECK_MONTECARLO_N_CARDS_D(StdDeck, cards, dead_cards, 2, 100, {
        count++;
        // Verify we got exactly 2 cards
        TEST_ASSERT_EQUAL_INT(2, StdDeck_numCards(cards));
    });

    TEST_ASSERT_EQUAL_INT(100, count);
}

static void test_enumerate_n_cards(void)
{
    int count = 0;
    StdDeck_CardMask cards;

    // Test with 4 cards
    DECK_ENUMERATE_N_CARDS(StdDeck, cards, 4, {
        count++;
        TEST_ASSERT_EQUAL_INT(4, StdDeck_numCards(cards));
    });

    // C(52,4) = 270725
    TEST_ASSERT_EQUAL_INT(270725, count);
}

static void test_enumerate_n_cards_with_dead(void)
{
    int count = 0;
    StdDeck_CardMask cards, dead_cards;

    // Set up dead cards (2 cards)
    StdDeck_CardMask_RESET(dead_cards);
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    // Test with 3 cards from remaining 50
    DECK_ENUMERATE_N_CARDS_D(StdDeck, cards, 3, dead_cards, {
        count++;
        TEST_ASSERT_EQUAL_INT(3, StdDeck_numCards(cards));
        // Verify no dead cards are included
        TEST_ASSERT_FALSE(StdDeck_CardMask_ANY_SET(cards, dead_cards));
    });

    // C(50,3) = 19600
    TEST_ASSERT_EQUAL_INT(19600, count);
}

static void test_enumerate_card_validation(void)
{
    int count = 0;
    StdDeck_CardMask cards;

    DECK_ENUMERATE_2_CARDS(StdDeck, cards, {
        count++;
        // Verify exactly 2 cards
        TEST_ASSERT_EQUAL_INT(2, StdDeck_numCards(cards));

        // Verify cards are different (no duplicates)
        int card_count[52] = {0};
        int i;
        for (i = 0; i < 52; i++)
        {
            if (StdDeck_CardMask_CARD_IS_SET(cards, i))
            {
                card_count[i]++;
            }
        }

        int total_cards = 0;
        for (i = 0; i < 52; i++)
        {
            total_cards += card_count[i];
            TEST_ASSERT_TRUE(card_count[i] <= 1); // No duplicates
        }
        TEST_ASSERT_EQUAL_INT(2, total_cards);

        // Only check first few iterations to avoid slowing down test
        if (count >= 10)
            return;
    });
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enumerate_1_card);
    RUN_TEST(test_enumerate_2_cards);
    RUN_TEST(test_enumerate_3_cards);
    RUN_TEST(test_enumerate_with_dead_cards);
    RUN_TEST(test_enumerate_2_cards_with_dead);
    RUN_TEST(test_montecarlo_enumeration);
    RUN_TEST(test_enumerate_n_cards);
    RUN_TEST(test_enumerate_n_cards_with_dead);
    RUN_TEST(test_enumerate_card_validation);
    return UNITY_END();
}
