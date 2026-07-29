/*
 * test_cardmask_compat.c - Test CardMask compatibility layer
 */

#include <poker_eval/core/cardmask_compat.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main() {
    printf("Testing CardMask compatibility layer...\n");

    // Test 1: Empty mask conversion
    StdDeck_CardMask empty_cardmask;
    StdDeck_CardMask_RESET(empty_cardmask);

    mask_t empty_mask = cardmask_to_mask_t(empty_cardmask);
    assert(empty_mask == MASK_EMPTY);
    printf("✅ Empty mask conversion\n");

    // Test 2: Single card conversion
    StdDeck_CardMask single_cardmask;
    StdDeck_CardMask_RESET(single_cardmask);
    StdDeck_CardMask_SET(single_cardmask, 0);  // 2 of clubs

    mask_t single_mask = cardmask_to_mask_t(single_cardmask);
    assert(mask_popcount(single_mask) == 1);
    assert(mask_is_set(single_mask, 0));  // Should be card 0 (2c)
    printf("✅ Single card conversion\n");

    // Test 3: Round-trip conversion
    StdDeck_CardMask original_cardmask;
    StdDeck_CardMask_RESET(original_cardmask);
    StdDeck_CardMask_SET(original_cardmask, 0);   // 2c
    StdDeck_CardMask_SET(original_cardmask, 12);  // Ks
    StdDeck_CardMask_SET(original_cardmask, 51);  // As

    mask_t intermediate_mask = cardmask_to_mask_t(original_cardmask);
    StdDeck_CardMask roundtrip_cardmask = mask_t_to_cardmask(intermediate_mask);

    assert(StdDeck_CardMask_EQUAL(original_cardmask, roundtrip_cardmask));
    printf("✅ Round-trip conversion\n");

    // Test 4: Population count
    assert(cardmask_popcount(original_cardmask) == 3);
    printf("✅ Population count\n");

    // Test 5: Valid hand check
    assert(cardmask_is_valid_hand(original_cardmask, 3));
    assert(!cardmask_is_valid_hand(original_cardmask, 5));
    printf("✅ Valid hand check\n");

    // Test 6: String conversion
    char buffer[256];
    char* result = cardmask_to_string(original_cardmask, buffer, sizeof(buffer));
    assert(result != NULL);
    printf("✅ String conversion: %s\n", buffer);

    // Test 7: Modern operations
    StdDeck_CardMask mask1, mask2, result_mask;
    StdDeck_CardMask_RESET(mask1);
    StdDeck_CardMask_RESET(mask2);

    StdDeck_CardMask_SET(mask1, 0);   // 2c
    StdDeck_CardMask_SET(mask2, 1);   // 2d

    MODERN_CardMask_OR(result_mask, mask1, mask2);
    assert(cardmask_popcount(result_mask) == 2);
    printf("✅ Modern OR operation\n");

    printf("✅ All CardMask compatibility tests passed!\n");
    return 0;
}
