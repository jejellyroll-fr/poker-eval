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
                                         if (count >= max_count) {
                                             printf("\nStopping after %d combinations\n", max_count);
                                             goto done;
                                         }
                                     });
    
done:
    printf("\nTotal combinations enumerated: %d\n", count);
    
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
                                         if (count >= max_count) {
                                             printf("\nStopping after %d combinations\n", max_count);
                                             goto done2;
                                         }
                                     });
    
done2:
    printf("\nTotal combinations enumerated: %d\n", count);
    
    return 0;
}
