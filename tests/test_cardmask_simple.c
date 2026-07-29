/*
 * test_cardmask_simple.c - Simple test without string functions
 */

#include <poker_eval/core/cardmask_compat.h>
#include <stdio.h>
#include <assert.h>

int main() {
    printf("Testing CardMask basic compatibility...\n");

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

    // Test 3: Population count
    assert(cardmask_popcount(single_cardmask) == 1);
    printf("✅ Population count\n");

    printf("✅ Basic CardMask compatibility tests passed!\n");
    return 0;
}
