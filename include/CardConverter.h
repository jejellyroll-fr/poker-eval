#ifndef CARD_CONVERTER_H
#define CARD_CONVERTER_H

#include "deck_std.h"

// Declaration of the global table
extern StdDeck_CardMask PokerEvalCards[53];

// Initialization function
void InitPokerEvalCards(void);


StdDeck_CardMask TextToPokerEval(const char *inString);
int TextToPokerEvalArray(const char *inString, StdDeck_CardMask *outArray);
StdDeck_CardMask PokerTrackerToPokerEval(int trackerId);

#endif // CARD_CONVERTER_H