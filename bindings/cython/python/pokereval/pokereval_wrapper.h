#ifndef POKEREVAL_WRAPPER_H
#define POKEREVAL_WRAPPER_H

#include "deck_std.h"
#include "poker_defs.h"

void py_StdDeck_CardMask_RESET(StdDeck_CardMask* cm);
void py_StdDeck_CardMask_OR(StdDeck_CardMask* res, StdDeck_CardMask* op1, StdDeck_CardMask* op2);
void py_StdDeck_CardMask_SET(StdDeck_CardMask* mask, int index);
void py_StdDeck_CardMask_UNSET(StdDeck_CardMask* mask, int index);
int py_StdDeck_CardMask_CARD_IS_SET(StdDeck_CardMask* cm, int index);

HandVal py_Hand_EVAL_N(StdDeck_CardMask* hand, int n);
LowHandVal py_Hand_EVAL_LOW(StdDeck_CardMask* hand, int n);
LowHandVal py_Hand_EVAL_LOW8(StdDeck_CardMask* hand, int n);
LowHandVal py_Hand_EVAL_DEUCE_TO_SEVEN_LOW(StdDeck_CardMask* hand, int n); 

#endif // POKEREVAL_WRAPPER_H
