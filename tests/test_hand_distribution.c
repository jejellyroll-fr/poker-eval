#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/distributions/HoldemAgnosticHand.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/deck/deck_std.h>

static void print_holdem_hands(const HandList* handList, const char* description) {
    printf("\n=== %s ===\n", description);
    printf("Total hands: %d\n", handList->count);
    
    // Print first 10 hands as examples
    int max_display = (handList->count < 10) ? handList->count : 10;
    for (int i = 0; i < max_display; ++i) {
        char handStr[60];
        StdDeck_maskString(handList->hands[i], handStr);
        printf("Hand %d: %s\n", i+1, handStr);
    }
    if (handList->count > 10) {
        printf("... and %d more hands\n", handList->count - 10);
    }
}

static void print_omaha_hands(const OmahaHandList* handList, const char* description) {
    printf("\n=== %s ===\n", description);
    printf("Total hands: %d\n", handList->count);
    
    // Print first 10 hands as examples
    int max_display = (handList->count < 10) ? handList->count : 10;
    for (int i = 0; i < max_display; ++i) {
        char handStr[60];
        StdDeck_maskString(handList->hands[i], handStr);
        printf("Hand %d: %s\n", i+1, handStr);
    }
    if (handList->count > 10) {
        printf("... and %d more hands\n", handList->count - 10);
    }
}

static void test_holdem_distributions(void) {
    printf("\n========== TESTING HOLDEM HAND DISTRIBUTIONS ==========\n");
    
    HandList handList;
    
    // Test 1: Specific suited hand
    memset(&handList, 0, sizeof(HandList));
    (void)HoldemAgnosticHand_Instantiate("AKs", "", &handList);
    print_holdem_hands(&handList, "Test 1: AKs (suited AK)");
    printf("Expected: 4 hands (AhKh, AdKd, AcKc, AsKs)\n");
    
    // Test 2: Specific offsuit hand
    memset(&handList, 0, sizeof(HandList));
    (void)HoldemAgnosticHand_Instantiate("AKo", "", &handList);
    print_holdem_hands(&handList, "Test 2: AKo (offsuit AK)");
    printf("Expected: 12 hands (all AK combinations except suited ones)\n");
    
    // Test 3: Any AK
    memset(&handList, 0, sizeof(HandList));
    (void)HoldemAgnosticHand_Instantiate("AK", "", &handList);
    print_holdem_hands(&handList, "Test 3: AK (any AK)");
    printf("Expected: 16 hands (all AK combinations)\n");
    
    // Test 4: Pocket pair
    memset(&handList, 0, sizeof(HandList));
    (void)HoldemAgnosticHand_Instantiate("AA", "", &handList);
    print_holdem_hands(&handList, "Test 4: AA (pocket aces)");
    printf("Expected: 6 hands (all AA combinations)\n");
    
    // Test 5: With dead cards
    memset(&handList, 0, sizeof(HandList));
    (void)HoldemAgnosticHand_Instantiate("AK", "AsKh", &handList);
    print_holdem_hands(&handList, "Test 5: AK with AsKh dead");
    printf("Expected: 9 hands (16 - 7 blocked combinations)\n");
}

static void test_omaha_distributions(void) {
    printf("\n\n========== TESTING OMAHA HAND DISTRIBUTIONS ==========\n");
    
    OmahaHandQuery query;
    OmahaHandList handList = {0};
    StdDeck_CardMask deadCards;
    StdDeck_CardMask_RESET(deadCards);
    
    // Test 1: AAxx (pair of aces with any two cards)
    OmahaHandList_Free(&handList);
    if (OmahaHand_Parse("AAxx", &query)) {
        (void)OmahaHand_Instantiate(&query, deadCards, &handList);
        print_omaha_hands(&handList, "Test 1: AAxx (pair of aces + any 2 cards)");
        printf("Expected: Many hands (C(4,2) * C(50,2) = 6 * 1225 = 7350 minus conflicts)\n");
    } else {
        printf("Failed to parse AAxx\n");
    }
    
    // Test 2: AAxx double-suited
    OmahaHandList_Free(&handList);
    if (OmahaHand_Parse("AAxxds", &query)) {
        (void)OmahaHand_Instantiate(&query, deadCards, &handList);
        print_omaha_hands(&handList, "Test 2: AAxxds (double-suited aces)");
        printf("Expected: Fewer hands (only double-suited combinations)\n");
    } else {
        printf("Failed to parse AAxxds\n");
    }
    
    // Test 3: Specific hand AsKhQdJc
    OmahaHandList_Free(&handList);
    if (OmahaHand_Parse("AsKhQdJc", &query)) {
        (void)OmahaHand_Instantiate(&query, deadCards, &handList);
        print_omaha_hands(&handList, "Test 3: AsKhQdJc (specific hand)");
        printf("Expected: 1 hand\n");
    } else {
        printf("Failed to parse AsKhQdJc\n");
    }
    
    // Test 4: AKQJds (double-suited broadway)
    OmahaHandList_Free(&handList);
    if (OmahaHand_Parse("AKQJds", &query)) {
        (void)OmahaHand_Instantiate(&query, deadCards, &handList);
        print_omaha_hands(&handList, "Test 4: AKQJds (double-suited broadway)");
        printf("Expected: Multiple double-suited combinations\n");
    } else {
        printf("Failed to parse AKQJds\n");
    }
    
    // Test 5: AAAx (trip aces)
    OmahaHandList_Free(&handList);
    if (OmahaHand_Parse("AAAx", &query)) {
        (void)OmahaHand_Instantiate(&query, deadCards, &handList);
        print_omaha_hands(&handList, "Test 5: AAAx (trip aces + any card)");
        printf("Expected: C(4,3) * 48 = 4 * 48 = 192 hands\n");
    } else {
        printf("Failed to parse AAAx\n");
    }

    OmahaHandList_Free(&handList);
}

int main(void) {
    printf("=== POKER HAND DISTRIBUTION TEST ===\n");
    printf("Testing hand distribution functionality for Holdem and Omaha\n");
    
    test_holdem_distributions();
    test_omaha_distributions();
    
    printf("\n\n=== TEST COMPLETED ===\n");
    return 0;
}
