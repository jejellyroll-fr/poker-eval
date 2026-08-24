#include <poker_eval/economics/fgs.h>

#include <math.h>
#include <string.h>

static int fgs_walk(const pe_fgs_tree_t *tree, int node_index, double path_probability,
                   int depth, pe_fgs_result_t *result, int *active)
{
    const pe_fgs_node_t *node;
    if (depth > PE_FGS_MAX_DEPTH || node_index < 0 ||
        (size_t)node_index >= tree->node_count || active[node_index])
        return -1;
    node = &tree->nodes[node_index];
    if (node->terminal) {
        icm_input_t input;
        icm_result_t icm;
        memset(&input, 0, sizeof(input));
        input.num_players = tree->num_players;
        input.num_payouts = tree->num_payouts;
        memcpy(input.stacks, node->stacks, sizeof(input.stacks));
        memcpy(input.payouts, tree->payouts, sizeof(input.payouts));
        if (pe_icm_calculate(&input, &icm) != 0)
            return -1;
        for (int p = 0; p < tree->num_players; ++p)
            result->ev[p] += path_probability * icm.icm_ev[p];
        result->leaf_count++;
        result->probability += path_probability;
        return 0;
    }
    if (node->edge_count == 0 || node->edge_count > PE_FGS_MAX_CHILDREN ||
        !tree->edges ||
        node->first_edge > tree->edge_count || node->edge_count > tree->edge_count - node->first_edge)
        return -1;
    {
        double sum = 0.0;
        active[node_index] = 1;
        for (size_t i = 0; i < node->edge_count; ++i) {
            const pe_fgs_edge_t *edge = &tree->edges[node->first_edge + i];
            if (!isfinite(edge->probability) || edge->probability < 0.0 ||
                edge->probability > 1.0 || edge->child_index < 0 ||
                (size_t)edge->child_index >= tree->node_count) {
                active[node_index] = 0;
                return -1;
            }
            sum += edge->probability;
        }
        if (fabs(sum - 1.0) > 1e-9) {
            active[node_index] = 0;
            return -1;
        }
        for (size_t i = 0; i < node->edge_count; ++i) {
            const pe_fgs_edge_t *edge = &tree->edges[node->first_edge + i];
            if (fgs_walk(tree, edge->child_index, path_probability * edge->probability,
                         depth + 1, result, active) != 0) {
                active[node_index] = 0;
                return -1;
            }
        }
        active[node_index] = 0;
    }
    return 0;
}

int pe_fgs_calculate_tree(const pe_fgs_tree_t *tree, pe_fgs_result_t *result)
{
    int active[4096];
    if (!tree || !result || !tree->nodes || tree->node_count == 0 ||
        tree->node_count > 4096 || tree->root_index < 0 ||
        (size_t)tree->root_index >= tree->node_count || tree->num_players <= 0 ||
        tree->num_players > ICM_MAX_PLAYERS || tree->num_payouts <= 0 ||
        tree->num_payouts > ICM_MAX_PLAYERS)
        return -1;
    memset(result, 0, sizeof(*result));
    memset(active, 0, sizeof(active));
    if (fgs_walk(tree, tree->root_index, 1.0, 0, result, active) != 0 ||
        fabs(result->probability - 1.0) > 1e-8)
        return -1;
    return 0;
}
