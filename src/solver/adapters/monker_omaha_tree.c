/*
 * monker_omaha_tree.c - exact deal enumeration through an imported tree
 */

#include <poker_eval/solver/pe_monker_omaha_tree.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_pots.h>

#include <math.h>
#include <string.h>

#define PE_MONKER_TREE_MAX_ACTIONS 32u
#define PE_MONKER_SIZE_EPS 1e-12

typedef struct
{
    const pe_monker_omaha_tree_spec_t *spec;
    const mask_t *holes;
    double deal_weight;
    double values[PE_BETTING_MAX_PLAYERS];
    double path_weight;
    int failed;
} tree_walk_t;

static StdDeck_CardMask to_std(mask_t cards)
{
    StdDeck_CardMask result;
    int card;
    StdDeck_CardMask_RESET(result);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card))
            StdDeck_CardMask_SET(result, card);
    return result;
}

static void to_monker_cards(mask_t cards, int out_cards[4])
{
    /* Monker is suit-major s,h,c,d; the modern deck is c,d,h,s. */
    static const int wire_suit[4] = {2, 3, 1, 0};
    int index = 0;
    int card;
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card) && index < 4)
        {
            int suit = MODERN_GET_SUIT(card);
            out_cards[index++] = wire_suit[suit] * 13 + MODERN_GET_RANK(card);
        }
}

static int terminal_value(tree_walk_t *walk,
                          const pe_betting_state_t *betting,
                          double path_weight)
{
    const pe_monker_omaha_tree_spec_t *spec = walk->spec;
    HandVal strength[PE_BETTING_MAX_PLAYERS];
    pe_pot_slice_t slices[PE_BETTING_MAX_PLAYERS];
    uint8_t winners[PE_BETTING_MAX_PLAYERS];
    double awards[PE_BETTING_MAX_PLAYERS];
    uint8_t slice_count = 0u;
    uint8_t player;
    uint8_t slice;

    if (pe_pot_slices_build(betting, slices, PE_BETTING_MAX_PLAYERS,
                            &slice_count) != 0)
        return -1;
    for (player = 0u; player < spec->player_count; ++player)
        if (betting->active[player])
        {
            StdDeck_CardMask hole = to_std(walk->holes[player]);
            if (StdDeck_OmahaHi_EVAL(hole, to_std(spec->board),
                                     &strength[player]) != 0)
                return -1;
        }
    for (slice = 0u; slice < slice_count; ++slice)
    {
        HandVal best = HandVal_NOTHING;
        uint8_t found = 0u;
        winners[slice] = 0u;
        for (player = 0u; player < spec->player_count; ++player)
        {
            if (!betting->active[player] ||
                !(slices[slice].eligible_mask & (uint8_t)(1u << player)))
                continue;
            if (!found || strength[player] > best)
            {
                best = strength[player];
                winners[slice] = (uint8_t)(1u << player);
                found = 1u;
            }
            else if (strength[player] == best)
                winners[slice] |= (uint8_t)(1u << player);
        }
    }
    if (pe_pot_distribute(slices, slice_count, winners, spec->player_count,
                          awards) != 0)
        return -1;
    for (player = 0u; player < spec->player_count; ++player)
        walk->values[player] += walk->deal_weight * path_weight *
                                (awards[player] - betting->invested[player]);
    walk->path_weight += walk->deal_weight * path_weight;
    return 0;
}

static int tree_apply_action(const mpf_tree_node_t *node, int action,
                             pe_betting_state_t *state)
{
    int player;
    double need;
    double commitment = 0.0;

    if (!node || !state ||
        action < 0 || action >= node->action_count)
        return -1;
    player = node->acting_player;
    if (player < 0 || player >= state->player_count)
        return -1;
    if (!state->active[player])
        return -1;
    if (node->actions[action].type == MPF_TREE_ACTION_FOLD)
    {
        state->active[player] = 0;
        state->to_act = -1;
        return 0;
    }
    need = state->to_call - state->round_contrib[player];
    if (need < 0.0)
        need = 0.0;
    if (node->actions[action].type == MPF_TREE_ACTION_CALL)
        commitment = need;
    else if (node->actions[action].type == MPF_TREE_ACTION_RAISE)
    {
        int size_index = node->actions[action].size_index;
        double increment;
        if (size_index < 0 || size_index >= node->bet_size_count)
            return -1;
        increment = node->bet_sizes[size_index];
        if (fabs(increment - (-1.0)) < PE_MONKER_SIZE_EPS)
            commitment = state->stack[player];
        else
        {
            if (fabs(increment - (-2.0)) < PE_MONKER_SIZE_EPS)
                increment = state->min_raise;
            else if (node->use_pot_sizing)
                increment *= state->pot > 0.0 ? state->pot : state->to_call;
            if (!isfinite(increment) || increment < 0.0)
                return -1;
            commitment = need + increment;
        }
    }
    else
        return -1;
    if (commitment < 0.0 || !isfinite(commitment))
        return -1;
    if (commitment > state->stack[player])
        commitment = state->stack[player];
    state->stack[player] -= commitment;
    state->round_contrib[player] += commitment;
    state->invested[player] += commitment;
    state->pot += commitment;
    if (state->stack[player] <= 1e-9)
    {
        state->stack[player] = 0.0;
        state->all_in[player] = 1;
    }
    if (state->round_contrib[player] > state->to_call + 1e-9)
    {
        double increment = state->round_contrib[player] - state->to_call;
        state->to_call = state->round_contrib[player];
        state->current_bet = state->to_call;
        if (increment > state->min_raise)
            state->min_raise = increment;
    }
    state->to_act = -1;
    return 0;
}

