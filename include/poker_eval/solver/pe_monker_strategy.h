/*
 * pe_monker_strategy.h - a MonkerSolver saved strategy, indexed by hand
 *
 * The pieces are all in place separately: a .tree gives the topology, a .mkr
 * gives one array per decision node, pe_monker_mkr_bind_strategy says which
 * node an array belongs to, and pe_monker_classes gives the hand class an
 * array is indexed by. This joins them, so that a question a solver can use —
 * "how often does this player take this action at this node holding these four
 * cards?" — has an answer.
 *
 * The frequencies are bytes over 256. They are renormalised on the way out,
 * because MonkerSolver rounds each action to a byte independently and a hand's
 * bytes therefore sum to 256 or, once in three thousand times, to 257.
 *
 * What this does not do is measure anything. Turning an imported strategy into
 * an exploitability figure needs the game the tree describes, and a .tree
 * carries no board: format 33487 stores topology, pot-fraction sizings, dead
 * money and starting stacks, and the board is applied when the solve is
 * launched. A showdown terminal cannot be valued without it.
 */

#ifndef POKER_EVAL_PE_MONKER_STRATEGY_H
#define POKER_EVAL_PE_MONKER_STRATEGY_H

#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_monker_classes.h>
#include <poker_eval/solver/pe_traversal.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_monker_strategy_t pe_monker_strategy_t;

/** Map one vector-lane combo in a game state to a tree node and four cards. */
typedef int (*pe_monker_combo_decoder_fn)(const void *state,
                                          uint16_t combo,
                                          int *out_node,
                                          int out_cards[4],
                                          void *user);

/** A borrowed vector-game view whose strategy callback reads Monker bytes. */
typedef struct
{
    pe_vector_game_t game;
    const pe_vector_game_t *base;
    const pe_monker_strategy_t *strategy;
    pe_monker_combo_decoder_fn decode_combo;
    void *decode_user;
} pe_monker_strategy_game_t;

/**
 * Join a stored strategy to its tree and to the hand-class numbering.
 *
 * Borrows all three: none may be freed while the view is open. Fails if the
 * stored slots do not bind to the tree, so a view that opens is one whose
 * every array is known to belong to the node it is read against.
 */
pe_monker_status_t pe_monker_strategy_open(
    const struct mpf_tree_def_t *tree,
    const pe_monker_mkr_strategy_t *stored,
    const pe_monker_classes_t *classes,
    pe_monker_strategy_t **out);

void pe_monker_strategy_close(pe_monker_strategy_t *view);

/** The hand classes the view is indexed by, and the tree it is bound to. */
uint32_t pe_monker_strategy_class_count(const pe_monker_strategy_t *view);

/**
 * The action probabilities at `node` for the hand `cards` (four distinct card
 * indices, any order).
 *
 * `out_probs` receives one probability per action, summing to 1. When the
 * archive holds no strategy for that hand — 87 classes of the run this was
 * built against are stored as all-zero — the actions come back uniform and
 * `out_specified` is set to 0. Passing NULL for `out_specified` is allowed;
 * the difference between "plays uniformly" and "was never given a strategy"
 * is then silently lost, which is the caller's choice to make.
 */
pe_monker_status_t pe_monker_strategy_probs(
    const pe_monker_strategy_t *view,
    int node,
    const int *cards,
    double *out_probs,
    size_t capacity,
    uint16_t *out_action_count,
    int *out_specified);

/**
 * Wrap a vector game with the imported Monker strategy.
 *
 * The base game supplies topology, chance and terminal values. The decoder
 * supplies the corresponding Monker tree node and canonical four-card hand
 * for each combo in a state. The returned game borrows every input and is
 * valid while `adapter` and those inputs remain alive.
 */
pe_monker_status_t pe_monker_strategy_vector_game_init(
    pe_monker_strategy_game_t *adapter,
    const pe_vector_game_t *base,
    const pe_monker_strategy_t *strategy,
    pe_monker_combo_decoder_fn decode_combo,
    void *decode_user);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_MONKER_STRATEGY_H */
