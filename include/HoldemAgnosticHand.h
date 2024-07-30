#ifndef HOLDEM_AGNOSTIC_HAND_H
#define HOLDEM_AGNOSTIC_HAND_H

#include "deck_std.h"  
#include "poker_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HANDS 1000

typedef struct {
    StdDeck_CardMask hands[MAX_HANDS];
    int count;
} HandList;

// Temporarily disable the StdDeck_maskString macro
#ifdef StdDeck_maskString
#undef StdDeck_maskString
#endif

// Declare the StdDeck_maskString function
void StdDeck_maskString(StdDeck_CardMask mask, char *out);

int HoldemAgnosticHand_Parse(const char* handText, const char* deadCards);
int HoldemAgnosticHand_Parse_StdDeck(const char* handText, StdDeck_CardMask deadCards);

int HoldemAgnosticHand_Instantiate(const char* handText, const char* deadCards, HandList* handList);
int HoldemAgnosticHand_Instantiate_StdDeck(const char* handText, StdDeck_CardMask deadCards, HandList* handList);
int HoldemAgnosticHand_InstantiateRandom(StdDeck_CardMask deadCards, HandList* handList);

int HoldemAgnosticHand_IsPair(const char* handText);
int HoldemAgnosticHand_IsSuited(const char* handText);
int HoldemAgnosticHand_IsOffSuit(const char* handText);
int HoldemAgnosticHand_IsInclusive(const char* handText);
int HoldemAgnosticHand_IsSpecificHand(const char* handText);

#ifdef __cplusplus
}
#endif

// If we need to reactivate the StdDeck_maskString macro, uncomment the following line
// #define StdDeck_maskString(m) GenericDeck_maskString(&StdDeck, ((void *) &(m)))

#endif // HOLDEM_AGNOSTIC_HAND_H