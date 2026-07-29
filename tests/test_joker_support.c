/*
 * test_joker_support.c - Unit tests for joker support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/universal_deck.h>

// Test parsing of joker
static void test_joker_parsing(void) {
    printf("Testing joker parsing...\n");
    
    int card;
    deck_type_t type;
    
    // Test "Xx"
    int result = Universal_StringToCard("Xx", &card, &type);
    (void)result; /* Suppress unused variable warning */
    assert(result == 2);
    assert(card == JokerDeck_JOKER);
    assert(type == UNIVERSAL_DECK_JOKER);
    
    // Test "xx"
    result = Universal_StringToCard("xx", &card, &type);
    assert(result == 2);
    assert(card == JokerDeck_JOKER);
    assert(type == UNIVERSAL_DECK_JOKER);
    
    // Test "XX"
    result = Universal_StringToCard("XX", &card, &type);
    assert(result == 2);
    assert(card == JokerDeck_JOKER);
    assert(type == UNIVERSAL_DECK_JOKER);
    
    printf("  ✓ Joker parsing works correctly\n");
}

// Test joker card mask operations
static void test_joker_cardmask(void) {
    printf("Testing joker card mask operations...\n");
    
    JokerDeck_CardMask mask;
    JokerDeck_CardMask_RESET(mask);
    
    // Set joker
    JokerDeck_CardMask_SET(mask, JokerDeck_JOKER);
    assert(JokerDeck_CardMask_CARD_IS_SET(mask, JokerDeck_JOKER));
    
    // Count should be 1
    assert(JokerDeck_numCards(mask) == 1);
    
    // Add another card
    JokerDeck_CardMask_SET(mask, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES));
    assert(JokerDeck_numCards(mask) == 2);
    
    printf("  ✓ Joker card mask operations work correctly\n");
}

// Test conversion between StdDeck and JokerDeck
static void test_deck_conversion(void) {
    printf("Testing deck conversions...\n");
    
    StdDeck_CardMask std_mask;
    JokerDeck_CardMask joker_mask;
    
    // Create a StdDeck mask with As Kh
    StdDeck_CardMask_RESET(std_mask);
    StdDeck_CardMask_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(std_mask, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    // Convert to JokerDeck
    Universal_ConvertStdToJoker(std_mask, &joker_mask);
    
    // Check cards are preserved
    assert(JokerDeck_CardMask_CARD_IS_SET(joker_mask, 
        JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES)));
    assert(JokerDeck_CardMask_CARD_IS_SET(joker_mask, 
        JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS)));
    
    // Joker should not be set
    assert(!JokerDeck_CardMask_CARD_IS_SET(joker_mask, JokerDeck_JOKER));
    
    // Count should be same
    assert(StdDeck_numCards(std_mask) == JokerDeck_numCards(joker_mask));
    
    printf("  ✓ Deck conversions work correctly\n");
}

// Test game type detection
static void test_game_type_detection(void) {
    printf("Testing game type detection...\n");
    
    // Lowball should require joker deck
    assert(Universal_IsJokerGame(game_lowball) == 1);
    assert(Universal_DetermineRequiredDeckType(game_lowball) == UNIVERSAL_DECK_JOKER);
    
    // Holdem should use standard deck
    assert(Universal_IsJokerGame(game_holdem) == 0);
    assert(Universal_DetermineRequiredDeckType(game_holdem) == UNIVERSAL_DECK_STANDARD);
    
    // 5-draw games should use joker deck
    assert(Universal_IsJokerGame(game_5draw) == 1);
    assert(Universal_IsJokerGame(game_5draw8) == 1);
    assert(Universal_IsJokerGame(game_5drawnsq) == 1);
    
    printf("  ✓ Game type detection works correctly\n");
}

// Test joker index
static void test_joker_index(void) {
    printf("Testing joker index...\n");
    
    // Joker should be card 52 (0-based index)
    assert(JokerDeck_JOKER == 52);
    
    // JokerDeck should have 53 cards total
    assert(JokerDeck_N_CARDS == 53);
    
    // StdDeck should have 52 cards
    assert(StdDeck_N_CARDS == 52);
    
    printf("  ✓ Joker index is correct (card 52 in 53-card deck)\n");
}

int main(void) {
    printf("=== Joker Support Unit Tests ===\n\n");
    
    test_joker_parsing();
    test_joker_cardmask();
    test_deck_conversion();
    test_game_type_detection();
    test_joker_index();
    
    printf("\n✅ All tests passed!\n");
    
    return 0;
}
