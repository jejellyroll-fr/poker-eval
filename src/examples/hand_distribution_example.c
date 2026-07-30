#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/distributions/HoldemAgnosticHand.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

// Example: Calculate equity for AA vs KK in Holdem
static void example_holdem_equity(void) {
    printf("\n=== HOLDEM EXAMPLE: AA vs KK ===\n");
    
    HandList aces, kings;
    memset(&aces, 0, sizeof(HandList));
    memset(&kings, 0, sizeof(HandList));
    
    // Parse AA and KK
    HoldemAgnosticHand_Instantiate("AA", "", &aces);
    HoldemAgnosticHand_Instantiate("KK", "", &kings);
    
    printf("AA has %d combinations\n", aces.count);
    printf("KK has %d combinations\n", kings.count);
    
    // Calculate average equity across all AA vs KK matchups
    double total_equity_aa = 0.0;
    int valid_matchups = 0;
    
    for (int i = 0; i < aces.count; i++) {
        for (int j = 0; j < kings.count; j++) {
            // Check for card conflicts
            if (StdDeck_CardMask_ANY_SET(aces.hands[i], kings.hands[j])) {
                continue; // Skip conflicting hands
            }
            
            // Set up the matchup
            StdDeck_CardMask pockets[2];
            pockets[0] = aces.hands[i];
            pockets[1] = kings.hands[j];
            
            StdDeck_CardMask board, dead;
            StdDeck_CardMask_RESET(board);
            StdDeck_CardMask_RESET(dead);
            
            // Calculate equity
            enum_result_t result;
            enumResultAlloc(&result, 2, enum_ordering_mode_hi);
            // npockets=2, nboard=0 (no board cards dealt yet), orderflag=0
            enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, 0, 0, &result);
            
            double denom = (result.nsamples > 0) ? (double)result.nsamples : 10000.0;
            total_equity_aa += (result.ev[0] / denom);
            valid_matchups++;
            
            enumResultFree(&result);
        }
    }
    
    if (valid_matchups > 0) {
        double avg_equity = total_equity_aa / valid_matchups;
        printf("\nAA vs KK equity: %.2f%% vs %.2f%%\n", 
               avg_equity * 100, (1 - avg_equity) * 100);
        printf("Based on %d valid matchups\n", valid_matchups);
    }
}

// Example: Calculate equity for AAxx vs KKxx in Omaha
static void example_omaha_equity(void) {
    printf("\n=== OMAHA EXAMPLE: AAxx vs KKxx ===\n");
    
    OmahaHandQuery query_aa, query_kk;
    OmahaHandList aces, kings;
    memset(&aces, 0, sizeof(OmahaHandList));
    memset(&kings, 0, sizeof(OmahaHandList));
    
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    
    // Parse AAxx and KKxx
    if (!OmahaHand_Parse("AAxx", &query_aa) || !OmahaHand_Parse("KKxx", &query_kk)) {
        printf("Failed to parse hands\n");
        return;
    }
    
    // Initialize with appropriate capacity
    OmahaHandList_Init(&aces, 5000);
    OmahaHandList_Init(&kings, 5000);
    
    OmahaHand_Instantiate(&query_aa, dead, &aces);
    OmahaHand_Instantiate(&query_kk, dead, &kings);
    
    printf("AAxx has %d combinations\n", aces.count);
    printf("KKxx has %d combinations\n", kings.count);
    
    // Sample a few matchups for demonstration
    int sample_size = 100; // Sample 100 random matchups
    double total_equity_aa = 0.0;
    int valid_samples = 0;
    
    for (int s = 0; s < sample_size && s < aces.count && s < kings.count; s++) {
        // Pick random indices
        int i = rand() % aces.count;
        int j = rand() % kings.count;
        
        // Check for conflicts
        if (StdDeck_CardMask_ANY_SET(aces.hands[i], kings.hands[j])) {
            continue;
        }
        
        StdDeck_CardMask pockets[2];
        pockets[0] = aces.hands[i];
        pockets[1] = kings.hands[j];
        
        StdDeck_CardMask board, dead_cards;
        StdDeck_CardMask_RESET(board);
        StdDeck_CardMask_RESET(dead_cards);
        
        // Use Monte Carlo for Omaha (faster than exhaustive)
        enum_result_t result;
        enumResultAlloc(&result, 2, enum_ordering_mode_hi);
        // npockets=2, nboard=0 (no board cards dealt yet), niter=10000, orderflag=0
        enumSample(game_omaha, pockets, board, dead_cards, 2, 0, 10000, 0, &result);
        
        // ev[0] is already the sum of pot fractions for all samples
        // We need to divide by the total samples to get the average equity
        double denom = (result.nsamples > 0) ? (double)result.nsamples : 10000.0;
        total_equity_aa += (result.ev[0] / denom);
        valid_samples++;
        
        enumResultFree(&result);
    }
    
    if (valid_samples > 0) {
        double avg_equity = total_equity_aa / valid_samples;
        printf("\nAAxx vs KKxx equity (sample): %.2f%% vs %.2f%%\n", 
               avg_equity * 100, (1 - avg_equity) * 100);
        printf("Based on %d sampled matchups\n", valid_samples);
    }
    
    // Clean up
    OmahaHandList_Free(&aces);
    OmahaHandList_Free(&kings);
}

