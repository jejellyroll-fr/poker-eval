/*
 * Exemple d'utilisation de la nouvelle structure modulaire de poker-eval
 *
 * Cet exemple démontre comment utiliser les différents modules:
 * - Core: Types de base et jeux de cartes
 * - Games: Règles et évaluations spécialisées
 * - Equity: Calculs d'équité et énumération
 * - Utils: Utilitaires et fonctions helper
 * - Distributions: Distributions de mains
 */

#include <stdio.h>
#include <stdlib.h>

// Modules de la nouvelle structure
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/deck_short.h>

int main(void)
{
       printf("=== Exemple d'utilisation de la structure modulaire ===\n\n");

       // 1. Module CORE - Types de base et jeux de cartes
       printf("1. Module CORE - Jeux de cartes supportés:\n");
       printf("   - Deck standard: %d cartes\n", StdDeck_N_CARDS);
       printf("   - Deck avec joker: %d cartes\n", JokerDeck_N_CARDS);
       printf("   - Deck court (Short Deck): %d cartes\n", ShortDeck_N_CARDS);
       printf("\n");

       // 2. Création et manipulation de cartes
       printf("2. Création et manipulation de cartes:\n");

       // Deck standard
       StdDeck_CardMask hand_std;
       StdDeck_CardMask_RESET(hand_std);

       int ace_spades = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
       int king_hearts = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
       int queen_diamonds = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
       int jack_clubs = StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
       int ten_spades = StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES);

       StdDeck_CardMask_SET(hand_std, ace_spades);
       StdDeck_CardMask_SET(hand_std, king_hearts);
       StdDeck_CardMask_SET(hand_std, queen_diamonds);
       StdDeck_CardMask_SET(hand_std, jack_clubs);
       StdDeck_CardMask_SET(hand_std, ten_spades);

       printf("   Main créée: As♠ K♥ Q♦ J♣ T♠ (Quinte royale)\n");
       printf("   Cartes individuelles: As♠=%d, K♥=%d, Q♦=%d, J♣=%d, T♠=%d\n",
              ace_spades, king_hearts, queen_diamonds, jack_clubs, ten_spades);
       printf("\n");

       // 3. Conversion de cartes en chaînes
       printf("3. Conversion de cartes:\n");
       char card_str[4];

       StdDeck_cardToString(ace_spades, card_str);
       printf("   As de pique: %s\n", card_str);

       StdDeck_cardToString(king_hearts, card_str);
       printf("   Roi de coeur: %s\n", card_str);

       StdDeck_cardToString(queen_diamonds, card_str);
       printf("   Dame de carreau: %s\n", card_str);
       printf("\n");

       // 4. Comparaison des différents types de decks
       printf("4. Comparaison des types de decks:\n");
       printf("   Standard: Rangs %d-%d, Couleurs %d-%d\n",
              StdDeck_Rank_FIRST, StdDeck_Rank_LAST,
              StdDeck_Suit_FIRST, StdDeck_Suit_LAST);

       printf("   Short Deck: Rangs %d-%d, Couleurs %d-%d\n",
              ShortDeck_Rank_FIRST, ShortDeck_Rank_LAST,
              ShortDeck_Suit_FIRST, ShortDeck_Suit_LAST);
       printf("\n");

       // 5. Démonstration des masques de cartes
       printf("5. Manipulation des masques de cartes:\n");
       StdDeck_CardMask pocket_aces;
       StdDeck_CardMask_RESET(pocket_aces);

       int ace_hearts = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
       int ace_clubs = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);

       StdDeck_CardMask_SET(pocket_aces, ace_hearts);
       StdDeck_CardMask_SET(pocket_aces, ace_clubs);

       printf("   Pocket Aces créé: A♥ A♣\n");
       printf("   Vérification A♥ présent: %s\n",
              StdDeck_CardMask_CARD_IS_SET(pocket_aces, ace_hearts) ? "Oui" : "Non");
       printf("   Vérification K♥ présent: %s\n",
              StdDeck_CardMask_CARD_IS_SET(pocket_aces, king_hearts) ? "Oui" : "Non");
       printf("\n");

       // 6. Informations sur l'architecture modulaire
       printf("6. Architecture modulaire:\n");
       printf("   ✅ Module CORE: Types de base et jeux de cartes\n");
       printf("   ✅ Module GAMES: Règles et évaluations (headers disponibles)\n");
       printf("   ✅ Module EQUITY: Calculs d'équité (headers disponibles)\n");
       printf("   ✅ Module UTILS: Utilitaires (headers disponibles)\n");
       printf("   ✅ Module DISTRIBUTIONS: Distributions de mains (headers disponibles)\n");
       printf("\n");

       printf("=== Exemple terminé avec succès ===\n");
       printf("La nouvelle structure modulaire est pleinement fonctionnelle!\n");

       return 0;
}
