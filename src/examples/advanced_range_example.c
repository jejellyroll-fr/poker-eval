/*
 * Example demonstrating the Advanced Range Parser API
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/deck/deck_std.h>

static void print_hand(StdDeck_CardMask hand) {
    int cards[2];
    int count = 0;

    /* Extract cards from mask */
    for (int i = 0; i < StdDeck_N_CARDS && count < 2; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, i)) {
            cards[count++] = i;
        }
    }

    if (count == 2) {
        /* Convert to readable format */
        char rank1 = "23456789TJQKA"[StdDeck_RANK(cards[0])];
        char suit1 = "cdhs"[StdDeck_SUIT(cards[0])];
        char rank2 = "23456789TJQKA"[StdDeck_RANK(cards[1])];
        char suit2 = "cdhs"[StdDeck_SUIT(cards[1])];

        printf("%c%c%c%c", rank1, suit1, rank2, suit2);
    } else {
        printf("????");
    }
}

static void test_range_parsing(const char* range_string) {
    printf("\n=== Testing Range: \"%s\" ===\n", range_string);

    /* Validate first */
    char error_buffer[256];
    if (!ARP_ValidateRangeString(range_string, error_buffer, sizeof(error_buffer))) {
        printf("❌ Validation failed: %s\n", error_buffer);
        return;
    }
    printf("✅ Validation passed\n");

    /* Parse the range */
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;
    if (!ARP_ParseRange(range_string, dead_cards, game_holdem, &range)) {
        printf("❌ Parsing failed\n");
        return;
    }
    
    /* Display results */
    char description[256];
    ARP_RangeToString(&range, description, sizeof(description));
    printf("📊 Range: %s\n", description);
    
    float percentage = ARP_GetRangePercentage(&range, game_holdem);
    printf("📈 Percentage: %.2f%%\n", percentage * 100.0f);
    
    /* Show first few hands */
    printf("🃏 Sample hands: ");
    int max_show = (range.count < 10) ? (int)range.count : 10;
    for (int i = 0; i < max_show; i++) {
        if (i > 0) printf(", ");
        print_hand(range.hands[i]);
    }
    if (range.count > (size_t)max_show) {
        printf(", ... (%zu more)", range.count - (size_t)max_show);
    }
    printf("\n");
    
    /* Convert to PlayerRange for equity calculations */
    PlayerRange player_range;
    if (ARP_ToPlayerRange(&range, &player_range)) {
        printf("✅ Successfully converted to PlayerRange (%d hands)\n", player_range.count);
    }
    
    ARP_FreeRange(&range);
}

int main(void) {
    printf("🎯 Advanced Range Parser Test Suite\n");
    printf("====================================\n");
    
    /* Test basic hands */
    test_range_parsing("AA");
    test_range_parsing("AKs");
    test_range_parsing("AKo");
    test_range_parsing("AK");
    
    /* Test ranges */
    test_range_parsing("AA-TT");
    test_range_parsing("22-77");
    
    /* Test combinations */
    test_range_parsing("AA, KK, QQ");
    test_range_parsing("AKs, AQs, AJs");
    test_range_parsing("AA-TT, AKs");
    
    /* Test specific hands */
    test_range_parsing("AsKh");
    test_range_parsing("AsKh, AdQd");
    
    /* Test invalid ranges */
    printf("\n=== Testing Invalid Ranges ===\n");
    test_range_parsing("XX");      /* Invalid rank */
    test_range_parsing("A");       /* Incomplete */
    test_range_parsing("AK-");     /* Incomplete range */
    
    /* Test percentage (should fail for now) */
    printf("\n=== Testing Percentage Ranges (Not Yet Implemented) ===\n");
    test_range_parsing("20%");
    
    printf("\n🎉 Test suite completed!\n");
    return 0;
}
