/*
 * test_joker_mask_ops.c - Test JokerDeck mask operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_joker.h>

static void test_joker_mask_or(void) {
    printf("Testing JokerDeck_CardMask_OR with joker...\n");
    
    JokerDeck_CardMask mask1, mask2, result; (void)result;
    
    // Mask1: Joker + As
    JokerDeck_CardMask_RESET(mask1);
    JokerDeck_CardMask_SET(mask1, JokerDeck_JOKER);
    JokerDeck_CardMask_SET(mask1, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES));
    
    // Mask2: Kh + Qd
    JokerDeck_CardMask_RESET(mask2);
    JokerDeck_CardMask_SET(mask2, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(mask2, JokerDeck_MAKE_CARD(JokerDeck_Rank_QUEEN, JokerDeck_Suit_DIAMONDS));
    
    // OR operation
    JokerDeck_CardMask_OR(result, mask1, mask2);
    
    // Verify all cards are in result
    assert(JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_JOKER));
    assert(JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES)));
    assert(JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS)));
    assert(JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_MAKE_CARD(JokerDeck_Rank_QUEEN, JokerDeck_Suit_DIAMONDS)));
    
    // Count should be 4
    assert(JokerDeck_numCards(result) == 4);
    
    printf("  ✓ JokerDeck_CardMask_OR works correctly with joker\n");
}

static void test_joker_mask_and(void) {
    printf("Testing JokerDeck_CardMask_AND with joker...\n");
    
    JokerDeck_CardMask mask1, mask2, result; (void)result;
    
    // Mask1: Joker + As + Kh
    JokerDeck_CardMask_RESET(mask1);
    JokerDeck_CardMask_SET(mask1, JokerDeck_JOKER);
    JokerDeck_CardMask_SET(mask1, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(mask1, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS));
    
    // Mask2: Joker + Kh + Qd
    JokerDeck_CardMask_RESET(mask2);
    JokerDeck_CardMask_SET(mask2, JokerDeck_JOKER);
    JokerDeck_CardMask_SET(mask2, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(mask2, JokerDeck_MAKE_CARD(JokerDeck_Rank_QUEEN, JokerDeck_Suit_DIAMONDS));
    
    // AND operation
    JokerDeck_CardMask_AND(result, mask1, mask2);
    
    // Only Joker and Kh should be in result
    assert(JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_JOKER));
    assert(JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS)));
    assert(!JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES)));
    assert(!JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_MAKE_CARD(JokerDeck_Rank_QUEEN, JokerDeck_Suit_DIAMONDS)));
    
    // Count should be 2
    assert(JokerDeck_numCards(result) == 2);
    
    printf("  ✓ JokerDeck_CardMask_AND works correctly with joker\n");
}

static void test_joker_mask_not(void) {
    printf("Testing JokerDeck_CardMask_NOT with joker...\n");
    
    JokerDeck_CardMask mask, result; (void)result;
    
    // Create a mask with just the joker
    JokerDeck_CardMask_RESET(mask);
    JokerDeck_CardMask_SET(mask, JokerDeck_JOKER);
    
    // NOT operation
    JokerDeck_CardMask_NOT(result, mask);
    
    // Joker should not be in result
    assert(!JokerDeck_CardMask_CARD_IS_SET(result, JokerDeck_JOKER));
    
    // All other cards should be in result
    assert(JokerDeck_numCards(result) == 52); // All cards except joker
    
    printf("  ✓ JokerDeck_CardMask_NOT works correctly with joker\n");
}

int main(void) {
    printf("=== JokerDeck Mask Operations Tests ===\n\n");
    
    test_joker_mask_or();
    test_joker_mask_and();
    test_joker_mask_not();
    
    printf("\n✅ All mask operation tests passed!\n");
    
    return 0;
}
