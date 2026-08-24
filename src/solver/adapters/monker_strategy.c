/*
 * monker_strategy.c - join a saved MonkerSolver strategy to its tree
 */

#include <poker_eval/solver/pe_monker_strategy.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <stdlib.h>
#include <string.h>

struct pe_monker_strategy_t
{
    const mpf_tree_def_t *tree;
    const pe_monker_mkr_strategy_t *stored;
    const pe_monker_classes_t *classes;
    /* Which slot holds the strategy for each node, or -1. Only the byte slots
       are indexed: the int slots run parallel and are not a strategy. */
    int32_t *slot_of_node;
    uint32_t class_count;
};

pe_monker_status_t pe_monker_strategy_open(
    const mpf_tree_def_t *tree,
    const pe_monker_mkr_strategy_t *stored,
    const pe_monker_classes_t *classes,
    pe_monker_strategy_t **out)
{
    pe_monker_strategy_t *view;
    int32_t *map = NULL;
    uint32_t slot;
    int node;
    uint32_t class_count = 0u;

    if (tree == NULL || stored == NULL || classes == NULL || out == NULL)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    *out = NULL;
    if (tree->node_count <= 0 || tree->nodes == NULL)
        return PE_MONKER_ERR_INVALID_TOPOLOGY;

    if (pe_monker_mkr_strategy_class_count(tree, stored, &class_count) !=
        PE_MONKER_MKR_OK)
        return PE_MONKER_ERR_INVALID_HEADER;

    map = (int32_t *)malloc((size_t)stored->slot_count * sizeof(*map));
    if (map == NULL)
        return PE_MONKER_ERR_IO;
    if (pe_monker_mkr_bind_strategy(tree, stored, map, stored->slot_count) !=
        PE_MONKER_MKR_OK)
    {
        free(map);
        return PE_MONKER_ERR_INVALID_TOPOLOGY;
    }

    view = (pe_monker_strategy_t *)calloc(1u, sizeof(*view));
    if (view == NULL)
    {
        free(map);
        return PE_MONKER_ERR_IO;
    }
    view->slot_of_node = (int32_t *)malloc((size_t)tree->node_count *
                                           sizeof(*view->slot_of_node));
    if (view->slot_of_node == NULL)
    {
        free(map);
        free(view);
        return PE_MONKER_ERR_IO;
    }
    for (node = 0; node < tree->node_count; ++node)
        view->slot_of_node[node] = -1;
    for (slot = 0u; slot < stored->slot_count; ++slot)
    {
        if (stored->slots[slot].kind != PE_MONKER_SLOT_BYTES)
            continue;
        /* An entry holds the strategy once per node; a second byte slot for
           the same node would mean the halves are not what they are taken to
           be, so it is refused rather than quietly overwritten. */
        if (view->slot_of_node[map[slot]] >= 0)
        {
            free(map);
            pe_monker_strategy_close(view);
            return PE_MONKER_ERR_INVALID_TOPOLOGY;
        }
        view->slot_of_node[map[slot]] = (int32_t)slot;
    }
    free(map);

    /* Every node that decides must have one, or the view would answer some
       questions and silently not others. */
    for (node = 0; node < tree->node_count; ++node)
        if ((tree->nodes[node].action_count > 0) !=
            (view->slot_of_node[node] >= 0))
        {
            pe_monker_strategy_close(view);
            return PE_MONKER_ERR_INVALID_TOPOLOGY;
        }

    view->tree = tree;
    view->stored = stored;
    view->classes = classes;
    view->class_count = class_count;
    *out = view;
    return PE_MONKER_OK;
}

void pe_monker_strategy_close(pe_monker_strategy_t *view)
{
    if (view == NULL)
        return;
    free(view->slot_of_node);
    free(view);
}

uint32_t pe_monker_strategy_class_count(const pe_monker_strategy_t *view)
{
    return view == NULL ? 0u : view->class_count;
}

pe_monker_status_t pe_monker_strategy_probs(
    const pe_monker_strategy_t *view,
    int node,
    const int *cards,
    double *out_probs,
    size_t capacity,
    uint16_t *out_action_count,
    int *out_specified)
{
    uint32_t hand_class = 0u;
    int32_t slot;
    int actions;
    uint32_t base;
    double total = 0.0;
    int a;

    if (view == NULL || cards == NULL || out_probs == NULL ||
        out_action_count == NULL)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    if (node < 0 || node >= view->tree->node_count)
        return PE_MONKER_ERR_INVALID_TOPOLOGY;
    actions = view->tree->nodes[node].action_count;
    if (actions <= 0)
        return PE_MONKER_ERR_INVALID_TOPOLOGY;
    if (capacity < (size_t)actions)
        return PE_MONKER_ERR_INVALID_HEADER;
    slot = view->slot_of_node[node];
    if (slot < 0)
        return PE_MONKER_ERR_INVALID_TOPOLOGY;
    if (pe_monker_class_of(view->classes, cards, &hand_class) != PE_MONKER_OK)
        return PE_MONKER_ERR_INVALID_HEADER;
    if (hand_class >= view->class_count)
        return PE_MONKER_ERR_INVALID_HEADER;

    base = hand_class * (uint32_t)actions;
    if (base + (uint32_t)actions > view->stored->slots[slot].count)
        return PE_MONKER_ERR_TRUNCATED;
    for (a = 0; a < actions; ++a)
    {
        out_probs[a] = (double)view->stored->slots[slot].bytes[base + (uint32_t)a];
        total += out_probs[a];
    }
    *out_action_count = (uint16_t)actions;
    if (total > 0.0)
    {
        /* Renormalise rather than divide by 256: MonkerSolver rounds each
           action independently, so a hand's bytes sum to 256 or sometimes
           257, and dividing by a constant would leave the probabilities
           slightly off one. */
        for (a = 0; a < actions; ++a)
            out_probs[a] /= total;
        if (out_specified != NULL)
            *out_specified = 1;
        return PE_MONKER_OK;
    }
    for (a = 0; a < actions; ++a)
        out_probs[a] = 1.0 / (double)actions;
    if (out_specified != NULL)
        *out_specified = 0;
    return PE_MONKER_OK;
}
