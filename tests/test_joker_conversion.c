/*
 * test_joker_conversion.c - Test conversions between JokerDeck and StdDeck
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/core/universal_deck.h>

static void test_joker_to_std_conversion(void)
{
    printf("Testing JokerDeck to StdDeck conversion...\n");

    JokerDeck_CardMask joker_mask;
    StdDeck_CardMask std_mask;

    // Create JokerDeck mask with As, Kh, and Joker
    JokerDeck_CardMask_RESET(joker_mask);
    JokerDeck_CardMask_SET(joker_mask, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(joker_mask, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(joker_mask, JokerDeck_JOKER);

    printf("  JokerDeck has %d cards (including joker)\n", JokerDeck_numCards(joker_mask));

    // Convert to StdDeck
    Universal_ConvertJokerToStd(joker_mask, &std_mask);

    // Verify regular cards are preserved
    assert(StdDeck_CardMask_CARD_IS_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES)));
    assert(StdDeck_CardMask_CARD_IS_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS)));

    // StdDeck should have only 2 cards (joker is lost)
    assert(StdDeck_numCards(std_mask) == 2);

    printf("  ✓ Conversion drops joker correctly (3 cards → 2 cards)\n");
}

static void test_bidirectional_conversion(void)
{
    printf("Testing bidirectional conversion...\n");

    StdDeck_CardMask std_original, std_final;
    JokerDeck_CardMask joker_intermediate;

    // Create StdDeck mask with 5 cards
    StdDeck_CardMask_RESET(std_original);
    StdDeck_CardMask_SET(std_original, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(std_original, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(std_original, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(std_original, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(std_original, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    // Convert Std → Joker → Std
    Universal_ConvertStdToJoker(std_original, &joker_intermediate);
    Universal_ConvertJokerToStd(joker_intermediate, &std_final);

    // Verify all cards are preserved
    assert(StdDeck_numCards(std_original) == StdDeck_numCards(std_final));

    // Check each card
    for (int i = 0; i < StdDeck_N_CARDS; i++)
    {
        assert(StdDeck_CardMask_CARD_IS_SET(std_original, i) ==
               StdDeck_CardMask_CARD_IS_SET(std_final, i));
    }

    printf("  ✓ Bidirectional conversion preserves all cards\n");
}

static void test_empty_mask_conversion(void)
{
    printf("Testing empty mask conversion...\n");

    StdDeck_CardMask std_empty;
    JokerDeck_CardMask joker_empty;

    // Test empty Std → Joker
    StdDeck_CardMask_RESET(std_empty);
    Universal_ConvertStdToJoker(std_empty, &joker_empty);
    assert(JokerDeck_numCards(joker_empty) == 0);

    // Test empty Joker → Std
    JokerDeck_CardMask_RESET(joker_empty);
    Universal_ConvertJokerToStd(joker_empty, &std_empty);
    assert(StdDeck_numCards(std_empty) == 0);

    printf("  ✓ Empty mask conversions work correctly\n");
}

int main(void)
{
    printf("=== JokerDeck Conversion Tests ===\n\n");

    test_joker_to_std_conversion();
    test_bidirectional_conversion();
    test_empty_mask_conversion();

    printf("\n✅ All conversion tests passed!\n");

    return 0;
}
