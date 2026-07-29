/*
 * drawmaha_example.c - Exemple d'utilisation du Drawmaha (split pot)
 * 
 * Démontre l'évaluation et l'énumération pour le jeu Drawmaha,
 * un jeu split pot combinant Omaha et 5-Card Draw.
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/games/rules_drawmaha.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>

int main(void)
{
    printf("Test Drawmaha Split Pot (Version Correcte)\n");
    printf("=========================================\n\n");

    // Test 1: Évaluation de base split pot
    printf("Test 1: Évaluation split pot de base\n");
    StdDeck_CardMask hand, board;
    HandVal omaha_result;
    LowHandVal draw_result;
    int err;

    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_RESET(board);

    // Main: As Ah Kc Kd Qs (5 cartes pour Drawmaha)
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));

    // Board: Qh Jc Tc
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));

    err = Drawmaha_EvaluateHand(hand, board, &omaha_result, &draw_result);

    printf("Main: As Ah Kc Kd Qs (5 cartes)\n");
    printf("Board: Qh Jc Tc\n");
    printf("Erreur: %d\n", err);
    printf("Résultat Omaha (Hi): %u\n", omaha_result);
    printf("Résultat Draw (Lo): %u\n", draw_result);

    if (err == 0)
    {
        printf("✓ Évaluation split pot réussie\n");
    }
    else
    {
        printf("✗ Erreur dans l'évaluation\n");
        return 1;
    }
    printf("\n");

    // Exemple 2: Énumération Drawmaha split pot
    printf("Exemple 2: Énumération Drawmaha split pot\n");
    enum_game_t game = game_drawmaha;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask enum_board, dead;
    enum_result_t result;
    int npockets = 2;
    int nboard = 3;

    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(enum_board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);

    // Player 1: As Ah Kc Kd Qs (forte main Omaha et Draw)
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));

    // Player 2: 7c 6d 5s 4h 3c (main plus faible)
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));

    // Board: Qh Jc Tc
    StdDeck_CardMask_SET(enum_board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(enum_board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(enum_board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));

    // Dead cards = all pocket and board cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    StdDeck_CardMask_OR(dead, dead, enum_board);

    printf("Player 1: As Ah Kc Kd Qs (5 cartes - forte main)\n");
    printf("Player 2: 7c 6d 5s 4h 3c (5 cartes - main faible)\n");
    printf("Board: Qh Jc Tc\n");
    printf("Running Monte Carlo avec 1000 échantillons...\n\n");

    err = enumSample(game, pockets, enum_board, dead, npockets, nboard, 1000, 0, &result);

    if (err)
    {
        printf("ERROR: enumSample returned %d\n", err);
        return 1;
    }

    printf("Résultats Split Pot:\n");
    printf("Échantillons: %u\n", result.nsamples);
    printf("\nPlayer 1:\n");
    printf("  Hi Win: %d, Hi Tie: %d, Hi Lose: %d\n",
           result.nwinhi[0], result.ntiehi[0], result.nlosehi[0]);
    printf("  Lo Win: %d, Lo Tie: %d, Lo Lose: %d\n",
           result.nwinlo[0], result.ntielo[0], result.nloselo[0]);
    printf("  Scoops: %d\n", result.nscoop[0]);
    printf("  EV: %.4f\n", result.ev[0] / result.nsamples);

    printf("\nPlayer 2:\n");
    printf("  Hi Win: %d, Hi Tie: %d, Hi Lose: %d\n",
           result.nwinhi[1], result.ntiehi[1], result.nlosehi[1]);
    printf("  Lo Win: %d, Lo Tie: %d, Lo Lose: %d\n",
           result.nwinlo[1], result.ntielo[1], result.nloselo[1]);
    printf("  Scoops: %d\n", result.nscoop[1]);
    printf("  EV: %.4f\n", result.ev[1] / result.nsamples);

    // Vérifications
    if (result.ntiehi[0] > 0 || result.ntiehi[1] > 0)
    {
        printf("\n✓ TIES DÉTECTÉS dans la partie Hi!\n");
    }

    if (result.ntielo[0] > 0 || result.ntielo[1] > 0)
    {
        printf("✓ TIES DÉTECTÉS dans la partie Lo (Draw)!\n");
    }

    if (result.nscoop[0] > 0 || result.nscoop[1] > 0)
    {
        printf("✓ SCOOPS DÉTECTÉS (victoire des deux pots)!\n");
    }

    enumResultFree(&result);

    printf("\n✓ Test Drawmaha split pot terminé!\n");

    // Test bonus: Vérification d'un tie avec straight
    printf("\n================================================\n");
    printf("Test Bonus: Tie avec straight\n");
    printf("==============================\n\n");

    // Board fixe : 9♠ T♣ J♥ 2♦ 7♦
    StdDeck_CardMask straight_board;
    StdDeck_CardMask_RESET(straight_board);
    StdDeck_CardMask_SET(straight_board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(straight_board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(straight_board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(straight_board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(straight_board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS));

    // Joueur 1 : Q♠ K♠ + trois cartes quelconques
    StdDeck_CardMask straight_hand1;
    StdDeck_CardMask_RESET(straight_hand1);
    StdDeck_CardMask_SET(straight_hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(straight_hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(straight_hand1, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(straight_hand1, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(straight_hand1, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));

    // Joueur 2 : Q♦ K♦ + trois autres cartes
    StdDeck_CardMask straight_hand2;
    StdDeck_CardMask_RESET(straight_hand2);
    StdDeck_CardMask_SET(straight_hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(straight_hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(straight_hand2, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(straight_hand2, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(straight_hand2, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));

    HandVal straight_hi1, straight_hi2;
    LowHandVal straight_lo1, straight_lo2;

    int straight_err1 = Drawmaha_EvaluateHand(straight_hand1, straight_board, &straight_hi1, &straight_lo1);
    int straight_err2 = Drawmaha_EvaluateHand(straight_hand2, straight_board, &straight_hi2, &straight_lo2);

    printf("Board : 9♠ T♣ J♥ 2♦ 7♦\n");
    printf("Hand1 : K♠ Q♠ + xxx -> attend KQJT9 straight\n");
    printf("Hand2 : K♦ Q♦ + xxx -> attend KQJT9 straight\n\n");

    if (straight_err1 || straight_err2)
    {
        printf("Erreur d'évaluation (%d / %d)\n", straight_err1, straight_err2);
    }
    else
    {
        printf("HandVal 1 : %u\n", straight_hi1);
        printf("HandVal 2 : %u\n", straight_hi2);
        printf("Tie Hi ? %s\n", (straight_hi1 == straight_hi2) ? "OUI" : "NON");

        if (straight_hi1 == straight_hi2)
        {
            printf("✓ Tie correctement détecté pour la partie Hi (straight)\n");
        }
        else
        {
            printf("✗ Pas de tie alors qu'il devrait y en avoir\n");
        }
    }

    return 0;
}
