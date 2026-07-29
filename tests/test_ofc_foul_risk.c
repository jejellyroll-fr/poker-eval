/*
 * test_foul_risk.c - Test OFC foul risk calculator
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <poker_eval/ofc/ofc.h>
#include <poker_eval/deck/deck_std.h>

int main(void)
{
    ofc_hand_t hand;
    float risk;

    printf("=== OFC Foul Risk Calculator Test ===\n\n");

    /* Test 1: Empty hand - should have very low risk */
    OFC_InitializeHand(&hand);

    risk = OFC_CalculateFoulRisk(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
    printf("Test 1 - Empty hand, placing Ace in top: %.1f%% foul risk\n", risk * 100);

    risk = OFC_CalculateFoulRisk(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS), OFC_BOTTOM);
    printf("Test 1 - Empty hand, placing 2 in bottom: %.1f%% foul risk\n", risk * 100);

    /* Test 2: Risky scenario - high cards in top */
    OFC_InitializeHand(&hand);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_TOP);

    risk = OFC_CalculateFoulRisk(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS), OFC_TOP);
    printf("Test 2 - AK in top, placing Queen: %.1f%% foul risk\n", risk * 100);

    /* Test 3: Very risky scenario - pair in top */
    OFC_InitializeHand(&hand);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_TOP);

    risk = OFC_CalculateFoulRisk(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS), OFC_TOP);
    printf("Test 3 - KK in top, placing Queen: %.1f%% foul risk\n", risk * 100);

    /* Test 4: Safe placement in bottom */
    OFC_InitializeHand(&hand);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS), OFC_MIDDLE);

    risk = OFC_CalculateFoulRisk(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS), OFC_BOTTOM);
    printf("Test 4 - Low cards in top/middle, placing Ace in bottom: %.1f%% foul risk\n", risk * 100);

    printf("\n✅ Foul risk calculator tests completed!\n");
    return 0;
}
