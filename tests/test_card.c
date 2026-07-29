#include <stdio.h>
#include <assert.h>
#include <poker_eval/core/card.h>

/* Function prototypes */
static void test_char_to_rank(void);
static void test_char_to_suit(void);
static void test_rank_to_char(void);
static void test_suit_to_char(void);
static void test_card_init_destroy(void);
static void test_round_trip_conversions(void);

// Test function to verify all rank conversions
static void test_char_to_rank(void) {
    printf("Testing CharToRank...\n");
    
    // Test uppercase letters
    assert(CharToRank('A') == 12);
    assert(CharToRank('K') == 11);
    assert(CharToRank('Q') == 10);
    assert(CharToRank('J') == 9);
    assert(CharToRank('T') == 8);
    
    // Test lowercase letters
    assert(CharToRank('a') == 12);
    assert(CharToRank('k') == 11);
    assert(CharToRank('q') == 10);
    assert(CharToRank('j') == 9);
    assert(CharToRank('t') == 8);
    
    // Test numeric cards
    assert(CharToRank('9') == 7);
    assert(CharToRank('8') == 6);
    assert(CharToRank('7') == 5);
    assert(CharToRank('6') == 4);
    assert(CharToRank('5') == 3);
    assert(CharToRank('4') == 2);
    assert(CharToRank('3') == 1);
    assert(CharToRank('2') == 0);
    
    // Test special case for joker
    assert(CharToRank('X') == 0);
    assert(CharToRank('x') == 0);
    
    // Test invalid characters
    assert(CharToRank('1') == -1);
    assert(CharToRank('0') == -1);
    assert(CharToRank('Z') == -1);
    assert(CharToRank('!') == -1);
    assert(CharToRank(' ') == -1);
    
    printf("  ✓ All CharToRank tests passed\n");
}

// Test function to verify all suit conversions
static void test_char_to_suit(void) {
    printf("Testing CharToSuit...\n");
    
    // Test uppercase letters
    assert(CharToSuit('S') == 3);
    assert(CharToSuit('C') == 2);
    assert(CharToSuit('D') == 1);
    assert(CharToSuit('H') == 0);
    
    // Test lowercase letters
    assert(CharToSuit('s') == 3);
    assert(CharToSuit('c') == 2);
    assert(CharToSuit('d') == 1);
    assert(CharToSuit('h') == 0);
    
    // Test invalid characters
    assert(CharToSuit('X') == -1);
    assert(CharToSuit('A') == -1);
    assert(CharToSuit('1') == -1);
    assert(CharToSuit('!') == -1);
    assert(CharToSuit(' ') == -1);
    
    printf("  ✓ All CharToSuit tests passed\n");
}

// Test function to verify rank to char conversion
static void test_rank_to_char(void) {
    printf("Testing RankToChar...\n");
    
    // Test all valid ranks
    assert(RankToChar(0) == '2');
    assert(RankToChar(1) == '3');
    assert(RankToChar(2) == '4');
    assert(RankToChar(3) == '5');
    assert(RankToChar(4) == '6');
    assert(RankToChar(5) == '7');
    assert(RankToChar(6) == '8');
    assert(RankToChar(7) == '9');
    assert(RankToChar(8) == 'T');
    assert(RankToChar(9) == 'J');
    assert(RankToChar(10) == 'Q');
    assert(RankToChar(11) == 'K');
    assert(RankToChar(12) == 'A');
    
    printf("  ✓ All RankToChar tests passed\n");
}

// Test function to verify suit to char conversion
static void test_suit_to_char(void) {
    printf("Testing SuitToChar...\n");
    
    // Test all valid suits
    assert(SuitToChar(0) == 'h');
    assert(SuitToChar(1) == 'd');
    assert(SuitToChar(2) == 'c');
    assert(SuitToChar(3) == 's');
    
    printf("  ✓ All SuitToChar tests passed\n");
}

// Test Card structure initialization and destruction
static void test_card_init_destroy(void) {
    printf("Testing Card_init and Card_destroy...\n");
    
    Card card;
    Card_init(&card);
    
    // Verify that the card is properly initialized
    assert(card.rankChars != NULL);
    assert(card.suitChars != NULL);
    
    // Test that we can access the rank and suit chars
    assert(card.rankChars[0] == '2');
    assert(card.rankChars[12] == 'A');
    assert(card.suitChars[0] == 'h');
    assert(card.suitChars[3] == 's');
    
    Card_destroy(&card);
    
    printf("  ✓ Card_init and Card_destroy tests passed\n");
}

// Test round-trip conversions
static void test_round_trip_conversions(void) {
    printf("Testing round-trip conversions...\n");
    
    // Test rank round-trips
    for (int rank = 0; rank <= 12; rank++) {
        char c = RankToChar(rank);
        int converted_rank = CharToRank(c);
        assert(converted_rank == rank);
        (void)converted_rank; // Suppress unused variable warning
    }
    
    // Test suit round-trips
    for (int suit = 0; suit <= 3; suit++) {
        char c = SuitToChar(suit);
        int converted_suit = CharToSuit(c);
        assert(converted_suit == suit);
        (void)converted_suit; // Suppress unused variable warning
    }
    
    printf("  ✓ All round-trip conversion tests passed\n");
}

int main(void) {
    printf("=== Running Card.c Unit Tests ===\n\n");
    
    test_card_init_destroy();
    test_char_to_rank();
    test_char_to_suit();
    test_rank_to_char();
    test_suit_to_char();
    test_round_trip_conversions();
    
    printf("\n=== All Card.c tests passed! ===\n");
    
    return 0;
}
