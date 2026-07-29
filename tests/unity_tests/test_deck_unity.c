#include "unity.h"
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/poker_defs.h>

void setUp(void) {}
void tearDown(void) {}

// Test basic deck operations
static void test_deck_constants(void)
{
    TEST_ASSERT_EQUAL_INT(52, StdDeck_N_CARDS);
    TEST_ASSERT_EQUAL_INT(4, StdDeck_Suit_COUNT);
    TEST_ASSERT_EQUAL_INT(13, StdDeck_Rank_COUNT);
}

static void test_card_creation(void)
{
    int card;

    // Test Ace of Spades
    card = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_ACE, StdDeck_RANK(card));
    TEST_ASSERT_EQUAL_INT(StdDeck_Suit_SPADES, StdDeck_SUIT(card));

    // Test King of Hearts
    card = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_KING, StdDeck_RANK(card));
    TEST_ASSERT_EQUAL_INT(StdDeck_Suit_HEARTS, StdDeck_SUIT(card));

    // Test Two of Clubs
    card = StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_2, StdDeck_RANK(card));
    TEST_ASSERT_EQUAL_INT(StdDeck_Suit_CLUBS, StdDeck_SUIT(card));
}

static void test_cardmask_operations(void)
{
    StdDeck_CardMask mask1;
    int card1, card2;

    // Create cards
    card1 = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    card2 = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    // Test RESET
    StdDeck_CardMask_RESET(mask1);
    TEST_ASSERT_EQUAL_INT(0, StdDeck_numCards(mask1));

    // Test SET
    StdDeck_CardMask_SET(mask1, card1);
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(mask1, card1));
    TEST_ASSERT_EQUAL_INT(1, StdDeck_numCards(mask1));

    // Test setting another card
    StdDeck_CardMask_SET(mask1, card2);
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(mask1, card1));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(mask1, card2));
    TEST_ASSERT_EQUAL_INT(2, StdDeck_numCards(mask1));

    // Test UNSET
    StdDeck_CardMask_UNSET(mask1, card1);
    TEST_ASSERT_FALSE(StdDeck_CardMask_CARD_IS_SET(mask1, card1));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(mask1, card2));
    TEST_ASSERT_EQUAL_INT(1, StdDeck_numCards(mask1));
}

static void test_cardmask_logical_operations(void)
{
    StdDeck_CardMask mask1, mask2, result;
    int card1, card2, card3;

    card1 = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    card2 = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    card3 = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    // Setup masks
    StdDeck_CardMask_RESET(mask1);
    StdDeck_CardMask_RESET(mask2);

    StdDeck_CardMask_SET(mask1, card1);
    StdDeck_CardMask_SET(mask1, card2);

    StdDeck_CardMask_SET(mask2, card2);
    StdDeck_CardMask_SET(mask2, card3);

    // Test OR
    StdDeck_CardMask_OR(result, mask1, mask2);
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(result, card1));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(result, card2));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(result, card3));
    TEST_ASSERT_EQUAL_INT(3, StdDeck_numCards(result));

    // Test AND
    StdDeck_CardMask_AND(result, mask1, mask2);
    TEST_ASSERT_FALSE(StdDeck_CardMask_CARD_IS_SET(result, card1));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(result, card2));
    TEST_ASSERT_FALSE(StdDeck_CardMask_CARD_IS_SET(result, card3));
    TEST_ASSERT_EQUAL_INT(1, StdDeck_numCards(result));

    // Test XOR
    StdDeck_CardMask_XOR(result, mask1, mask2);
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(result, card1));
    TEST_ASSERT_FALSE(StdDeck_CardMask_CARD_IS_SET(result, card2));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(result, card3));
    TEST_ASSERT_EQUAL_INT(2, StdDeck_numCards(result));
}

static void test_cardmask_any_set(void)
{
    StdDeck_CardMask mask1, mask2;
    int card1, card2, card3;

    card1 = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    card2 = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    card3 = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    // Setup masks
    StdDeck_CardMask_RESET(mask1);
    StdDeck_CardMask_RESET(mask2);

    StdDeck_CardMask_SET(mask1, card1);
    StdDeck_CardMask_SET(mask1, card2);

    StdDeck_CardMask_SET(mask2, card2);
    StdDeck_CardMask_SET(mask2, card3);

    // Test ANY_SET - should be true because card2 is in both
    TEST_ASSERT_TRUE(StdDeck_CardMask_ANY_SET(mask1, mask2));

    // Test with no overlap
    StdDeck_CardMask_RESET(mask2);
    StdDeck_CardMask_SET(mask2, card3);
    TEST_ASSERT_FALSE(StdDeck_CardMask_ANY_SET(mask1, mask2));
}

static void test_cardmask_equal(void)
{
    StdDeck_CardMask mask1, mask2;
    int card1, card2;

    card1 = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    card2 = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    // Test equal empty masks
    StdDeck_CardMask_RESET(mask1);
    StdDeck_CardMask_RESET(mask2);
    TEST_ASSERT_TRUE(StdDeck_CardMask_EQUAL(mask1, mask2));

    // Test equal non-empty masks
    StdDeck_CardMask_SET(mask1, card1);
    StdDeck_CardMask_SET(mask2, card1);
    TEST_ASSERT_TRUE(StdDeck_CardMask_EQUAL(mask1, mask2));

    // Test unequal masks
    StdDeck_CardMask_SET(mask1, card2);
    TEST_ASSERT_FALSE(StdDeck_CardMask_EQUAL(mask1, mask2));
}

static void test_card_string_conversion(void)
{
    int card;
    char card_str[16];

    // Test Ace of Spades
    card = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    StdDeck_cardToString(card, card_str);
    TEST_ASSERT_EQUAL_STRING("As", card_str);

    // Test King of Hearts
    card = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    StdDeck_cardToString(card, card_str);
    TEST_ASSERT_EQUAL_STRING("Kh", card_str);

    // Test Two of Clubs
    card = StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    StdDeck_cardToString(card, card_str);
    TEST_ASSERT_EQUAL_STRING("2c", card_str);

    // Test Ten of Diamonds
    card = StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS);
    StdDeck_cardToString(card, card_str);
    TEST_ASSERT_EQUAL_STRING("Td", card_str);
}

static void test_all_cards_unique(void)
{
    int cards[52];
    int i, j;

    // Generate all 52 cards
    for (i = 0; i < 52; i++)
    {
        cards[i] = i;
    }

    // Verify all cards are unique
    for (i = 0; i < 52; i++)
    {
        for (j = i + 1; j < 52; j++)
        {
            TEST_ASSERT_NOT_EQUAL(cards[i], cards[j]);
        }
    }

    // Verify rank and suit extraction works for all cards
    for (i = 0; i < 52; i++)
    {
        int rank = StdDeck_RANK(i);
        int suit = StdDeck_SUIT(i);

        TEST_ASSERT_TRUE(rank >= 0 && rank < 13);
        TEST_ASSERT_TRUE(suit >= 0 && suit < 4);

        // Verify reconstruction
        int reconstructed = StdDeck_MAKE_CARD(rank, suit);
        TEST_ASSERT_EQUAL_INT(i, reconstructed);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deck_constants);
    RUN_TEST(test_card_creation);
    RUN_TEST(test_cardmask_operations);
    RUN_TEST(test_cardmask_logical_operations);
    RUN_TEST(test_cardmask_any_set);
    RUN_TEST(test_cardmask_equal);
    RUN_TEST(test_card_string_conversion);
    RUN_TEST(test_all_cards_unique);
    return UNITY_END();
}
