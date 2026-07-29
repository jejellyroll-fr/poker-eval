#ifndef POKER_EVAL_CORE_LOW_EVAL_H
#define POKER_EVAL_CORE_LOW_EVAL_H

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval_low.h>

#ifdef __cplusplus
extern "C" {
#endif

LowHandVal pe_eval_low_a5(StdDeck_CardMask cards);
LowHandVal pe_eval_low_27(StdDeck_CardMask cards);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CORE_LOW_EVAL_H */