static int walk_node(tree_walk_t *walk, int node_index,
                     const pe_betting_state_t *betting, double path_weight)
{
    const pe_monker_omaha_tree_spec_t *spec = walk->spec;
    const mpf_tree_node_t *node;
    double probs[PE_MONKER_TREE_MAX_ACTIONS];
    uint16_t action_count = 0u;
    uint8_t player;
    int cards[4];
    int action;

    if (node_index < 0 || node_index >= spec->tree->node_count)
        return -1;
    node = &spec->tree->nodes[node_index];
    if (node->type == MPF_TREE_NODE_CHANCE)
        return -1;
    if (node->type == MPF_TREE_NODE_TERMINAL || node->action_count == 0)
        return terminal_value(walk, betting, path_weight);
    player = (uint8_t)node->acting_player;
    if (player >= spec->player_count || !betting->active[player] ||
        node->action_count > (int)PE_MONKER_TREE_MAX_ACTIONS)
        return -1;
    to_monker_cards(walk->holes[player], cards);
    if (pe_monker_strategy_probs(spec->strategy, node_index, cards, probs,
                                 PE_MONKER_TREE_MAX_ACTIONS, &action_count,
                                 NULL) != PE_MONKER_OK ||
        action_count != (uint16_t)node->action_count)
        return -1;
    for (action = 0; action < node->action_count; ++action)
    {
        pe_betting_state_t child = *betting;
        double probability = probs[action];
        int next = node->actions[action].next_index;
        if (probability <= 0.0)
            continue;
        if (tree_apply_action(node, action, &child) != 0)
            return -1;
        if (next >= 0 && next < spec->tree->node_count)
            child.to_act = (int8_t)spec->tree->nodes[next].acting_player;
        if (walk_node(walk, next, &child,
                      path_weight * probability) != 0)
            return -1;
    }
    return 0;
}

static int tree_deal_callback(const mask_t *holes, uint8_t player_count,
                              double weight, void *user)
{
    tree_walk_t *walk = (tree_walk_t *)user;
    pe_betting_state_t betting = *walk->spec->state;

    if (player_count != walk->spec->player_count)
        return 1;
    walk->holes = holes;
    walk->deal_weight = weight;
    if (walk_node(walk, walk->spec->tree->root_index, &betting, 1.0) != 0)
    {
        walk->failed = 1;
        return 1;
    }
    return 0;
}

int pe_monker_omaha_tree_values(
    const pe_monker_omaha_tree_spec_t *spec,
    double *out_values,
    size_t *out_deal_count,
    double *out_weight_sum,
    double *out_path_weight)
{
    tree_walk_t walk;
    uint8_t player;
    int status;

    if (!spec || !spec->context || !spec->ranges || !spec->state ||
        !spec->tree || !spec->strategy || !spec->classes || !out_values ||
        !out_deal_count || !out_weight_sum || !out_path_weight ||
        spec->player_count < 2u ||
        spec->player_count > PE_BETTING_MAX_PLAYERS || spec->hole_cards != 4u ||
        !spec->tree->nodes || spec->tree->node_count <= 0)
        return -1;
    for (player = 0u; player < spec->player_count; ++player)
        out_values[player] = 0.0;
    memset(&walk, 0, sizeof(walk));
    walk.spec = spec;
    status = pe_omaha_deals_enumerate(
        spec->board, spec->ranges, spec->player_count, spec->hole_cards,
        tree_deal_callback, &walk, out_deal_count, out_weight_sum);
    if (status != 0 || walk.failed || *out_weight_sum <= 0.0 ||
        !isfinite(*out_weight_sum))
        return -1;
    for (player = 0u; player < spec->player_count; ++player)
        out_values[player] = walk.values[player] / *out_weight_sum;
    *out_path_weight = walk.path_weight / *out_weight_sum;
    return 0;
}
