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

static int terminal_value(tree_walk_t *walk, int active[PE_BETTING_MAX_PLAYERS],
                           double path_weight)
{
    const pe_monker_omaha_tree_spec_t *spec = walk->spec;
    HandVal strength[PE_BETTING_MAX_PLAYERS];
    pe_betting_state_t state = *spec->state;
    pe_pot_slice_t slices[PE_BETTING_MAX_PLAYERS];
    uint8_t winners[PE_BETTING_MAX_PLAYERS];
    double awards[PE_BETTING_MAX_PLAYERS];
    uint8_t slice_count = 0u;
    uint8_t player;
    uint8_t slice;

    for (player = 0u; player < spec->player_count; ++player)
        state.active[player] = active[player];
    if (pe_pot_slices_build(&state, slices, PE_BETTING_MAX_PLAYERS,
                            &slice_count) != 0)
        return -1;
    for (player = 0u; player < spec->player_count; ++player)
        if (active[player])
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
            if (!active[player] ||
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
                                (awards[player] - state.invested[player]);
    walk->path_weight += walk->deal_weight * path_weight;
    return 0;
}

static int walk_node(tree_walk_t *walk, int node_index,
                     int active[PE_BETTING_MAX_PLAYERS], double path_weight)
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
        return terminal_value(walk, active, path_weight);
    player = (uint8_t)node->acting_player;
    if (player >= spec->player_count || !active[player] ||
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
        int child_active[PE_BETTING_MAX_PLAYERS];
        double probability = probs[action];
        int next = node->actions[action].next_index;
        if (probability <= 0.0)
            continue;
        memcpy(child_active, active, sizeof(child_active));
        if (node->actions[action].type == MPF_TREE_ACTION_FOLD)
            child_active[player] = 0;
        if (walk_node(walk, next, child_active,
                      path_weight * probability) != 0)
            return -1;
    }
    return 0;
}

static int tree_deal_callback(const mask_t *holes, uint8_t player_count,
                              double weight, void *user)
{
    tree_walk_t *walk = (tree_walk_t *)user;
    int active[PE_BETTING_MAX_PLAYERS];
    uint8_t player;

    if (player_count != walk->spec->player_count)
        return 1;
    for (player = 0u; player < player_count; ++player)
        active[player] = walk->spec->state->active[player] != 0;
    walk->holes = holes;
    walk->deal_weight = weight;
    if (walk_node(walk, walk->spec->tree->root_index, active, 1.0) != 0)
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
