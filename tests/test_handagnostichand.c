#include <stdio.h>
#include <stdlib.h>
#include "HoldemAgnosticHand.h"

void print_hands(const HandList* handList) {
    for (int i = 0; i < handList->count; ++i) {
        char handStr[60];  // Adjust size as needed
        StdDeck_maskString(handList->hands[i], handStr);
        printf("Hand %d: %s\n", i+1, handStr);
    }
}

int main() {
    const char* handText = "AKs";
    const char* deadCards = "AsKs";
    HandList handList = {0};
    
    int result = HoldemAgnosticHand_Instantiate(handText, deadCards, &handList);
    if (result > 0) {
        printf("Successfully parsed %d specific hands.\n", result);
        print_hands(&handList);
    } else {
        printf("Failed to parse hand.\n");
    }

    return 0;
}
