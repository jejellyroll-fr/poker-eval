/*
 * range_equity_calc.c - Calculate range equities using pokenum functionality
 * 
 * This program demonstrates how to calculate range equities in pure C
 * by enumerating all possible hand combinations and averaging results.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

// Structure to hold a range of hands
typedef struct {
    StdDeck_CardMask *hands;
    int count;
    int capacity;
} HandRange;

// Initialize a hand range
static void initRange(HandRange *range) {
    range->capacity = 100;
    range->hands = malloc(sizeof(StdDeck_CardMask) * range->capacity);
    range->count = 0;
}

// Add a hand to the range
static void addHandToRange(HandRange *range, StdDeck_CardMask hand) {
    if (range->count >= range->capacity) {
        range->capacity *= 2;
        range->hands = realloc(range->hands, sizeof(StdDeck_CardMask) * range->capacity);
    }
    range->hands[range->count++] = hand;
}

// Free range memory
static void freeRange(HandRange *range) {
    free(range->hands);
    range->hands = NULL;
    range->count = 0;
    range->capacity = 0;
}

// Generate all pocket pairs from AA to 22
static void generatePocketPairs(HandRange *range, int minRank, int maxRank) {
    int rank1, suit1, suit2;
    StdDeck_CardMask hand;
    
    for (rank1 = minRank; rank1 <= maxRank; rank1++) {
        for (suit1 = 0; suit1 < 4; suit1++) {
            for (suit2 = suit1 + 1; suit2 < 4; suit2++) {
                StdDeck_CardMask_RESET(hand);
                StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank1, suit1));
                StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank1, suit2));
                addHandToRange(range, hand);
            }
        }
    }
}

// Generate all AK combinations
static void generateAK(HandRange *range) {
    int suitA, suitK;
    StdDeck_CardMask hand;
    
    for (suitA = 0; suitA < 4; suitA++) {
        for (suitK = 0; suitK < 4; suitK++) {
            StdDeck_CardMask_RESET(hand);
            StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, suitA));
            StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, suitK));
            addHandToRange(range, hand);
        }
    }
}

// Calculate equity of range1 vs range2
static void calculateRangeVsRange(HandRange *range1, HandRange *range2, 
                          StdDeck_CardMask board, int nboard,
                          double *ev1, double *ev2) {
    enum_result_t result;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask dead;
    int i, j;
    int validMatchups = 0;
    double totalEV1 = 0.0, totalEV2 = 0.0;
    
    printf("Calculating %d vs %d hand combinations...\n", range1->count, range2->count);
    
    for (i = 0; i < range1->count; i++) {
        for (j = 0; j < range2->count; j++) {
            // Check for card conflicts
            if (StdDeck_CardMask_ANY_SET(range1->hands[i], range2->hands[j])) {
                continue; // Skip if hands share cards
            }
            
            if (StdDeck_CardMask_ANY_SET(range1->hands[i], board) ||
                StdDeck_CardMask_ANY_SET(range2->hands[j], board)) {
                continue; // Skip if hands conflict with board
            }
            
            // Setup the enumeration
            pockets[0] = range1->hands[i];
            pockets[1] = range2->hands[j];
            
            StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
            StdDeck_CardMask_OR(dead, dead, board);
            
            enumResultClear(&result);
            
            // Calculate equity for this specific matchup
            int err = enumExhaustive_dispatch(game_holdem, pockets, board, dead, 
                                              2, nboard, 0, &result);
            
            if (err == 0) {
                totalEV1 += result.ev[0];
                totalEV2 += result.ev[1];
                validMatchups++;
            }
            
            enumResultFree(&result);
        }
    }
    
    // Average the equities
    if (validMatchups > 0) {
        *ev1 = totalEV1 / validMatchups;
        *ev2 = totalEV2 / validMatchups;
    } else {
        *ev1 = 0.0;
        *ev2 = 0.0;
    }
    
    printf("Evaluated %d valid matchups\n", validMatchups);
}

// Print a card mask in readable format
static void printHand(StdDeck_CardMask hand) {
    int card;
    char rank, suit;
    int first = 1;
    
    for (card = 0; card < 52; card++) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, card)) {
            if (!first) printf(" ");
            first = 0;
            
            int r = StdDeck_RANK(card);
            int s = StdDeck_SUIT(card);
            
            rank = StdDeck_rankChars[r];
            suit = StdDeck_suitChars[s];
            printf("%c%c", rank, suit);
        }
    }
}

int main(int argc, char *argv[]) {
    HandRange pairsRange, akRange;
    StdDeck_CardMask board;
    double ev1, ev2;
    
    printf("==============================================\n");
    printf("RANGE EQUITY CALCULATOR USING POKENUM\n");
    printf("==============================================\n\n");
    
    // Example 1: High pairs (AA-QQ) vs AK
    printf("Example 1: AA-QQ vs AK\n");
    printf("----------------------\n");
    
    initRange(&pairsRange);
    initRange(&akRange);
    
    // Generate AA, KK, QQ
    generatePocketPairs(&pairsRange, StdDeck_Rank_QUEEN, StdDeck_Rank_ACE);
    generateAK(&akRange);
    
    printf("Pairs range: %d combinations (AA-QQ)\n", pairsRange.count);
    printf("AK range: %d combinations\n", akRange.count);
    
    StdDeck_CardMask_RESET(board);
    calculateRangeVsRange(&pairsRange, &akRange, board, 5, &ev1, &ev2);
    
    printf("\nResults:\n");
    printf("AA-QQ equity: %.3f\n", ev1);
    printf("AK equity: %.3f\n\n", ev2);
    
    // Example 2: With a flop
    printf("Example 2: AA-KK vs QQ-JJ on flop 9s 8s 2h\n");
    printf("-------------------------------------------\n");
    
    HandRange highPairs, midPairs;
    initRange(&highPairs);
    initRange(&midPairs);
    
    // AA-KK
    generatePocketPairs(&highPairs, StdDeck_Rank_KING, StdDeck_Rank_ACE);
    // QQ-JJ
    generatePocketPairs(&midPairs, StdDeck_Rank_JACK, StdDeck_Rank_QUEEN);
    
    // Set up board: 9s 8s 2h
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));
    
    printf("Board: ");
    printHand(board);
    printf("\n");
    
    printf("High pairs range: %d combinations (AA-KK)\n", highPairs.count);
    printf("Mid pairs range: %d combinations (QQ-JJ)\n", midPairs.count);
    
    calculateRangeVsRange(&highPairs, &midPairs, board, 2, &ev1, &ev2);
    
    printf("\nResults:\n");
    printf("AA-KK equity: %.3f\n", ev1);
    printf("QQ-JJ equity: %.3f\n\n", ev2);
    
    // Example 3: Single hand vs range
    printf("Example 3: AsAh vs all Kings\n");
    printf("----------------------------\n");
    
    HandRange asah, kings;
    initRange(&asah);
    initRange(&kings);
    
    // Add AsAh
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    addHandToRange(&asah, hand);
    
    // Generate all KK
    generatePocketPairs(&kings, StdDeck_Rank_KING, StdDeck_Rank_KING);
    
    printf("AsAh: 1 combination\n");
    printf("KK range: %d combinations\n", kings.count);
    
    StdDeck_CardMask_RESET(board);
    calculateRangeVsRange(&asah, &kings, board, 5, &ev1, &ev2);
    
    printf("\nResults:\n");
    printf("AsAh equity: %.3f\n", ev1);
    printf("KK equity: %.3f\n\n", ev2);
    
    // Cleanup
    freeRange(&pairsRange);
    freeRange(&akRange);
    freeRange(&highPairs);
    freeRange(&midPairs);
    freeRange(&asah);
    freeRange(&kings);
    
    printf("==============================================\n");
    printf("SUMMARY\n");
    printf("==============================================\n");
    printf("This demonstrates how to calculate range equities\n");
    printf("in pure C by enumerating all hand combinations.\n");
    printf("For more complex ranges (like 'top 20%%'), you would\n");
    printf("need to implement hand ranking and selection logic.\n");
    
    return 0;
}
