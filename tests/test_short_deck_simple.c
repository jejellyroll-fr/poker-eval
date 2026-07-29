/*
 * Test simple pour vérifier les règles du Short Deck Hold'em
 */

#include <stdio.h>
#include <assert.h>

// Inclusions directes sans chemins relatifs
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/games/rules_short.h>

// Déclaration de la fonction d'évaluation (définie inline)
extern HandVal ShortDeck_ShortRules_EVAL_N(ShortDeck_CardMask cards, int n_cards);

// Helper function to create a hand
static void create_hand(ShortDeck_CardMask *hand, int rank1, int suit1,
                        int rank2, int suit2, int rank3, int suit3,
                        int rank4, int suit4, int rank5, int suit5)
{
    ShortDeck_CardMask_RESET(*hand);
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank1, suit1));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank2, suit2));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank3, suit3));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank4, suit4));
    ShortDeck_CardMask_SET(*hand, ShortDeck_MAKE_CARD(rank5, suit5));
}

int main(void)
{
    printf("=== Test des Règles Short Deck Hold'em ===\n\n");

    // Test 1: Vérifier les constantes des types de mains
    printf("Test 1: Ordre des constantes... ");
    printf("(FLUSH=%d, FULLHOUSE=%d) ",
           ShortRules_HandType_FLUSH, ShortRules_HandType_FULLHOUSE);

    if (ShortRules_HandType_FLUSH > ShortRules_HandType_FULLHOUSE)
    {
        printf("✓ PASSED (Flush constant > Full House constant)\n");
    }
    else
    {
        printf("✗ FAILED (Constants: Flush=%d, FullHouse=%d)\n",
               ShortRules_HandType_FLUSH, ShortRules_HandType_FULLHOUSE);
    }

    // Test 2: Vérifier la définition du wheel straight
    printf("Test 2: Wheel straight mask... ");
    uint32 expected_wheel = ((1 << ShortDeck_Rank_ACE) |
                             (1 << ShortDeck_Rank_6) |
                             (1 << ShortDeck_Rank_7) |
                             (1 << ShortDeck_Rank_8) |
                             (1 << ShortDeck_Rank_9));

    if (ShortRules_9_STRAIGHT == expected_wheel)
    {
        printf("✓ PASSED (A-6-7-8-9 mask correct)\n");
    }
    else
    {
        printf("✗ FAILED (Expected: %u, Got: %u)\n", expected_wheel, ShortRules_9_STRAIGHT);
    }

    // Test 3: Vérifier les rangs Short Deck
    printf("Test 3: Short Deck ranks... ");
    if (ShortDeck_Rank_6 < ShortDeck_Rank_7 &&
        ShortDeck_Rank_7 < ShortDeck_Rank_8 &&
        ShortDeck_Rank_8 < ShortDeck_Rank_9 &&
        ShortDeck_Rank_9 < ShortDeck_Rank_TEN)
    {
        printf("✓ PASSED (Rank order correct)\n");
    }
    else
    {
        printf("✗ FAILED (Rank order incorrect)\n");
    }

    // Test 4: Nombre de cartes
    printf("Test 4: Deck size... ");
    if (ShortDeck_N_CARDS == 36)
    {
        printf("✓ PASSED (36 cards)\n");
    }
    else
    {
        printf("✗ FAILED (Expected 36, got %d)\n", ShortDeck_N_CARDS);
    }

    // Test 5: Nombre de rangs
    printf("Test 5: Rank count... ");
    if (ShortDeck_Rank_COUNT == 9)
    {
        printf("✓ PASSED (9 ranks: 6-A)\n");
    }
    else
    {
        printf("✗ FAILED (Expected 9, got %d)\n", ShortDeck_Rank_COUNT);
    }

    printf("\n=== Analyse de l'Implémentation ===\n");
    printf("Constantes Short Deck:\n");
    printf("- NOPAIR:    %d\n", ShortRules_HandType_NOPAIR);
    printf("- ONEPAIR:   %d\n", ShortRules_HandType_ONEPAIR);
    printf("- TWOPAIR:   %d\n", ShortRules_HandType_TWOPAIR);
    printf("- STRAIGHT:  %d\n", ShortRules_HandType_STRAIGHT);
    printf("- TRIPS:     %d\n", ShortRules_HandType_TRIPS);
    printf("- FULLHOUSE: %d\n", ShortRules_HandType_FULLHOUSE);
    printf("- FLUSH:     %d\n", ShortRules_HandType_FLUSH);
    printf("- QUADS:     %d\n", ShortRules_HandType_QUADS);
    printf("- STFLUSH:   %d\n", ShortRules_HandType_STFLUSH);

    printf("\nRangs Short Deck:\n");
    printf("- Rank_6:   %d\n", ShortDeck_Rank_6);
    printf("- Rank_7:   %d\n", ShortDeck_Rank_7);
    printf("- Rank_8:   %d\n", ShortDeck_Rank_8);
    printf("- Rank_9:   %d\n", ShortDeck_Rank_9);
    printf("- Rank_TEN: %d\n", ShortDeck_Rank_TEN);
    printf("- Rank_ACE: %d\n", ShortDeck_Rank_ACE);

    printf("\nWheel Straight Mask: 0x%X\n", ShortRules_9_STRAIGHT);

    printf("\n=== Conclusion ===\n");

    // Vérification des règles principales
    int rules_ok = 1;

    // Dans l'implémentation actuelle, FLUSH=6 et FULLHOUSE=5
    // Cela signifie que numériquement, FLUSH > FULLHOUSE
    // L'évaluateur doit gérer la logique pour que Flush batte Full House
    if (ShortRules_HandType_FLUSH > ShortRules_HandType_FULLHOUSE)
    {
        printf("✓ Constantes: Flush (%d) > Full House (%d)\n",
               ShortRules_HandType_FLUSH, ShortRules_HandType_FULLHOUSE);
    }
    else
    {
        printf("✗ Problème: Flush (%d) <= Full House (%d)\n",
               ShortRules_HandType_FLUSH, ShortRules_HandType_FULLHOUSE);
        rules_ok = 0;
    }

    if (ShortRules_9_STRAIGHT == expected_wheel)
    {
        printf("✓ A-6-7-8-9 straight correctement défini\n");
    }
    else
    {
        printf("✗ Problème avec la définition du wheel straight\n");
        rules_ok = 0;
    }

    if (ShortDeck_N_CARDS == 36 && ShortDeck_Rank_COUNT == 9)
    {
        printf("✓ Deck Short correctement configuré (36 cartes, 9 rangs)\n");
    }
    else
    {
        printf("✗ Problème avec la configuration du deck\n");
        rules_ok = 0;
    }

    if (rules_ok)
    {
        printf("\n🎉 IMPLÉMENTATION SHORT DECK: STRUCTURE CORRECTE!\n");
        printf("Les constantes et définitions sont conformes aux règles Short Deck.\n");
        printf("L'évaluateur doit gérer la logique pour que Flush batte Full House.\n");
        return 0;
    }
    else
    {
        printf("\n❌ PROBLÈMES DÉTECTÉS DANS L'IMPLÉMENTATION\n");
        return 1;
    }
}
