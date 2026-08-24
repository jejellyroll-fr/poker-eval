/* Dynamic Future Game Simulation (FGS) over a tournament chance tree. */
#ifndef POKER_EVAL_ECONOMICS_FGS_H
#define POKER_EVAL_ECONOMICS_FGS_H

#include <stddef.h>

#include <poker_eval/economics/icm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_FGS_MAX_CHILDREN 16
#define PE_FGS_MAX_DEPTH 128

typedef struct {
    int child_index;
    double probability;
} pe_fgs_edge_t;

typedef struct {
    int terminal;
    size_t first_edge;
    size_t edge_count;
    double stacks[ICM_MAX_PLAYERS];
} pe_fgs_node_t;

typedef struct {
    const pe_fgs_node_t *nodes;
    size_t node_count;
    const pe_fgs_edge_t *edges;
    size_t edge_count;
    int root_index;
    int num_players;
    int num_payouts;
    double payouts[ICM_MAX_PLAYERS];
} pe_fgs_tree_t;

typedef struct {
    double ev[ICM_MAX_PLAYERS];
    size_t leaf_count;
    double probability;
} pe_fgs_result_t;

/* Every non-terminal node is a chance/transition node. Edge probabilities are
 * validated locally and multiplied along the path, so the same API handles a
 * flat FGS list, a multi-round tournament tree, or a tree with shared stack
 * snapshots at every transition. */
int pe_fgs_calculate_tree(const pe_fgs_tree_t *tree, pe_fgs_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
