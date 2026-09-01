#include <poker_eval/economics/fgs.h>

#include <poker_eval/economics/icm.h>

#include <math.h>
#include <string.h>

#include "../solver/domain/finite_double.h"

static int fgs_walk(const pe_fgs_tree_t *tree, int node_index, double path_probability,
                   int depth, pe_fgs_result_t *result, int *active,
                   const int *elimination_round)
{
    const pe_fgs_node_t *node;
    if (depth > PE_FGS_MAX_DEPTH || node_index < 0 ||
        (size_t)node_index >= tree->node_count || active[node_index])
        return -1;
    node = &tree->nodes[node_index];
    if (node->terminal) {
        icm_input_t input;
        icm_result_t icm;
        int player_map[ICM_MAX_PLAYERS];
        int eliminated_map[ICM_MAX_PLAYERS];
        int eliminated_round[ICM_MAX_PLAYERS];
        int assigned[ICM_MAX_PLAYERS];
        int active_players = 0;
        int eliminated_players = 0;
        memset(&input, 0, sizeof(input));
        for (int p = 0; p < tree->num_players; ++p) {
            if (!pe_finite_double(node->stacks[p]) || node->stacks[p] < 0.0)
                return -1;
            if (node->stacks[p] > 0.0) {
                player_map[active_players] = p;
                input.stacks[active_players++] = node->stacks[p];
            }
            else {
                eliminated_map[eliminated_players++] = p;
                eliminated_round[eliminated_players - 1] = elimination_round[p] >= 0
                    ? elimination_round[p] : 0;
            }
        }
        if (active_players == 0)
            return -1;
        input.num_players = active_players;
        input.num_payouts = tree->num_payouts < active_players
            ? tree->num_payouts : active_players;
        for (int p = 0; p < tree->num_payouts; ++p) {
            if (!pe_finite_double(tree->payouts[p]) || tree->payouts[p] < 0.0)
                return -1;
            if (p < input.num_payouts)
                input.payouts[p] = tree->payouts[p];
        }
        if (pe_icm_calculate(&input, &icm) != 0)
            return -1;
        for (int p = 0; p < active_players; ++p)
            result->ev[player_map[p]] += path_probability * icm.icm_ev[p];
        memset(assigned, 0, sizeof(assigned));
        for (int placed = 0; placed < eliminated_players;) {
            int group_round = PE_FGS_MAX_DEPTH + 1;
            int group_count = 0;
            double group_payout = 0.0;

            for (int p = 0; p < eliminated_players; ++p)
                if (!assigned[p] && eliminated_round[p] < group_round)
                    group_round = eliminated_round[p];
            for (int p = 0; p < eliminated_players; ++p)
                if (!assigned[p] && eliminated_round[p] == group_round)
                    group_count++;
            if (group_count == 0)
                return -1;

            /* Earlier eliminations finish later. Players eliminated by the
             * same transition tie and split the places for that group. */
            {
                int first_payout = tree->num_players - placed - group_count;
                int last_payout = tree->num_players - placed - 1;
                for (int payout = first_payout; payout <= last_payout; ++payout)
                    if (payout >= 0 && payout < tree->num_payouts)
                        group_payout += tree->payouts[payout];
            }
            for (int p = 0; p < eliminated_players; ++p) {
                if (!assigned[p] && eliminated_round[p] == group_round) {
                    result->ev[eliminated_map[p]] +=
                        path_probability * group_payout / (double)group_count;
                    assigned[p] = 1;
                }
            }
            placed += group_count;
        }
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
            if (!pe_finite_double(edge->probability) || edge->probability < 0.0 ||
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
            int child_elimination_round[ICM_MAX_PLAYERS];
            const pe_fgs_node_t *child = &tree->nodes[edge->child_index];
            memcpy(child_elimination_round, elimination_round,
                   sizeof(child_elimination_round));
            for (int p = 0; p < tree->num_players; ++p) {
                if (child_elimination_round[p] < 0 && node->stacks[p] > 0.0 &&
                    child->stacks[p] <= 0.0)
                    child_elimination_round[p] = depth + 1;
            }
            if (fgs_walk(tree, edge->child_index, path_probability * edge->probability,
                         depth + 1, result, active, child_elimination_round) != 0) {
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
        tree->num_payouts > ICM_MAX_PLAYERS ||
        tree->num_payouts > tree->num_players)
        return -1;
    memset(result, 0, sizeof(*result));
    memset(active, 0, sizeof(active));
    {
        int elimination_round[ICM_MAX_PLAYERS];
        for (int p = 0; p < ICM_MAX_PLAYERS; ++p)
            elimination_round[p] = -1;
        for (int p = 0; p < tree->num_players; ++p)
            if (tree->nodes[tree->root_index].stacks[p] <= 0.0)
                elimination_round[p] = 0;
        if (fgs_walk(tree, tree->root_index, 1.0, 0, result, active,
                     elimination_round) != 0)
            return -1;
    }
    if (fabs(result->probability - 1.0) > 1e-8)
        return -1;
    return 0;
}

#define PE_FGS_GENERATOR_NODE_LIMIT 4096

typedef struct {
    const pe_fgs_scenario_input_t *input;
    pe_fgs_node_t *nodes;
    size_t node_capacity;
    size_t node_count;
    pe_fgs_edge_t *edges;
    size_t edge_capacity;
    size_t edge_count;
} fgs_generator_t;

static int fgs_generate_node(fgs_generator_t *gen, const double *stacks, int depth)
{
    int node_index;
    int active_index[ICM_MAX_PLAYERS];
    int active_count = 0;
    int i;
    double win_sum = 0.0;

    if (gen->node_count >= gen->node_capacity ||
        gen->node_count >= PE_FGS_GENERATOR_NODE_LIMIT)
        return -1;
    node_index = (int)gen->node_count++;
    memset(&gen->nodes[node_index], 0, sizeof(gen->nodes[node_index]));
    for (i = 0; i < ICM_MAX_PLAYERS; ++i)
        gen->nodes[node_index].stacks[i] = stacks[i];

    for (i = 0; i < gen->input->num_players; ++i) {
        if (stacks[i] > 0.0) {
            if (active_count >= PE_FGS_MAX_CHILDREN)
                return -1;
            active_index[active_count++] = i;
            win_sum += gen->input->win_probability[i];
        }
    }
    if (depth <= 0 || active_count < 2 || !(win_sum > 0.0) ||
        !(gen->input->pot > 0.0)) {
        gen->nodes[node_index].terminal = 1;
        return node_index;
    }

    {
        size_t first_edge = gen->edge_count;
        size_t edge_count = (size_t)active_count;
        double deduction = gen->input->pot / (double)(active_count - 1);
        /* Reserve the parent's edge block before recursing: each child
         * appends its own subtree edges, so the slots must be claimed up
         * front to stay contiguous. */
        if (gen->edge_count + edge_count > gen->edge_capacity)
            return -1;
        gen->edge_count += edge_count;
        for (i = 0; i < active_count; ++i) {
            int winner = active_index[i];
            double child_stacks[ICM_MAX_PLAYERS];
            double moved = 0.0;
            int child;
            int j;
            for (j = 0; j < ICM_MAX_PLAYERS; ++j)
                child_stacks[j] = stacks[j];
            for (j = 0; j < active_count; ++j) {
                int loser = active_index[j];
                double taken;
                if (loser == winner)
                    continue;
                taken = deduction < child_stacks[loser] ? deduction
                                                        : child_stacks[loser];
                child_stacks[loser] -= taken;
                moved += taken;
            }
            child_stacks[winner] += moved;
            child = fgs_generate_node(gen, child_stacks, depth - 1);
            if (child < 0)
                return -1;
            gen->edges[first_edge + (size_t)i].child_index = child;
            gen->edges[first_edge + (size_t)i].probability =
                gen->input->win_probability[winner] / win_sum;
        }
        gen->nodes[node_index].first_edge = first_edge;
        gen->nodes[node_index].edge_count = edge_count;
    }
    return node_index;
}

int pe_fgs_generate_even_contribution(const pe_fgs_scenario_input_t *input,
                                      pe_fgs_node_t *nodes, size_t node_capacity,
                                      pe_fgs_edge_t *edges, size_t edge_capacity,
                                      pe_fgs_tree_t *out_tree)
{
    fgs_generator_t gen;
    int root;
    int i;
    double payout_sum = 0.0;

    if (!input || !nodes || !edges || !out_tree || node_capacity == 0u ||
        edge_capacity == 0u)
        return -1;
    if (input->num_players < 2 || input->num_players > ICM_MAX_PLAYERS ||
        input->num_payouts < 1 || input->num_payouts > ICM_MAX_PLAYERS ||
        input->num_payouts > input->num_players ||
        input->depth < 0 || input->depth > PE_FGS_MAX_DEPTH)
        return -1;
    for (i = 0; i < input->num_players; ++i) {
        if (!pe_finite_double(input->stacks[i]) || input->stacks[i] < 0.0 ||
            !pe_finite_double(input->win_probability[i]) ||
            input->win_probability[i] < 0.0)
            return -1;
    }
    for (i = 0; i < input->num_payouts; ++i) {
        if (!pe_finite_double(input->payouts[i]) || input->payouts[i] < 0.0)
            return -1;
        payout_sum += input->payouts[i];
    }
    if (!pe_finite_double(input->pot) || input->pot < 0.0 || !(payout_sum > 0.0))
        return -1;

    memset(&gen, 0, sizeof(gen));
    gen.input = input;
    gen.nodes = nodes;
    gen.node_capacity = node_capacity;
    gen.edges = edges;
    gen.edge_capacity = edge_capacity;
    root = fgs_generate_node(&gen, input->stacks, input->depth);
    if (root != 0)
        return -1;

    memset(out_tree, 0, sizeof(*out_tree));
    out_tree->nodes = nodes;
    out_tree->node_count = gen.node_count;
    out_tree->edges = edges;
    out_tree->edge_count = gen.edge_count;
    out_tree->root_index = 0;
    out_tree->num_players = input->num_players;
    out_tree->num_payouts = input->num_payouts;
    for (int p = 0; p < ICM_MAX_PLAYERS; ++p)
        out_tree->payouts[p] = input->payouts[p];
    return 0;
}
