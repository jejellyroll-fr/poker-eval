#include <stdio.h>
#include <string.h>
#include <poker_eval/distributions/plo_integration.h>
#include <poker_eval/core/enumdefs.h>

static void print_equity_results(const char* patterns[], int num_players, enum_result_t* results) {
    printf("\n=== PLO Range Equity Results ===\n");
    for (int i = 0; i < num_players; i++) {
        printf("Player %d (%s):\n", i + 1, patterns[i]);
        printf("  Equity:  %.2f%%\n", results->ev[i] * 100.0);
    }
    printf("Total samples: %d\n", results->nsamples);
}

int main(void) {
    printf("=== PLO Range Equity Calculator Example ===\n\n");
    
    // Example 1: AAxx vs KKxx preflop
    printf("Example 1: AAxx vs KKxx preflop\n");
    {
        const char* patterns[] = {"AAxxds", "KKxxds"};
        enum_result_t results;
        StdDeck_CardMask board, dead;
        StdDeck_CardMask_RESET(board);
        StdDeck_CardMask_RESET(dead);
        
        int matchups = PLO_CalculateRangeEquity(
            patterns, 2, board, dead, 5, // 5 board cards to deal
            true, 1000, // Use Monte Carlo with 1k iterations
            &results
        );
        
        if (matchups > 0) {
            print_equity_results(patterns, 2, &results);
            printf("Valid matchups evaluated: %d\n", matchups);
        } else {
            printf("Error calculating equity\n");
        }
    }
    
    // Example 2: Specific hand vs range
    printf("\n\nExample 2: AsAdKsKd vs AAxxds\n");
    {
        const char* patterns[] = {"AsAdKsKd", "AAxxds"};
        enum_result_t results;
        StdDeck_CardMask board, dead;
        StdDeck_CardMask_RESET(board);
        StdDeck_CardMask_RESET(dead);
        
        int matchups = PLO_CalculateRangeEquity(
            patterns, 2, board, dead, 5,
            true, 1000,
            &results
        );
        
        if (matchups > 0) {
            print_equity_results(patterns, 2, &results);
            printf("Valid matchups evaluated: %d\n", matchups);
        }
    }
    
    // Example 3: Three-way pot with flop
    printf("\n\nExample 3: Three-way on flop AsKhTd\n");
    {
        const char* patterns[] = {"QQxxds", "QQxxss", "QJ98ds"};
        enum_result_t results;
        StdDeck_CardMask board, dead;
        StdDeck_CardMask_RESET(board);
        StdDeck_CardMask_RESET(dead);
        
        // Set flop: As Kh Td
        StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(12, 3)); // As
        StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(11, 2)); // Kh
        StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(8, 1));  // Td
        
        int matchups = PLO_CalculateRangeEquity(
            patterns, 3, board, dead, 2, // 2 more board cards
            true, 1000,
            &results
        );
        
        if (matchups > 0) {
            print_equity_results(patterns, 3, &results);
            printf("Valid matchups evaluated: %d\n", matchups);
        }
    }
    
    // Example 4: Range descriptions
    printf("\n\nExample 4: Range Descriptions\n");
    {
        const char* test_patterns[] = {
            "AAxxds", "AAxxss", "AAxxr",
            "AKQJds", "JT98r", "KKxxds",
            "AsAdKsKd", "xxxxds"
        };
        
        for (int i = 0; i < 8; i++) {
            char desc[256];
            PLO_GetRangeDescription(test_patterns[i], desc, sizeof(desc));
            printf("  %s\n", desc);
        }
    }
    
    // Example 5: Filter hands by category
    printf("\n\nExample 5: Filter AA hands by suitedness\n");
    {
        OmahaHandList allAAHands, filteredHands;
        OmahaHandList_Init(&allAAHands, 100);
        OmahaHandList_Init(&filteredHands, 100);
        
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);
        
        // Generate all AAxx hands
        PLO_GenerateHands("AAxxr", dead, &allAAHands);
        int rainbow_count = allAAHands.count;
        
        OmahaHandList_Clear(&allAAHands);
        PLO_GenerateHands("AAxxss", dead, &allAAHands);
        int single_suited_count = allAAHands.count;
        
        OmahaHandList_Clear(&allAAHands);
        PLO_GenerateHands("AAxxds", dead, &allAAHands);
        int double_suited_count = allAAHands.count;
        
        printf("  AAxx rainbow: %d combos\n", rainbow_count);
        printf("  AAxx single-suited: %d combos\n", single_suited_count);
        printf("  AAxx double-suited: %d combos\n", double_suited_count);
        printf("  Total AAxx: %d combos\n", 
               rainbow_count + single_suited_count + double_suited_count);
        
        OmahaHandList_Free(&allAAHands);
        OmahaHandList_Free(&filteredHands);
    }
    
    return 0;
}
