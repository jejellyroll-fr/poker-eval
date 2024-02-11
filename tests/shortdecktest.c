#include <assert.h> // Pour assert()
#include <string.h> // Ajoutez cette ligne
#include "deck.h"
#include "deck_short.h"
#include "rules_short.h"
#include "inlines/eval_short.h"



void testCardToString() {
    char cardStr[3];
    int cardIndex = ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES);

    // Test direct de l'accès aux tableaux
    //printf("Direct Access: %c%c\n", ShortDeck_rankChars[12], ShortDeck_suitChars[3]);


    ShortDeck_cardToString(cardIndex, cardStr);
    //printf("Rank Index: %d, Suit Index: %d\n", ShortDeck_RANK(cardIndex), ShortDeck_SUIT(cardIndex));
    //printf("Expected: As, Actual: %s\n", cardStr);
    assert(strcmp(cardStr, "As") == 0);
}



void testStringToCard() {
    int cardIndex;
    int result = ShortDeck_stringToCard("As", &cardIndex);
    assert(result == 2); // Vérifier que deux caractères ont été traités
    assert(cardIndex == ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES)); // Vérifier que l'indice de carte correspond à As de Pique
}

void testMaskToCards() {
    ShortDeck_CardMask mask;
    ShortDeck_CardMask_RESET(mask);
    ShortDeck_CardMask_SET(mask, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES)); // Ajouter As de Pique
    ShortDeck_CardMask_SET(mask, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_HEARTS)); // Ajouter Roi de Coeur

    int cards[ShortDeck_N_CARDS];
    int n = ShortDeck_maskToCards(&mask, cards);

    assert(n == 2); // Vérifier que deux cartes sont retournées
    assert(cards[0] == ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES)); // Vérifier la première carte
    assert(cards[1] == ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_HEARTS)); // Vérifier la seconde carte
}

void testNumCards() {
    ShortDeck_CardMask mask;
    ShortDeck_CardMask_RESET(mask);
    ShortDeck_CardMask_SET(mask, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES)); // Ajouter As de Pique
    ShortDeck_CardMask_SET(mask, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_HEARTS)); // Ajouter Roi de Coeur

    int ncards = ShortDeck_NumCards(&mask);
    assert(ncards == 2); // Vérifier que le nombre de cartes est 2
}
void testFlushBeatsFullHouse() {
    ShortDeck_CardMask flushHand, fullHouseHand;
    HandVal flushVal, fullHouseVal;

    // Créer une main de Flush
    // Note : Adaptez la création des mains à votre implémentation
    ShortDeck_CardMask_RESET(flushHand);
    ShortDeck_CardMask_SET(flushHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(flushHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(flushHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_QUEEN, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(flushHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_JACK, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(flushHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_9, ShortDeck_Suit_SPADES));


    // Créer une main de Full House
    ShortDeck_CardMask_RESET(fullHouseHand);
    // Brelan d'As
    ShortDeck_CardMask_SET(fullHouseHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(fullHouseHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS));
    ShortDeck_CardMask_SET(fullHouseHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_DIAMONDS));

    // Paire de Rois
    ShortDeck_CardMask_SET(fullHouseHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS));
    ShortDeck_CardMask_SET(fullHouseHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_SPADES));


    // Évaluer les deux mains
    flushVal = ShortDeck_ShortRules_EVAL_N(flushHand, ShortDeck_numCards(flushHand));
    fullHouseVal = ShortDeck_ShortRules_EVAL_N(fullHouseHand, ShortDeck_numCards(fullHouseHand));

    // Assert que le Flush bat le Full House
    assert(flushVal > fullHouseVal);
}

void testTripsBeatsStraight() {
    ShortDeck_CardMask straightHand, tripsHand;
    HandVal straightVal, tripsVal;

    // Créer une main de Straight
    // Note : Adaptez la création des mains à votre implémentation
    ShortDeck_CardMask_RESET(straightHand);
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_6, ShortDeck_Suit_HEARTS));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_7, ShortDeck_Suit_DIAMONDS));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_8, ShortDeck_Suit_CLUBS));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_9, ShortDeck_Suit_SPADES));


    // Créer une main de Full House
    ShortDeck_CardMask_RESET(tripsHand);
    // Brelan d'As
    ShortDeck_CardMask_SET(tripsHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(tripsHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_HEARTS));
    ShortDeck_CardMask_SET(tripsHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_DIAMONDS));

    // ramdom
    ShortDeck_CardMask_SET(tripsHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_KING, ShortDeck_Suit_CLUBS));
    ShortDeck_CardMask_SET(tripsHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_QUEEN, ShortDeck_Suit_SPADES));


    // Évaluer les deux mains
    straightVal = ShortDeck_ShortRules_EVAL_N(straightHand, ShortDeck_numCards(straightHand));
    tripsVal = ShortDeck_ShortRules_EVAL_N(tripsHand, ShortDeck_numCards(tripsHand));

    // Assert que le Flush bat le Full House
    assert(tripsVal > straightVal);
}

void testStraightIncludingAceLow() {
    ShortDeck_CardMask straightHand;
    HandVal straightVal;

    // Créer une main de Suite incluant un As comme carte basse (A-6-7-8-9)
    ShortDeck_CardMask_RESET(straightHand);
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_ACE, ShortDeck_Suit_SPADES));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_6, ShortDeck_Suit_HEARTS));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_7, ShortDeck_Suit_DIAMONDS));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_8, ShortDeck_Suit_CLUBS));
    ShortDeck_CardMask_SET(straightHand, ShortDeck_MAKE_CARD(ShortDeck_Rank_9, ShortDeck_Suit_SPADES));


    // Évaluer la main
    straightVal = ShortDeck_ShortRules_EVAL_N(straightHand, ShortDeck_numCards(straightHand));

    // Assert que la main est évaluée comme une Suite
    assert(HandVal_HANDTYPE(straightVal) == ShortRules_HandType_STRAIGHT);
}

int main() {
    testFlushBeatsFullHouse();
    testStraightIncludingAceLow();
    testCardToString();
    testStringToCard();
    testMaskToCards();
    testNumCards();

    printf("Tous les tests unitaires sont passés avec succès !\n");
    return 0;
}
