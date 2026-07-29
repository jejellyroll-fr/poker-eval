/*
 * test_debug_ofc.c - Debug OFC scoring
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <poker_eval/ofc/ofc.h>
#include <poker_eval/deck/deck_std.h>

int main(void)
{
       ofc_hand_t hand1, hand2;
       ofc_score_t score1, score2;
       ofc_royalties_t royalties1, royalties2;

       printf("=== OFC Debug ===\n");

       OFC_InitializeHand(&hand1);
       OFC_InitializeHand(&hand2);

       /* Simple hands for debugging */
       /* Hand 1: Pair of aces in top */
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS), OFC_TOP);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

       /* Fill middle and bottom with low cards */
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES), OFC_MIDDLE);

       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_CLUBS), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS), OFC_BOTTOM);

       /* Hand 2: Lower cards */
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS), OFC_TOP);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS), OFC_TOP);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS), OFC_TOP);

       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS), OFC_MIDDLE);

       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS), OFC_BOTTOM);

       /* Calculate royalties separately */
       OFC_CalculateRoyalties(&hand1, &royalties1);
       OFC_CalculateRoyalties(&hand2, &royalties2);

       printf("Hand1 royalties: Top=%d, Middle=%d, Bottom=%d\n",
              royalties1.top_royalties, royalties1.middle_royalties, royalties1.bottom_royalties);
       printf("Hand2 royalties: Top=%d, Middle=%d, Bottom=%d\n",
              royalties2.top_royalties, royalties2.middle_royalties, royalties2.bottom_royalties);

       /* Score the hands */
       int result = OFC_ScoreHands(&hand1, &hand2, &score1, &score2);
       printf("Score result: %d\n", result);

       printf("Score1: points=%d/%d/%d, scoop=%d, foul=%d, total=%d\n",
              score1.points[0], score1.points[1], score1.points[2],
              score1.scoop_bonus, score1.foul_penalty, score1.total_score);
       printf("Score2: points=%d/%d/%d, scoop=%d, foul=%d, total=%d\n",
              score2.points[0], score2.points[1], score2.points[2],
              score2.scoop_bonus, score2.foul_penalty, score2.total_score);

       return 0;
}