// Example: Specific suited hands in Omaha
static void example_omaha_suited(void) {
    printf("\n=== OMAHA SUITED EXAMPLE: AAxxds vs random ===\n");
    
    OmahaHandQuery query_aa;
    OmahaHandList aces;
    memset(&aces, 0, sizeof(OmahaHandList));
    
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    
    // Parse double-suited aces
    if (!OmahaHand_Parse("AAxxds", &query_aa)) {
        printf("Failed to parse AAxxds\n");
        return;
    }
    
    // Initialize with appropriate capacity
    OmahaHandList_Init(&aces, 2000);
    
    OmahaHand_Instantiate(&query_aa, dead, &aces);
    printf("AAxxds has %d combinations\n", aces.count);
    
    // Show first 5 hands as examples
    printf("\nExample double-suited AA hands:\n");
    for (int i = 0; i < 5 && i < aces.count; i++) {
        char handStr[60];
        StdDeck_maskString(aces.hands[i], handStr);
        printf("  %d: %s\n", i+1, handStr);
    }
    
    // Clean up
    OmahaHandList_Free(&aces);
}

// Example: Large range demonstration
static void example_large_range(void) {
    printf("\n=== OMAHA LARGE RANGE EXAMPLE: xxxx (all possible 4-card hands) ===\n");
    
    OmahaHandQuery query;
    OmahaHandList allHands;
    memset(&allHands, 0, sizeof(OmahaHandList));
    
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    
    // Parse xxxx (all possible 4-card hands)
    if (!OmahaHand_Parse("xxxx", &query)) {
        printf("Failed to parse xxxx\n");
        return;
    }
    
    // Initialize with large capacity for all possible 4-card combinations
    // C(52,4) = 270,725
    printf("Initializing with capacity for large range...\n");
    OmahaHandList_Init(&allHands, 300000);
    
    printf("Generating all possible 4-card hands...\n");
    OmahaHand_Instantiate(&query, dead, &allHands);
    
    printf("Generated %d hands (C(52,4) = 270,725)\n", allHands.count);
    printf("This exceeds the old MAX_OMAHA_COMBOS limit of 10,000!\n");
    printf("Memory used: ~%zu KB\n", 
           (allHands.capacity * sizeof(StdDeck_CardMask)) / 1024);
    
    // Clean up
    OmahaHandList_Free(&allHands);
}

int main(void) {
    printf("=== HAND DISTRIBUTION EQUITY EXAMPLES ===\n");
    
    // Seed random number generator
    srand(42);
    
    // Run examples
    example_holdem_equity();
    example_omaha_equity();
    example_omaha_suited();
    example_large_range();
    
    printf("\n=== EXAMPLES COMPLETED ===\n");
    return 0;
}
