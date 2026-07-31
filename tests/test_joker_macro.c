/*
 * test_joker_macro.c - Direct test of JOKERDECK_ENUMERATE_COMBINATIONS_D macro
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/core/enumerate.h>

int main(int argc, char *argv[]) {
    JokerDeck_CardMask dead;
    JokerDeck_CardMask unsharedCards[1];
    int numToDeal[1];
    int count = 0;
    int max_count = 10;
    int failures = 0;
    int dead_joker_dealt = 0;
    
    printf("Testing JOKERDECK_ENUMERATE_COMBINATIONS_D directly\n");
    printf("==================================================\n\n");
    
    // Initialize
    JokerDeck_CardMask_RESET(dead);
    
    // Simple test: 1 player needs 5 cards
    numToDeal[0] = 5;
    
    printf("Test 1: 1 player, needs 5 cards, no dead cards\n");
    printf("Total possible 5-card combinations from 53 cards: C(53,5) = 2,869,685\n\n");
    
    // Enable debug
    setenv("POKER_DEBUG", "1", 1);
    
    printf("Enumerating first %d combinations...\n", max_count);
    
    JOKERDECK_ENUMERATE_COMBINATIONS_D(unsharedCards,
                                     1, numToDeal,
                                     dead, 
                                     {
                                         count++;
                                         if (count <= max_count) {
                                             printf("Combination %d: ", count);
                                             int card;
                                             for (card = 0; card < JokerDeck_N_CARDS; card++) {
                                                 if (JokerDeck_CardMask_CARD_IS_SET(unsharedCards[0], card)) {
                                                     if (card == JokerDeck_JOKER) {
                                                         printf("Xx ");
                                                     } else {
                                                         static const char *ranks = "23456789TJQKA";
                                                         static const char *suits = "hdcs";
                                                         printf("%c%c ", 
                                                                ranks[JokerDeck_RANK(card)], 
                                                                suits[JokerDeck_SUIT(card)]);
                                                     }
                                                 }
                                             }
                                             printf("\n");
                                         }
                                         if (count == max_count) {
                                             printf("\nOnly the first %d are printed\n", max_count);
                                         }
                                     });

    /* No early exit: JOKERDECK_ENUMERATE_COMBINATIONS_D frees its combination
     * tables after the loop, so jumping out of the action body leaks them.
     * Running to the end also makes the total checkable: every 5-card
     * combination of the 53-card joker deck, C(53,5). */
    printf("\nTotal combinations enumerated: %d\n", count);
    if (count != 2869685) {
        printf("ERROR: expected C(53,5) = 2869685 combinations, got %d\n", count);
        failures++;
    }

    // Test 2: With some dead cards including the joker
    printf("\n\nTest 2: 1 player, needs 3 cards, joker is dead\n");
    printf("==============================================\n");
    
    JokerDeck_CardMask_RESET(dead);
    JokerDeck_CardMask_SET(dead, JokerDeck_JOKER);
    JokerDeck_CardMask_SET(dead, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES));
    
    numToDeal[0] = 3;
    count = 0;
    
    printf("Dead cards: Xx As\n");
    printf("Available cards: 51\n");
    printf("Enumerating first %d combinations...\n", max_count);
    
    JOKERDECK_ENUMERATE_COMBINATIONS_D(unsharedCards,
                                     1, numToDeal,
                                     dead, 
                                     {
                                         count++;
                                         /* The joker is dead, so it must not appear in any
                                          * combination — checked on all of them, not just
                                          * the ones printed below. */
                                         if (JokerDeck_CardMask_CARD_IS_SET(unsharedCards[0],
                                                                            JokerDeck_JOKER)) {
                                             dead_joker_dealt++;
                                         }
                                         if (count <= max_count) {
                                             printf("Combination %d: ", count);
                                             int card;
                                             int has_joker = 0;
                                             for (card = 0; card < JokerDeck_N_CARDS; card++) {
                                                 if (JokerDeck_CardMask_CARD_IS_SET(unsharedCards[0], card)) {
                                                     if (card == JokerDeck_JOKER) {
                                                         printf("Xx ");
                                                         has_joker = 1;
                                                     } else {
                                                         static const char *ranks = "23456789TJQKA";
                                                         static const char *suits = "hdcs";
                                                         printf("%c%c ", 
                                                                ranks[JokerDeck_RANK(card)], 
                                                                suits[JokerDeck_SUIT(card)]);
                                                     }
                                                 }
                                             }
                                             if (has_joker) {
                                                 printf(" <-- ERROR: Has joker but joker is dead!");
                                             }
                                             printf("\n");
                                         }
                                         if (count == max_count) {
                                             printf("\nOnly the first %d are printed\n", max_count);
                                         }
                                     });

    printf("\nTotal combinations enumerated: %d\n", count);
    /* 53 cards less the two dead ones, taken 3 at a time: C(51,3). */
    if (count != 20825) {
        printf("ERROR: expected C(51,3) = 20825 combinations, got %d\n", count);
        failures++;
    }
    if (dead_joker_dealt != 0) {
        printf("ERROR: dead joker was dealt in %d combination(s)\n", dead_joker_dealt);
        failures++;
    }

    if (failures > 0) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll checks passed\n");
    return 0;
}
