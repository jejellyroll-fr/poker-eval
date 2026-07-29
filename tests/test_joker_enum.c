/*
 * test_joker_enum.c - Test program for debugging JokerDeck enumeration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/games/rules_joker.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/universal_deck.h>

static void print_card(int card) {
    if (card == JokerDeck_JOKER) {
        printf("Xx");
    } else {
        static const char *ranks = "23456789TJQKA";
        static const char *suits = "hdcs";
        printf("%c%c", ranks[JokerDeck_RANK(card)], suits[JokerDeck_SUIT(card)]);
    }
}

static void print_mask(JokerDeck_CardMask mask) {
    int card;
    int first = 1;
    
    printf("{ ");
    for (card = 0; card < JokerDeck_N_CARDS; card++) {
        if (JokerDeck_CardMask_CARD_IS_SET(mask, card)) {
            if (!first) printf(" ");
            print_card(card);
            first = 0;
        }
    }
    printf(" }");
}

int main(int argc, char *argv[]) {
    JokerDeck_CardMask pocket1, pocket2, dead;
    JokerDeck_CardMask unsharedCards[2];
    int numToDeal[2];
    int count = 0;
    
    printf("Testing JokerDeck enumeration with simple case\n");
    printf("==============================================\n\n");
    
    // Initialize masks
    JokerDeck_CardMask_RESET(pocket1);
    JokerDeck_CardMask_RESET(pocket2);
    JokerDeck_CardMask_RESET(dead);
    
    // Player 1 has Xx (joker)
    JokerDeck_CardMask_SET(pocket1, JokerDeck_JOKER);
    printf("Player 1 pocket: ");
    print_mask(pocket1);
    printf(" (has joker)\n");
    
    // Player 2 has As
    int as_card = JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES);
    JokerDeck_CardMask_SET(pocket2, as_card);
    printf("Player 2 pocket: ");
    print_mask(pocket2);
    printf("\n");
    
    // Dead cards include both pockets
    JokerDeck_CardMask_OR(dead, pocket1, pocket2);
    printf("Dead cards: ");
    print_mask(dead);
    printf("\n\n");
    
    // Each player needs 4 more cards (5 total - 1 already)
    numToDeal[0] = 4;
    numToDeal[1] = 4;
    
    printf("Each player needs 4 more cards\n");
    printf("Total cards in JokerDeck: %d\n", JokerDeck_N_CARDS);
    printf("Dead cards count: %d\n", JokerDeck_numCards(dead));
    printf("Available cards: %d\n\n", JokerDeck_N_CARDS - JokerDeck_numCards(dead));
    
    // Test if we have the joker card correctly
    printf("Testing joker card:\n");
    printf("  JokerDeck_JOKER = %d\n", JokerDeck_JOKER);
    printf("  Is joker in pocket1? %s\n", 
           JokerDeck_CardMask_CARD_IS_SET(pocket1, JokerDeck_JOKER) ? "YES" : "NO");
    printf("  Is joker in dead? %s\n\n", 
           JokerDeck_CardMask_CARD_IS_SET(dead, JokerDeck_JOKER) ? "YES" : "NO");
    
    // Enable debug mode
    setenv("POKER_DEBUG", "1", 1);
    
    printf("Starting enumeration (first 10 combinations)...\n");
    printf("================================================\n");
    
    // Use the macro to enumerate combinations
    JOKERDECK_ENUMERATE_COMBINATIONS_D(unsharedCards,
                                     2, numToDeal,
                                     dead, 
                                     {
                                         count++;
                                         if (count <= 10) {
                                             printf("\nCombination %d:\n", count);
                                             printf("  Player 1 gets: ");
                                             print_mask(unsharedCards[0]);
                                             printf("\n  Player 2 gets: ");
                                             print_mask(unsharedCards[1]);
                                             printf("\n");
                                         }
                                         if (count == 10) {
                                             printf("\n... (stopping after 10 combinations)\n");
                                             break;
                                         }
                                     });
    
    printf("\nTotal combinations enumerated: %d\n", count);
    
    return 0;
}
