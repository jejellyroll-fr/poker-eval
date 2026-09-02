#ifndef POKER_EVAL_ECONOMICS_PUSH_FOLD_MULTIWAY_H
#define POKER_EVAL_ECONOMICS_PUSH_FOLD_MULTIWAY_H

#ifdef __cplusplus
extern "C" {
#endif

#define PE_PUSH_FOLD_MAX_VILLAINS 4
#define PE_PUSH_FOLD_MAX_PROFILES (1 << PE_PUSH_FOLD_MAX_VILLAINS)

typedef struct {
    double pot_before_push;
    double hero_stack;
    double villain_stacks[PE_PUSH_FOLD_MAX_VILLAINS];
    int num_villains;
    /* Hero equity for each caller subset. Bit i means villain i calls. */
    double hero_equity_by_call_mask[PE_PUSH_FOLD_MAX_PROFILES];
    int iterations;
} pe_push_fold_multiway_input_t;

typedef struct {
    double hero_push_frequency;
    double villain_call_frequency[PE_PUSH_FOLD_MAX_VILLAINS];
    double hero_ev;
    double exploitability;
    int iterations;
} pe_push_fold_multiway_result_t;

/* Regret-match the hero against a caller coalition. Equity is supplied per
 * caller subset, so ranges/card removal can be computed by the caller and are
 * not replaced by a single unconditional hand equity. */
int pe_push_fold_multiway_solve(const pe_push_fold_multiway_input_t *input,
                                pe_push_fold_multiway_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ECONOMICS_PUSH_FOLD_MULTIWAY_H */
