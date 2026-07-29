#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <poker_eval/distributions/card_converter.h>
#include <poker_eval/deck/deck_std.h>

/* Function prototypes */
static int masks_equal(StdDeck_CardMask mask1, StdDeck_CardMask mask2);
static void test_init_poker_eval_cards(void);
static void test_text_to_poker_eval_single_cards(void);
static void test_text_to_poker_eval_multiple_cards(void);
static void test_text_to_poker_eval_invalid(void);
static void test_text_to_poker_eval_array(void);
static void test_poker_tracker_to_poker_eval(void);
static void test_unique_card_masks(void);
static void test_combining_cards(void);

// Helper function to check if two card masks are equal
static int masks_equal(StdDeck_CardMask mask1, StdDeck_CardMask mask2) {
    return mask1.cards_n == mask2.cards_n;
}

// Test InitPokerEvalCards function
static void test_init_poker_eval_cards(void) {
    printf("Testing InitPokerEvalCards...\n");
    
    InitPokerEvalCards();
    
    // Test that index 0 is empty
    assert(PokerEvalCards[0].cards_n == 0);
    
    // Test first card (2c - index 1)
    assert(PokerEvalCards[1].cards_n != 0);
    
    // Test last card (As - index 52)
    assert(PokerEvalCards[52].cards_n != 0);
    
    // Verify all cards are different (except index 0)
    for (int i = 1; i <= 52; i++) {
        for (int j = i + 1; j <= 52; j++) {
            assert(PokerEvalCards[i].cards_n != PokerEvalCards[j].cards_n);
        }
    }
    
    printf("  ✓ InitPokerEvalCards test passed\n");
}

