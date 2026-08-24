/*
 * pe_monker_omaha_tree.h - strategy-weighted Omaha terminal traversal
 */

#ifndef POKER_EVAL_PE_MONKER_OMAHA_TREE_H
#define POKER_EVAL_PE_MONKER_OMAHA_TREE_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_monker_classes.h>
#include <poker_eval/solver/pe_monker_strategy.h>
#include <poker_eval/solver/pe_omaha_deals.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const EvalContext *context;
    mask_t board;
    const pe_omaha_range_t *ranges;
    const pe_betting_state_t *state;
    uint8_t player_count;
    uint8_t hole_cards;
    const mpf_tree_def_t *tree;
    const pe_monker_strategy_t *strategy;
    const pe_monker_classes_t *classes;
} pe_monker_omaha_tree_spec_t;

/**
 * Enumerate correlated Omaha deals and apply the imported Monker policy on
 * every tree edge before valuing the reached terminal. This lane supports
 * high-only PLO4 today; it rejects chance nodes and therefore does not claim
 * to model street transitions yet.
 *
 * `out_values` are net chip EVs, normalized by the range-deal weight.
 * `out_path_weight` receives the normalized probability mass that reached a
 * terminal; a complete tree should report one.
 */
int pe_monker_omaha_tree_values(
    const pe_monker_omaha_tree_spec_t *spec,
    double *out_values,
    size_t *out_deal_count,
    double *out_weight_sum,
    double *out_path_weight);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_MONKER_OMAHA_TREE_H */
