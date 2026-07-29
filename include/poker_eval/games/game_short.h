#ifndef GAME_SHORT_H
#define GAME_SHORT_H

#define DECK_SHORT
#define RULES_SHORT

#include <poker_eval/deck/deck_short.h>
#include <poker_eval/games/rules_short.h>
#include <poker_eval/games/eval_short.h>

#undef Hand_EVAL_N
#undef Hand_EVAL_TYPE
#undef Hand_EVAL_LOW
#undef Hand_EVAL_LOW8

#define Hand_EVAL_N           ShortDeck_ShortRules_EVAL_N
#define Hand_EVAL_TYPE(m, n)  HandVal_HANDTYPE(Hand_EVAL_N((m), (n)))

#undef  DECK_SHORT
#undef  RULES_SHORT

#endif /* GAME_SHORT_H */
