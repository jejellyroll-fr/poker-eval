/*
 * monker_strategy.c - join a saved strategy to its tree
 */

#include <poker_eval/solver/pe_monker_strategy.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <stdlib.h>
#include <string.h>

#define PE_MONKER_VECTOR_MAX_ACTIONS 32u

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
        /* Renormalise rather than divide by 256: the format rounds each
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

static pe_monker_strategy_game_t *strategy_game(void *user)
{
    return (pe_monker_strategy_game_t *)user;
}

static int game_is_chance(const void *state, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->is_chance(state, adapter->base->user);
}

static uint16_t game_chance_count(const void *state, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->chance_outcome_count(state, adapter->base->user);
}

static double game_chance_weight(const void *state, uint16_t outcome,
                                 void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->chance_outcome_weight(
        state, outcome, adapter->base->user);
}

static const void *game_apply_chance(const void *state, int outcome, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->apply_chance(state, outcome, adapter->base->user);
}

static int game_is_terminal(const void *state, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->is_terminal(state, adapter->base->user);
}

static int game_acting_player(const void *state, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->acting_player(state, adapter->base->user);
}

static uint16_t game_action_count(const void *state, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->action_count(state, adapter->base->user);
}

static uint64_t game_infoset_key(const void *state, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->infoset_key(state, adapter->base->user);
}

static const void *game_apply_action(const void *state, uint16_t action,
                                     void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->apply_action(state, action, adapter->base->user);
}

static int game_terminal_values(const void *state,
                                const pe_reach_vec_t *reach,
                                pe_value_vec_t *out_values,
                                uint8_t player_count, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->terminal_values(
        state, reach, out_values, player_count, adapter->base->user);
}

static int game_combo_compatible(const void *state, uint8_t player,
                                 uint16_t player_combo,
                                 uint8_t opponent, uint16_t opponent_combo,
                                 void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    return adapter->base->combo_compatible(
        state, player, player_combo, opponent, opponent_combo,
        adapter->base->user);
}

static int game_strategy(const void *state, uint64_t infoset_key,
                         uint16_t action, pe_value_vec_t *out, void *user)
{
    pe_monker_strategy_game_t *adapter = strategy_game(user);
    uint16_t combo;
    (void)infoset_key;

    if (!out || out->n != adapter->game.combo_count ||
        action >= PE_MONKER_VECTOR_MAX_ACTIONS)
        return -1;
    for (combo = 0u; combo < adapter->game.combo_count; ++combo)
    {
        int node;
        int cards[4];
        double probs[PE_MONKER_VECTOR_MAX_ACTIONS];
        uint16_t action_count = 0u;
        if (adapter->decode_combo(state, combo, &node, cards,
                                  adapter->decode_user) != 0 ||
            pe_monker_strategy_probs(adapter->strategy, node, cards, probs,
                                     PE_MONKER_VECTOR_MAX_ACTIONS,
                                     &action_count, NULL) != PE_MONKER_OK ||
            action >= action_count)
            return -1;
        out->v[combo] = probs[action];
    }
    return 0;
}

pe_monker_status_t pe_monker_strategy_vector_game_init(
    pe_monker_strategy_game_t *adapter,
    const pe_vector_game_t *base,
    const pe_monker_strategy_t *strategy,
    pe_monker_combo_decoder_fn decode_combo,
    void *decode_user)
{
    uint32_t class_count;

    if (!adapter || !base || !strategy || !decode_combo)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    class_count = pe_monker_strategy_class_count(strategy);
    if (class_count == 0u || class_count != base->combo_count)
        return PE_MONKER_ERR_INVALID_HEADER;
    if (!base->root || !base->is_terminal || !base->acting_player ||
        !base->action_count || !base->infoset_key || !base->apply_action ||
        !base->terminal_values)
        return PE_MONKER_ERR_INVALID_TOPOLOGY;
    if (base->is_chance && (!base->chance_outcome_count ||
                            !base->apply_chance))
        return PE_MONKER_ERR_INVALID_TOPOLOGY;

    memset(adapter, 0, sizeof(*adapter));
    adapter->base = base;
    adapter->strategy = strategy;
    adapter->decode_combo = decode_combo;
    adapter->decode_user = decode_user;
    adapter->game = *base;
    adapter->game.user = adapter;
    adapter->game.strategy = game_strategy;
    adapter->game.is_terminal = game_is_terminal;
    adapter->game.acting_player = game_acting_player;
    adapter->game.action_count = game_action_count;
    adapter->game.infoset_key = game_infoset_key;
    adapter->game.apply_action = game_apply_action;
    adapter->game.terminal_values = game_terminal_values;
    adapter->game.combo_compatible = base->combo_compatible
        ? game_combo_compatible : NULL;
    if (base->is_chance)
    {
        adapter->game.is_chance = game_is_chance;
        adapter->game.chance_outcome_count = game_chance_count;
        adapter->game.chance_outcome_weight = base->chance_outcome_weight
            ? game_chance_weight : NULL;
        adapter->game.apply_chance = game_apply_chance;
    }
    return PE_MONKER_OK;
}
