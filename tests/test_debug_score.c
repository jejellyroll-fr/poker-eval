/*
 * debug_score_simple.c - Debug simple pour isoler le problème de score OFC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/deck/deck_std.h>

int main(void)
{
       ofc_hand_t hand1, hand2;
       ofc_score_t score1, score2;

       printf("=== Debug Score Simple ===\n");

       // Initialiser les mains
       OFC_InitializeHand(&hand1);
       OFC_InitializeHand(&hand2);

       // Créer des mains valides avec hiérarchie correcte
       // Top: High card simple
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES), OFC_TOP);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS), OFC_TOP);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS), OFC_TOP);

       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS), OFC_TOP);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS), OFC_TOP);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS), OFC_TOP);

       // Middle: Pair (plus fort que top)
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_CLUBS), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES), OFC_MIDDLE);

       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES), OFC_MIDDLE);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS), OFC_MIDDLE);

       // Bottom: Two pair (plus fort que middle)
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_CLUBS), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
       OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS), OFC_BOTTOM);

       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
       OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES), OFC_BOTTOM);

       // Vérifier que les mains sont complètes et valides
       printf("Hand1 complete: %d\n", OFC_IsComplete(&hand1));
       printf("Hand2 complete: %d\n", OFC_IsComplete(&hand2));
       printf("Hand1 valid hierarchy: %d\n", OFC_ValidateHierarchy(&hand1));
       printf("Hand2 valid hierarchy: %d\n", OFC_ValidateHierarchy(&hand2));

       // Tester la comparaison de mains directement
       int top_result = OFC_CompareHands(hand1.hands[OFC_TOP], 3, hand2.hands[OFC_TOP], 3, OFC_TOP);
       int middle_result = OFC_CompareHands(hand1.hands[OFC_MIDDLE], 5, hand2.hands[OFC_MIDDLE], 5, OFC_MIDDLE);
       int bottom_result = OFC_CompareHands(hand1.hands[OFC_BOTTOM], 5, hand2.hands[OFC_BOTTOM], 5, OFC_BOTTOM);

       printf("Hand comparisons: top=%d, middle=%d, bottom=%d\n", top_result, middle_result, bottom_result);

       // Initialiser les scores explicitement
       memset(&score1, 0, sizeof(score1));
       memset(&score2, 0, sizeof(score2));

       printf("Scores avant scoring: score1.total=%d, score2.total=%d\n", score1.total_score, score2.total_score);

       // Appeler la fonction de scoring
       int result = OFC_ScoreHands(&hand1, &hand2, &score1, &score2);
       printf("OFC_ScoreHands result: %d\n", result);

       // Afficher les résultats détaillés
       printf("=== Résultats détaillés ===\n");
       printf("Score1 - points: [%d,%d,%d], scoop: %d, foul: %d, total: %d\n",
              score1.points[0], score1.points[1], score1.points[2],
              score1.scoop_bonus, score1.foul_penalty, score1.total_score);
       printf("Score2 - points: [%d,%d,%d], scoop: %d, foul: %d, total: %d\n",
              score2.points[0], score2.points[1], score2.points[2],
              score2.scoop_bonus, score2.foul_penalty, score2.total_score);

       printf("Score1 royalties: top=%d, middle=%d, bottom=%d\n",
              score1.royalties.top_royalties, score1.royalties.middle_royalties, score1.royalties.bottom_royalties);
       printf("Score2 royalties: top=%d, middle=%d, bottom=%d\n",
              score2.royalties.top_royalties, score2.royalties.middle_royalties, score2.royalties.bottom_royalties);

       return 0;
}
