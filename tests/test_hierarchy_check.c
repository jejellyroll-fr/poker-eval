/*
 * test_hierarchy_check.c - Test OFC hierarchy validation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/deck/deck_std.h>

int main(void)
{
    ofc_hand_t hand1;

    printf("=== Test Hierarchy Check ===\n");

    OFC_InitializeHand(&hand1);

    /* Build the same hand as in comprehensive test */
    /* Top: Pair of Aces */
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS), OFC_TOP);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

    /* Middle: Full house (JJJ33) */
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS), OFC_MIDDLE);

    /* Bottom: Four of a kind (KKKK4) */
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES), OFC_BOTTOM);

    printf("Hand complete: %d\n", OFC_IsComplete(&hand1));
    printf("Hand valid hierarchy: %d\n", OFC_ValidateHierarchy(&hand1));

    /* Test hand values */
    HandVal top_val = OFC_EvaluateHand(hand1.hands[OFC_TOP], 3);
    HandVal middle_val = OFC_EvaluateHand(hand1.hands[OFC_MIDDLE], 5);
    HandVal bottom_val = OFC_EvaluateHand(hand1.hands[OFC_BOTTOM], 5);

    printf("Hand values: top=%u, middle=%u, bottom=%u\n", top_val, middle_val, bottom_val);
    printf("Top type: %d, Middle type: %d, Bottom type: %d\n",
           HandVal_HANDTYPE(top_val), HandVal_HANDTYPE(middle_val), HandVal_HANDTYPE(bottom_val));

    return 0;
}
