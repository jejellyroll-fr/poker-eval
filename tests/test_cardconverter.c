#include <stdio.h>
#include "CardConverter.h"

int main() {
    InitPokerEvalCards();  // Make sure this is called

    const char* handText = "AcKcQcJcTc";
    StdDeck_CardMask handMask = TextToPokerEval(handText);
    printf("Hand mask for %s is %llu\n", handText, handMask.cards_n);

    StdDeck_CardMask array[5];
    int numCards = TextToPokerEvalArray(handText, array);
    printf("Number of cards in array: %d\n", numCards);
    for (int i = 0; i < numCards; i++) {
        printf("Card %d mask: %llu\n", i, array[i].cards_n);
    }


    // debug to check mask
    printf("Individual card masks:\n");
    printf("Ac: %llu\n", TextToPokerEval("Ac").cards_n);
    printf("Kc: %llu\n", TextToPokerEval("Kc").cards_n);
    printf("Qc: %llu\n", TextToPokerEval("Qc").cards_n);
    printf("Jc: %llu\n", TextToPokerEval("Jc").cards_n);
    printf("Tc: %llu\n", TextToPokerEval("Tc").cards_n);


    for (int i = 1; i <= 52; i++) {
        StdDeck_CardMask trackerMask = PokerTrackerToPokerEval(i);
        printf("PokerTracker card %d mask: %llu\n", i, trackerMask.cards_n);
    }

    return 0;
}