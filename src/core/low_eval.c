#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/handval_low.h>
#include <poker_eval/games/eval_low.h>
#include <poker_eval/games/eval_low27.h>

LowHandVal pe_eval_low_a5(StdDeck_CardMask cards) {
    int count = StdDeck_numCards(cards);
    if (count < 5)
        return LowHandVal_NOTHING;
    return StdDeck_Lowball_EVAL(cards, count);
}

LowHandVal pe_eval_low_27(StdDeck_CardMask cards) {
    int count = StdDeck_numCards(cards);
    if (count < 5)
        return LowHandVal_NOTHING;
    HandVal hv = StdDeck_Lowball27_EVAL_N(cards, count);
    return (LowHandVal)hv;
}
