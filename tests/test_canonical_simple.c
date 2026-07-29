/*
 * Simple test for canonical 5-card evaluator
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/canonical_5card.h>
#include <poker_eval/core/modern_cardmask.h>

int main(void) {
    printf("Testing canonical 5-card evaluator...\n");

    // Create a simple royal flush manually
    mask_t royal = MASK_EMPTY;
    royal = mask_set(royal, MODERN_MAKE_CARD(MODERN_RANK_A, MODERN_SUIT_SPADES));
    royal = mask_set(royal, MODERN_MAKE_CARD(MODERN_RANK_K, MODERN_SUIT_SPADES));
    royal = mask_set(royal, MODERN_MAKE_CARD(MODERN_RANK_Q, MODERN_SUIT_SPADES));
    royal = mask_set(royal, MODERN_MAKE_CARD(MODERN_RANK_J, MODERN_SUIT_SPADES));
    royal = mask_set(royal, MODERN_MAKE_CARD(MODERN_RANK_T, MODERN_SUIT_SPADES));

    printf("Created royal flush: ");
    char buffer[256];
    mask_to_string(royal, buffer, sizeof(buffer));
    printf("%s\n", buffer);

    // Test classification
    hand_class_t class = classify_5card_hand(royal);
    printf("Hand class: %d\n", (int)class);

    // Test evaluation
    uint32_t eval = evaluate_5card_canonical(royal);
    printf("Evaluation: %u\n", eval);

    /* Validate wheel straight detection */
    mask_t wheel = string_to_mask("Ah 2s 3d 4c 5h");
    hand_class_t wheel_class = classify_5card_hand(wheel);
    assert(wheel_class == HAND_STRAIGHT);

    /* Ensure A-K-Q-J-9 is not falsely flagged as a straight */
    mask_t akqj9 = string_to_mask("Ah Kd Qc Jh 9s");
    hand_class_t akqj9_class = classify_5card_hand(akqj9);
    assert(akqj9_class == HAND_HIGH_CARD);

    /* Confirm T-J-Q-K-A remains a straight */
    mask_t broadway = string_to_mask("Th Jd Qc Kh As");
    hand_class_t broadway_class = classify_5card_hand(broadway);
    assert(broadway_class == HAND_STRAIGHT);

    printf("Straight edge case checks passed.\n");

    return 0;
}
