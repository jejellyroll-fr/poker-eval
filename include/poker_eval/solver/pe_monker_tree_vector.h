/*
 * pe_monker_tree_vector.h - vector-game view of a loaded Monker tree
 *
 * This adapter supplies the topology side of pe_monker_strategy_vector_game:
 * actions follow mpf_tree_action_t::next_index and terminal values remain a
 * caller-owned callback because a .tree contains no board or showdown rule.
 */

#ifndef POKER_EVAL_PE_MONKER_TREE_VECTOR_H
#define POKER_EVAL_PE_MONKER_TREE_VECTOR_H

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/solver/pe_traversal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int node_index;
} pe_monker_tree_state_t;

typedef int (*pe_monker_tree_terminal_values_fn)(
    int node_index,
    const pe_reach_vec_t *reach,
    pe_value_vec_t *out_values,
    uint8_t player_count,
    void *user);

typedef struct
{
    pe_vector_game_t game;
    const mpf_tree_def_t *tree;
    pe_monker_tree_terminal_values_fn terminal_values;
    void *user;
    pe_monker_tree_state_t root_state;
    pe_monker_tree_state_t **owned_states;
    size_t owned_count;
    size_t owned_capacity;
} pe_monker_tree_vector_t;

/**
 * Create a vector game whose states are nodes in `tree`.
 *
 * The adapter deliberately rejects chance nodes: Monker betting trees store
 * public-card transitions outside this topology, and silently treating a
 * chance node as a player action would corrupt EV. A caller must compose the
 * appropriate street/chance game before the tree can be used for EV.
 */
int pe_monker_tree_vector_init(
    pe_monker_tree_vector_t *out,
    const mpf_tree_def_t *tree,
    uint8_t player_count,
    uint16_t combo_count,
    pe_monker_tree_terminal_values_fn terminal_values,
    void *user);

void pe_monker_tree_vector_destroy(pe_monker_tree_vector_t *game);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_MONKER_TREE_VECTOR_H */