// Test TextToPokerEval function with single cards
static void test_text_to_poker_eval_single_cards(void) {
    printf("Testing TextToPokerEval with single cards...\n");
    
    // Test all ranks with clubs
    const char* club_cards[] = {"2c", "3c", "4c", "5c", "6c", "7c", "8c", "9c", "Tc", "Jc", "Qc", "Kc", "Ac"};
    for (int i = 0; i < 13; i++) {
        StdDeck_CardMask mask = TextToPokerEval(club_cards[i]);
        assert(mask.cards_n != 0);
        (void)mask; // Suppress unused variable warning
    }
    
    // Test all ranks with diamonds
    const char* diamond_cards[] = {"2d", "3d", "4d", "5d", "6d", "7d", "8d", "9d", "Td", "Jd", "Qd", "Kd", "Ad"};
    for (int i = 0; i < 13; i++) {
        StdDeck_CardMask mask = TextToPokerEval(diamond_cards[i]);
        assert(mask.cards_n != 0);
        (void)mask; // Suppress unused variable warning
    }
    
    // Test all ranks with hearts
    const char* heart_cards[] = {"2h", "3h", "4h", "5h", "6h", "7h", "8h", "9h", "Th", "Jh", "Qh", "Kh", "Ah"};
    for (int i = 0; i < 13; i++) {
        StdDeck_CardMask mask = TextToPokerEval(heart_cards[i]);
        assert(mask.cards_n != 0);
        (void)mask; // Suppress unused variable warning
    }
    
    // Test all ranks with spades
    const char* spade_cards[] = {"2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "Ts", "Js", "Qs", "Ks", "As"};
    for (int i = 0; i < 13; i++) {
        StdDeck_CardMask mask = TextToPokerEval(spade_cards[i]);
        assert(mask.cards_n != 0);
        (void)mask; // Suppress unused variable warning
    }
    
    printf("  ✓ Single card conversion tests passed\n");
}

// Test TextToPokerEval function with multiple cards
static void test_text_to_poker_eval_multiple_cards(void) {
    printf("Testing TextToPokerEval with multiple cards...\n");
    
    // Test pair
    StdDeck_CardMask pair_mask = TextToPokerEval("AcAd");
    assert(pair_mask.cards_n != 0);
    (void)pair_mask; // Suppress unused variable warning
    
    // Test three cards
    StdDeck_CardMask three_mask = TextToPokerEval("KhQdJc");
    assert(three_mask.cards_n != 0);
    (void)three_mask; // Suppress unused variable warning
    
    // Test five cards (full hand)
    StdDeck_CardMask hand_mask = TextToPokerEval("AcKcQcJcTc");
    assert(hand_mask.cards_n != 0);
    (void)hand_mask; // Suppress unused variable warning
    
    // Test seven cards
    StdDeck_CardMask seven_mask = TextToPokerEval("AsKsQsJsTs9s8s");
    assert(seven_mask.cards_n != 0);
    (void)seven_mask; // Suppress unused variable warning
    
    // Test empty string
    StdDeck_CardMask empty_mask = TextToPokerEval("");
    assert(empty_mask.cards_n == 0);
    (void)empty_mask; // Suppress unused variable warning
    
    // Test odd length string (should only process pairs)
    StdDeck_CardMask odd_mask = TextToPokerEval("Ac2");
    assert(odd_mask.cards_n != 0); // Should process "Ac" only
    (void)odd_mask; // Suppress unused variable warning
    
    printf("  ✓ Multiple card conversion tests passed\n");
}

// Test TextToPokerEval with invalid inputs
static void test_text_to_poker_eval_invalid(void) {
    printf("Testing TextToPokerEval with invalid inputs...\n");
    
    // Test invalid rank
    StdDeck_CardMask invalid_rank = TextToPokerEval("Xc");
    // Should handle gracefully (might be 0 or might process as invalid)
    (void)invalid_rank; // Suppress unused variable warning
    
    // Test invalid suit
    StdDeck_CardMask invalid_suit = TextToPokerEval("Ax");
    // Should handle gracefully
    (void)invalid_suit; // Suppress unused variable warning
    
    // Test completely invalid
    StdDeck_CardMask invalid_both = TextToPokerEval("ZZ");
    // Should handle gracefully
    (void)invalid_both; // Suppress unused variable warning
    
    // Test lowercase (should work)
    StdDeck_CardMask lowercase = TextToPokerEval("ac");
    assert(lowercase.cards_n != 0);
    (void)lowercase; // Suppress unused variable warning
    
    printf("  ✓ Invalid input tests passed\n");
}

// Test TextToPokerEvalArray function
static void test_text_to_poker_eval_array(void) {
    printf("Testing TextToPokerEvalArray...\n");
    
    StdDeck_CardMask array[7];
    
    // Test with 5 cards
    int num_cards = TextToPokerEvalArray("AcKdQhJsTc", array);
    assert(num_cards == 5);
    for (int i = 0; i < num_cards; i++) {
        assert(array[i].cards_n != 0);
    }
    
    // Test with 7 cards (maximum)
    num_cards = TextToPokerEvalArray("AsKsQsJsTs9s8s", array);
    assert(num_cards == 7);
    for (int i = 0; i < num_cards; i++) {
        assert(array[i].cards_n != 0);
    }
    
    // Test with more than 7 cards (should stop at 7)
    num_cards = TextToPokerEvalArray("AsKsQsJsTs9s8s7s6s", array);
    assert(num_cards == 7);
    
    // Test with empty string
    num_cards = TextToPokerEvalArray("", array);
    assert(num_cards == 0);
    
    // Test with single card
    num_cards = TextToPokerEvalArray("Ac", array);
    assert(num_cards == 1);
    assert(array[0].cards_n != 0);
    
    // Test with odd length string
    num_cards = TextToPokerEvalArray("AcKd2", array);
    assert(num_cards == 2); // Should process "Ac" and "Kd" only
    
    printf("  ✓ TextToPokerEvalArray tests passed\n");
}

// Test PokerTrackerToPokerEval function
static void test_poker_tracker_to_poker_eval(void) {
    printf("Testing PokerTrackerToPokerEval...\n");
    
    // Test valid range (1-52)
    for (int i = 1; i <= 52; i++) {
        StdDeck_CardMask mask = PokerTrackerToPokerEval(i);
        assert(mask.cards_n != 0);
        assert(masks_equal(mask, PokerEvalCards[i]));
        (void)mask; // Suppress unused variable warning
    }
    
    // Test invalid values
    StdDeck_CardMask invalid_low = PokerTrackerToPokerEval(0);
    assert(invalid_low.cards_n == 0);
    (void)invalid_low; // Suppress unused variable warning
    
    StdDeck_CardMask invalid_high = PokerTrackerToPokerEval(53);
    assert(invalid_high.cards_n == 0);
    (void)invalid_high; // Suppress unused variable warning
    
    StdDeck_CardMask invalid_negative = PokerTrackerToPokerEval(-1);
    assert(invalid_negative.cards_n == 0);
    (void)invalid_negative; // Suppress unused variable warning
    
    StdDeck_CardMask invalid_large = PokerTrackerToPokerEval(100);
    assert(invalid_large.cards_n == 0);
    (void)invalid_large; // Suppress unused variable warning
    
    printf("  ✓ PokerTrackerToPokerEval tests passed\n");
}

// Test that different cards produce different masks
static void test_unique_card_masks(void) {
    printf("Testing unique card masks...\n");
    
    // Test that each single card produces a unique mask
    const char* all_cards[] = {
        "2c", "3c", "4c", "5c", "6c", "7c", "8c", "9c", "Tc", "Jc", "Qc", "Kc", "Ac",
        "2d", "3d", "4d", "5d", "6d", "7d", "8d", "9d", "Td", "Jd", "Qd", "Kd", "Ad",
        "2h", "3h", "4h", "5h", "6h", "7h", "8h", "9h", "Th", "Jh", "Qh", "Kh", "Ah",
        "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "Ts", "Js", "Qs", "Ks", "As"
    };
    
    // Get all masks and verify all are unique
    for (int i = 0; i < 52; i++) {
        StdDeck_CardMask mask_i = TextToPokerEval(all_cards[i]);
        assert(mask_i.cards_n != 0);
        (void)mask_i; // Suppress unused variable warning
        
        for (int j = i + 1; j < 52; j++) {
            StdDeck_CardMask mask_j = TextToPokerEval(all_cards[j]);
            assert(mask_j.cards_n != 0);
            assert(!masks_equal(mask_i, mask_j));
            (void)mask_j; // Suppress unused variable warning
        }
    }
    
    printf("  ✓ Unique card mask tests passed\n");
}

// Test combining cards
static void test_combining_cards(void) {
    printf("Testing combining cards...\n");
    
    // Get individual card masks
    StdDeck_CardMask ace_clubs = TextToPokerEval("Ac");
    StdDeck_CardMask king_hearts = TextToPokerEval("Kh");
    
    // Get combined mask
    StdDeck_CardMask combined = TextToPokerEval("AcKh");
    (void)combined; // Suppress unused variable warning
    
    // Verify the combined mask includes both cards
    StdDeck_CardMask manual_combined;
    StdDeck_CardMask_RESET(manual_combined);
    StdDeck_CardMask_OR(manual_combined, ace_clubs, king_hearts);
    
    assert(masks_equal(combined, manual_combined));
    (void)manual_combined; // Suppress unused variable warning
    
    printf("  ✓ Card combination tests passed\n");
}

int main(void) {
    printf("=== Running CardConverter.c Unit Tests ===\n\n");
    
    // Initialize the card table first
    InitPokerEvalCards();
    
    // Run all tests
    test_init_poker_eval_cards();
    test_text_to_poker_eval_single_cards();
    test_text_to_poker_eval_multiple_cards();
    test_text_to_poker_eval_invalid();
    test_text_to_poker_eval_array();
    test_poker_tracker_to_poker_eval();
    test_unique_card_masks();
    test_combining_cards();
    
    printf("\n=== All CardConverter.c tests passed! ===\n");
    
    return 0;
}
