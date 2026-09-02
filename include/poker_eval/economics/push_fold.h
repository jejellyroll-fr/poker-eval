/* Minimal two-player push/fold Nash solver for tournament spots. */
#ifndef POKER_EVAL_ECONOMICS_PUSH_FOLD_H
#define POKER_EVAL_ECONOMICS_PUSH_FOLD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double pot_before_push;
    double hero_stack;
    double villain_stack;
    double hero_equity_when_called;
    int iterations;
} pe_push_fold_input_t;

typedef struct {
    double hero_push_frequency;
    double villain_call_frequency;
    double hero_ev;
    double exploitability;
    int iterations;
} pe_push_fold_result_t;

/* Solve the explicit fold/push versus fold/call zero-sum abstraction by
 * full-information regret matching.  This is a Nash result for the supplied
 * 2x2 payoff model, not a claim to be a full range-aware HRC replacement. */
int pe_push_fold_solve(const pe_push_fold_input_t *input,
                       pe_push_fold_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ECONOMICS_PUSH_FOLD_H */
