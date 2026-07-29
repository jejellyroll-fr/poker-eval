#include <stdio.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/core/poker_defs.h>

int main() {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    arp_range_t range;
    
    printf("Test: AA-KK\n");
    int result = ARP_ParseRange("AA-KK", dead_cards, game_holdem, &range);
    if (!result) {
        printf("PARSING FAILED!\n");
        return 1;
    }
    printf("Parse result: %d\n", result);
    printf("Range count: %zu (expected 12)\n", range.count);
    printf("Game type: %d\n", range.game_type);
    printf("Is percentage: %d\n", range.is_percentage);
    
    if (range.count != 12) {
        printf("WRONG COUNT!\n");
        return 1;
    }
    
    ARP_FreeRange(&range);
    printf("SUCCESS!\n");
    return 0;
}
