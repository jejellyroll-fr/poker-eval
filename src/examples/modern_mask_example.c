/*
 * modern_mask_example.c - Example demonstrating modern card mask architecture
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <poker_eval/core/modern_cardmask.h>

/* Test basic mask operations */
static void test_basic_operations(void) {
    printf("=== Testing Basic Mask Operations ===\n");

    /* Create empty mask */
    mask_t hand = MASK_EMPTY;
    assert(mask_is_empty(hand));
    assert(mask_popcount(hand) == 0);

    /* Add some cards */
    hand = mask_set(hand, MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES));
    hand = mask_set(hand, MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS));
    hand = mask_set(hand, MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_DIAMONDS));
    hand = mask_set(hand, MODERN_MAKE_CARD(MODERN_RANK_J, MODERN_SUIT_CLUBS));
    hand = mask_set(hand, MODERN_MAKE_CARD(MODERN_RANK_T, MODERN_SUIT_SPADES));

    printf("Hand after adding 5 cards: ");
    mask_print(hand);

    assert(mask_popcount(hand) == 5);
    assert(mask_is_set(hand, MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES)));
    assert(!mask_is_set(hand, MODERN_MAKE_CARD(MODERN_RANK_2, MODERN_SUIT_CLUBS)));

    /* Test removal */
    hand = mask_unset(hand, MODERN_MAKE_CARD(MODERN_RANK_T, MODERN_SUIT_SPADES));
    assert(mask_popcount(hand) == 4);

    printf("✓ Basic operations work correctly\n\n");
}

/* Test string conversion */
static void test_string_conversion(void) {
    printf("=== Testing String Conversion ===\n");

    /* Test mask to string */
    mask_t hand = string_to_mask("As Kh Qd Jc");
    char buffer[256];
    const char* result = mask_to_string(hand, buffer, sizeof(buffer));

    printf("String to mask to string: \"As Kh Qd Jc\" -> %s\n", result);
    assert(mask_popcount(hand) == 4);

    /* Test individual cards */
    assert(mask_is_set(hand, MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES)));
    assert(mask_is_set(hand, MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_HEARTS)));
    assert(mask_is_set(hand, MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_DIAMONDS)));
    assert(mask_is_set(hand, MODERN_MAKE_CARD(MODERN_RANK_J, MODERN_SUIT_CLUBS)));

    printf("✓ String conversion works correctly\n\n");
}

/* Test set operations */
static void test_set_operations(void) {
    printf("=== Testing Set Operations ===\n");

    mask_t hand1 = string_to_mask("As Kh Qd");
    mask_t hand2 = string_to_mask("Qd Jc Ts");

    printf("Hand 1: ");
    mask_print(hand1);
    printf("Hand 2: ");
    mask_print(hand2);

    /* Union */
    mask_t union_mask = mask_union(hand1, hand2);
    printf("Union: ");
    mask_print(union_mask);
    assert(mask_popcount(union_mask) == 5); /* As Kh Qd Jc Ts */

    /* Intersection */
    mask_t intersect_mask = mask_intersect(hand1, hand2);
    printf("Intersection: ");
    mask_print(intersect_mask);
    assert(mask_popcount(intersect_mask) == 1); /* Qd */

    /* Subtraction */
    mask_t subtract_mask = mask_subtract(hand1, hand2);
    printf("Hand1 - Hand2: ");
    mask_print(subtract_mask);
    assert(mask_popcount(subtract_mask) == 2); /* As Kh */

    printf("✓ Set operations work correctly\n\n");
}

/* Test iteration */
static void test_iteration(void) {
    printf("=== Testing Iteration ===\n");

    mask_t hand = string_to_mask("As Kh Qd Jc Ts");
    printf("Iterating through hand: ");
    mask_print(hand);

    printf("Cards in hand:\n");
    int count = 0;
    MASK_FOR_EACH_SET_BIT(hand, card) {
        int rank = MODERN_GET_RANK(card);
        int suit = MODERN_GET_SUIT(card);
        printf("  Card %d: Rank %d, Suit %d\n", card, rank, suit);
        count++;
    }

    assert(count == 5);
    (void)count; /* Suppress unused warning */
    printf("✓ Iteration works correctly\n\n");
}

/* Test rank and suit extraction */
static void test_rank_suit_extraction(void) {
    printf("=== Testing Rank/Suit Extraction ===\n");

    mask_t hand = string_to_mask("As Ah Ad Kh Ks");
    printf("Hand with pairs: ");
    mask_print(hand);

    /* Count aces */
    int ace_count = 0;
    for (int suit = 0; suit < 4; suit++) {
        if (mask_is_set(hand, MODERN_MAKE_CARD(MODERN_RANK_A, suit))) {
            ace_count++;
        }
    }
    printf("Aces in hand: %d\n", ace_count);
    assert(ace_count == 3);

    /* Count spades */
    int spade_count = 0;
    for (int rank = 0; rank < 13; rank++) {
        if (mask_is_set(hand, MODERN_MAKE_CARD(rank, MODERN_SUIT_SPADES))) {
            spade_count++;
        }
    }
    printf("Spades in hand: %d\n", spade_count);
    assert(spade_count == 2); /* As, Ks */

    printf("✓ Rank/suit extraction works correctly\n\n");
}

/* Performance benchmark */
static void test_performance(void) {
    printf("=== Performance Benchmark ===\n");

    mask_performance_t perf = mask_benchmark(10000);

    printf("Performance results (10,000 iterations):\n");
    printf("  Set operations: %.0f ops/sec\n", perf.set_ops_per_sec);
    printf("  Popcount: %.0f ops/sec\n", perf.popcount_ops_per_sec);
    printf("  Iteration: %.0f ops/sec\n", perf.iteration_ops_per_sec);

    printf("✓ Performance benchmark completed\n\n");
}

/* Test validation */
static void test_validation(void) {
    printf("=== Testing Validation ===\n");

    /* Valid hand */
    mask_t valid_hand = string_to_mask("As Kh Qd Jc Ts");
    assert(mask_validate_hand(valid_hand, 5));
    assert(mask_validate_hand(valid_hand, -1)); /* Any number of cards */

    /* Invalid card count */
    assert(!mask_validate_hand(valid_hand, 7));

    /* Empty hand */
    mask_t empty_hand = MASK_EMPTY;
    assert(mask_validate_hand(empty_hand, 0));
    assert(!mask_validate_hand(empty_hand, 1));

    (void)valid_hand; /* Suppress unused warning */
    (void)empty_hand; /* Suppress unused warning */

    printf("✓ Validation works correctly\n\n");
}

int main(void) {
    printf("Modern Card Mask Architecture Test\n");
    printf("==================================\n\n");

    test_basic_operations();
    test_string_conversion();
    test_set_operations();
    test_iteration();
    test_rank_suit_extraction();
    test_performance();
    test_validation();

    printf("🎉 All tests passed! Modern mask architecture is working.\n");
    return 0;
}
