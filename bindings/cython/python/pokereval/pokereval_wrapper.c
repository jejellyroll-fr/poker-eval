#include "deck_std.h"
#include "poker_defs.h"
#include "inlines/eval.h"
#include "inlines/eval_low.h"
#include "inlines/eval_low8.h"
#include "inlines/eval_omaha.h"
#include "inlines/eval_low27.h"

void py_StdDeck_CardMask_RESET(StdDeck_CardMask* cm) {
    StdDeck_CardMask_RESET(*cm);
}

void py_StdDeck_CardMask_OR(StdDeck_CardMask* res, StdDeck_CardMask* op1, StdDeck_CardMask* op2) {
    StdDeck_CardMask_OR(*res, *op1, *op2);
}

void py_StdDeck_CardMask_SET(StdDeck_CardMask* mask, int index) {
    StdDeck_CardMask_SET(*mask, index);
}

void py_StdDeck_CardMask_UNSET(StdDeck_CardMask* mask, int index) {
    StdDeck_CardMask_UNSET(*mask, index);
}

int py_StdDeck_CardMask_CARD_IS_SET(StdDeck_CardMask* cm, int index) {
    return StdDeck_CardMask_CARD_IS_SET(*cm, index);
}

// Vous pouvez également wrapper les fonctions d'évaluation si nécessaire
HandVal py_Hand_EVAL_N(StdDeck_CardMask* hand, int n) {
    return Hand_EVAL_N(*hand, n);
}

LowHandVal py_Hand_EVAL_LOW(StdDeck_CardMask* hand, int n) {
    return Hand_EVAL_LOW(*hand, n);
}

LowHandVal py_Hand_EVAL_LOW8(StdDeck_CardMask* hand, int n) {
    return Hand_EVAL_LOW8(*hand, n);
}

LowHandVal py_Hand_EVAL_DEUCE_TO_SEVEN_LOW(StdDeck_CardMask* hand, int n) {
    return Hand_EVAL_DEUCE_TO_SEVEN_LOW(*hand, n);
}