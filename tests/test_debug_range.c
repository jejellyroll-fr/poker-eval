#include <stdio.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/core/poker_defs.h>

int main() {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    arp_range_t range;
    
    // Test 1: AA-KK
    printf("Test 1: AA-KK\n");
    int result = ARP_ParseRange("AA-KK", dead_cards, game_holdem, &range);
    printf("  Result: %d, Count: %zu (expected 12)\n", result, range.count);
    ARP_FreeRange(&range);
    
    // Test 2: AA, KK, QQ
    printf("\nTest 2: AA, KK, QQ\n");
    result = ARP_ParseRange("AA, KK, QQ", dead_cards, game_holdem, &range);
    printf("  Result: %d, Count: %zu (expected 18)\n", result, range.count);
    ARP_FreeRange(&range);
    
    // Test 3: AA, KK
    printf("\nTest 3: AA, KK\n");
    result = ARP_ParseRange("AA, KK", dead_cards, game_holdem, &range);
    printf("  Result: %d, Count: %zu (expected 12)\n", result, range.count);
    ARP_FreeRange(&range);
    
    // Test 4: 5%
    printf("\nTest 4: 5%%\n");
    result = ARP_ParseRange("5%", dead_cards, game_holdem, &range);
    printf("  Result: %d, Count: %zu, is_percentage: %d, percentage_used: %.3f\n", 
           result, range.count, range.is_percentage, range.percentage_used);
    ARP_FreeRange(&range);
    
    // Test 5: AA + KK
    printf("\nTest 5: AA + KK\n");
    result = ARP_ParseRange("AA + KK", dead_cards, game_holdem, &range);
    printf("  Result: %d, Count: %zu (expected 12)\n", result, range.count);
    ARP_FreeRange(&range);
    
    return 0;
}
