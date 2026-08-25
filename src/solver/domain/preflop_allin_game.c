/* preflop_allin_game.c - production preflop game over sampled deals (Lane B)
 *
 * Model: one complete preflop betting street over sampled private deals.
 * Hold'em and Omaha 4/5/6 are supported for two to six players. Terminals
 * are fold-outs or called pots resolved as an all-in showdown with boards
 * sampled deterministically per deal, so MCCFR iterations see a stable value
 * for a given deal.
 */

#include <poker_eval/solver/pe_preflop_allin_game.h>

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_holdem_round.h>
#include <poker_eval/solver/pe_range.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFLOP_FNV_OFFSET 0xcbf29ce484222325ULL
#define PREFLOP_FNV_PRIME 0x100000001b3ULL
#define PREFLOP_EPSILON 1e-6
#define PREFLOP_MAX_ACTIONS 16
#define PREFLOP_DESC_TEXT 192

typedef struct
{
    uint64_t key;
    char text[PREFLOP_DESC_TEXT];
    pe_preflop_betting_state_t state;
    int actor;
    int tree_node_index;
    double pot;
    double to_call;
    char hand[32];
    uint16_t action_count;
    char actions[PE_PREFLOP_ALLIN_MAX_DESC_ACTIONS]
                 [PE_PREFLOP_ALLIN_MAX_ACTION_LABEL];
} preflop_infodesc_t;

struct pe_preflop_allin_game_t
{
    pe_preflop_allin_rules_t rules;
    pe_range_t *const *ranges;
    pe_holdem_combo_t *holdem_combo_owned[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_omaha_combo_t *omaha_combo_owned[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_holdem_range_t holdem_ranges[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_omaha_range_t omaha_ranges[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_preflop_deal_sampler_t sampler;
    pe_betting_rules_t betting_rules;
    pe_preflop_betting_state_t root_betting;
    pe_preflop_betting_ops_t ops;
    pe_preflop_betting_game_t betting_game;
    pe_external_game_t external;
    pe_storage_t *storage;
    EvalContext *eval_ctx;
    preflop_infodesc_t *descs;
    size_t desc_count;
    size_t desc_capacity;
};

static int tree_action_to_semantic(const mpf_tree_node_t *node, int index,
                                   pe_action_t *out)
{
    const mpf_tree_action_t *source;
    if (!node || !out || index < 0 || index >= node->action_count)
        return -1;
    source = &node->actions[index];
    memset(out, 0, sizeof(*out));
    switch (source->type)
    {
    case MPF_TREE_ACTION_FOLD:
        out->kind = PE_ACTION_FOLD;
        return 0;
    case MPF_TREE_ACTION_CALL:
        out->kind = PE_ACTION_CALL;
        return 0;
    case MPF_TREE_ACTION_RAISE:
        if (source->size_index < 0 || source->size_index >= node->bet_size_count)
            return -1;
        if (fabs(node->bet_sizes[source->size_index] - (-1.0)) < PREFLOP_EPSILON)
        {
            out->kind = PE_ACTION_ALL_IN;
            return 0;
        }
        out->kind = PE_ACTION_RAISE;
        out->size_index = source->size_index;
        if (fabs(node->bet_sizes[source->size_index] - (-2.0)) < PREFLOP_EPSILON)
        {
            out->amount_kind = PE_AMOUNT_MINIMUM;
            out->amount = 0.0;
        }
        else if (node->use_pot_sizing)
        {
            out->amount_kind = PE_AMOUNT_POT_FRACTION;
            out->amount = node->bet_sizes[source->size_index];
        }
        else
        {
            out->amount_kind = PE_AMOUNT_CHIPS;
            out->amount = node->bet_sizes[source->size_index];
        }
        return 0;
    case MPF_TREE_ACTION_CHANCE:
    case MPF_TREE_ACTION_TERMINAL:
        return -1;
    default:
        return -1;
    }
}

static const mpf_tree_node_t *preflop_tree_node(
    const pe_preflop_allin_game_t *game,
    const pe_preflop_betting_state_t *state)
{
    if (!game || !state || !game->rules.tree || state->tree_node_index < 0 ||
        state->tree_node_index >= game->rules.tree->node_count)
        return NULL;
    return &game->rules.tree->nodes[state->tree_node_index];
}

/* ------------------------------------------------------------------ *
 * Hashing helpers
 * ------------------------------------------------------------------ */

static uint64_t preflop_mix_u64(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= PREFLOP_FNV_PRIME;
    return hash;
}

static uint64_t preflop_quantize(double chips)
{
    return (uint64_t)llround(chips * 2.0);
}

static unsigned preflop_card_count(StdDeck_CardMask hand)
{
    unsigned count = 0u;
    for (int card = 0; card < StdDeck_N_CARDS; ++card)
        if (StdDeck_CardMask_CARD_IS_SET(hand, card))
            ++count;
    return count;
}

/* ------------------------------------------------------------------ *
 * Action enumeration
 * ------------------------------------------------------------------ */

static int preflop_call_is_allin(const pe_preflop_allin_game_t *game,
                                 const pe_preflop_betting_state_t *state)
{
    const pe_betting_state_t *betting = &state->betting;
    int actor = betting->to_act;
    (void)game;
    if (actor < 0)
        return 0;
    return betting->stack[actor] <= betting->to_call + PREFLOP_EPSILON;
}

static uint16_t preflop_enumerate(const pe_preflop_allin_game_t *game,
                                  const pe_preflop_betting_state_t *state,
                                  pe_action_t *out, uint16_t max_actions)
{
    const pe_betting_state_t *betting = &state->betting;
    pe_action_t candidates[PREFLOP_MAX_ACTIONS];
    double commitments[PREFLOP_MAX_ACTIONS];
    uint16_t count = 0u;
    uint16_t i;
    int index;

    if (game->rules.tree && state->street == PE_HOLDEM_PREFLOP &&
        state->tree_node_index >= 0)
    {
        const mpf_tree_node_t *node = preflop_tree_node(game, state);
        uint16_t kept = 0u;
        if (!node || node->type != MPF_TREE_NODE_PLAYER ||
            node->acting_player != betting->to_act)
            return 0u;
        for (index = 0; index < node->action_count && kept < max_actions; ++index)
        {
            pe_action_t candidate;
            if (tree_action_to_semantic(node, index, &candidate) != 0)
                continue;
            if (candidate.kind == PE_ACTION_CALL && betting->to_call <= PREFLOP_EPSILON)
                candidate.kind = PE_ACTION_CHECK;
            if (pe_betting_action_is_legal(betting, &game->betting_rules,
                                           &candidate) == PE_BETTING_OK)
            {
                if (out)
                    out[kept] = candidate;
                ++kept;
            }
        }
        return kept;
    }

    if (betting->terminal || betting->round_complete || betting->to_act < 0)
        return 0u;

    if (betting->to_call > PREFLOP_EPSILON)
    {
        pe_action_t fold;
        memset(&fold, 0, sizeof(fold));
        fold.kind = PE_ACTION_FOLD;
        if (pe_betting_action_is_legal(betting, &game->betting_rules, &fold) ==
            PE_BETTING_OK)
            candidates[count++] = fold;
    }
    else
    {
        pe_action_t check;
        memset(&check, 0, sizeof(check));
        check.kind = PE_ACTION_CHECK;
        if (pe_betting_action_is_legal(betting, &game->betting_rules, &check) ==
            PE_BETTING_OK)
            candidates[count++] = check;
    }

    if (betting->to_call > PREFLOP_EPSILON)
    {
        pe_action_t call;
        memset(&call, 0, sizeof(call));
        call.kind = PE_ACTION_CALL;
        if (pe_betting_action_is_legal(betting, &game->betting_rules, &call) ==
                PE_BETTING_OK &&
            (game->rules.allow_nonallin_call ||
             preflop_call_is_allin(game, state)))
            candidates[count++] = call;
    }

    for (index = 0; index < game->rules.raise_count && count < PREFLOP_MAX_ACTIONS;
         ++index)
    {
        pe_action_t raise;
        memset(&raise, 0, sizeof(raise));
        raise.kind = PE_ACTION_RAISE;
        raise.amount_kind = PE_AMOUNT_CHIPS;
        raise.amount = betting->to_call + game->rules.raise_sizes[index];
        raise.size_index = index;
        if (pe_betting_action_is_legal(betting, &game->betting_rules, &raise) ==
            PE_BETTING_OK)
            candidates[count++] = raise;
    }

    {
        pe_action_t all_in;
        memset(&all_in, 0, sizeof(all_in));
        all_in.kind = PE_ACTION_ALL_IN;
        if (pe_betting_action_is_legal(betting, &game->betting_rules, &all_in) ==
            PE_BETTING_OK)
            candidates[count++] = all_in;
    }

    /* Drop duplicates: two actions that commit the same chips produce the
       same child, and double-counting them would skew the strategy. */
    count = count < max_actions ? count : max_actions;
    for (i = 0u; i < count; ++i)
    {
        double commitment = 0.0;
        int actor = betting->to_act;
        if (pe_action_commitment(&candidates[i], betting->to_call,
                                 actor >= 0 ? betting->stack[actor] : 0.0,
                                 betting->pot, betting->min_raise,
                                 game->betting_rules.pot_limit,
                                 &commitment) != PE_ACTION_OK)
            commitment = -1.0;
        commitments[i] = commitment;
    }
    {
        uint16_t kept = 0u;
        for (i = 0u; i < count; ++i)
        {
            uint16_t j;
            int duplicate = 0;
            for (j = 0u; j < kept; ++j)
                if (fabs(commitments[j] - commitments[i]) < PREFLOP_EPSILON)
                {
                    duplicate = 1;
                    break;
                }
            if (duplicate)
                continue;
            if (out != NULL)
                out[kept] = candidates[i];
            commitments[kept] = commitments[i];
            ++kept;
        }
        return kept;
    }
}

static uint16_t preflop_op_action_count(const pe_preflop_betting_state_t *state,
                                        void *user)
{
    const pe_preflop_allin_game_t *game = user;
    return preflop_enumerate(game, state, NULL, PREFLOP_MAX_ACTIONS);
}

static pe_action_status_t preflop_op_action_at(
    const pe_preflop_betting_state_t *state, uint16_t action, pe_action_t *out,
    void *user)
{
    const pe_preflop_allin_game_t *game = user;
    pe_action_t actions[PREFLOP_MAX_ACTIONS];
    uint16_t count = preflop_enumerate(game, state, actions, PREFLOP_MAX_ACTIONS);
    if (!out || action >= count)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    *out = actions[action];
    return PE_ACTION_OK;
}

static int tree_action_matches(const pe_action_t *wanted,
                               const pe_action_t *candidate)
{
    if (!wanted || !candidate || wanted->kind != candidate->kind)
        return 0;
    if (wanted->kind != PE_ACTION_RAISE)
        return 1;
    return wanted->amount_kind == candidate->amount_kind &&
           fabs(wanted->amount - candidate->amount) < PREFLOP_EPSILON;
}

/* ------------------------------------------------------------------ *
 * Infset identity and descriptions
 * ------------------------------------------------------------------ */

static void preflop_record_desc(pe_preflop_allin_game_t *game, uint64_t key,
                                const pe_preflop_betting_state_t *state)
{
    const pe_betting_state_t *betting = &state->betting;
    pe_action_t actions[PE_PREFLOP_ALLIN_MAX_DESC_ACTIONS];
    uint16_t action_count;
    size_t i;
    for (i = 0u; i < game->desc_count; ++i)
        if (game->descs[i].key == key)
            return;
    if (game->desc_count == game->desc_capacity)
    {
        size_t capacity = game->desc_capacity ? game->desc_capacity * 2u : 64u;
        preflop_infodesc_t *grown =
            realloc(game->descs, capacity * sizeof(*grown));
        if (!grown)
            return;
        game->descs = grown;
        game->desc_capacity = capacity;
    }
    game->descs[game->desc_count].key = key;
    game->descs[game->desc_count].state = *state;
    game->descs[game->desc_count].actor = betting->to_act;
    game->descs[game->desc_count].tree_node_index = state->tree_node_index;
    game->descs[game->desc_count].pot = betting->pot;
    game->descs[game->desc_count].to_call = betting->to_call;
    game->descs[game->desc_count].hand[0] = '\0';
    if (betting->to_act >= 0 && betting->to_act < betting->player_count)
    {
        size_t used = 0u;
        const mask_t hand = state->holes[betting->to_act];
        const char suit_chars[] = "cdhs";
        for (int card = 0; card < 52 && used + 2u < sizeof(game->descs[game->desc_count].hand); ++card)
        {
            if (!mask_is_set(hand, card))
                continue;
            game->descs[game->desc_count].hand[used++] =
                StdDeck_rankChars[MODERN_GET_RANK(card)];
            game->descs[game->desc_count].hand[used++] =
                suit_chars[MODERN_GET_SUIT(card)];
        }
        game->descs[game->desc_count].hand[used] = '\0';
    }
    action_count = preflop_enumerate(game, state, actions,
                                      PE_PREFLOP_ALLIN_MAX_DESC_ACTIONS);
    game->descs[game->desc_count].action_count = action_count;
    for (uint16_t action = 0u; action < action_count; ++action)
    {
        const pe_action_t *a = &actions[action];
        char *label = game->descs[game->desc_count].actions[action];
        if (a->kind == PE_ACTION_RAISE)
        {
            if (a->amount_kind == PE_AMOUNT_POT_FRACTION)
                snprintf(label, PE_PREFLOP_ALLIN_MAX_ACTION_LABEL,
                         "RAISE %.0f%% POT", a->amount * 100.0);
            else if (a->amount_kind == PE_AMOUNT_MINIMUM)
                snprintf(label, PE_PREFLOP_ALLIN_MAX_ACTION_LABEL, "MIN-RAISE");
            else
                snprintf(label, PE_PREFLOP_ALLIN_MAX_ACTION_LABEL,
                         "RAISE %.2f", a->amount);
        }
        else
            snprintf(label, PE_PREFLOP_ALLIN_MAX_ACTION_LABEL, "%s",
                     pe_action_kind_string(a->kind));
    }
    snprintf(game->descs[game->desc_count].text, PREFLOP_DESC_TEXT,
             "P%d hand=%s node=%d pot=%.1f tocall=%.1f bet=%.1f raises=%d actions=", betting->to_act,
             game->descs[game->desc_count].hand, state->tree_node_index,
             betting->pot, betting->to_call, betting->current_bet,
             (int)betting->raises_made);
    {
        size_t used = strlen(game->descs[game->desc_count].text);
        for (uint16_t action = 0u; action < action_count && used + 2u < PREFLOP_DESC_TEXT; ++action)
        {
            int written = snprintf(game->descs[game->desc_count].text + used,
                                   PREFLOP_DESC_TEXT - used, "%s%s",
                                   action ? "|" : "",
                                   game->descs[game->desc_count].actions[action]);
            if (written < 0 || (size_t)written >= PREFLOP_DESC_TEXT - used)
                break;
            used += (size_t)written;
        }
    }
    ++game->desc_count;
}

static uint64_t preflop_op_infoset_key(const pe_preflop_betting_state_t *state,
                                       void *user)
{
    pe_preflop_allin_game_t *game = user;
    const pe_betting_state_t *betting = &state->betting;
    uint64_t hash = PREFLOP_FNV_OFFSET;
    int actor = betting->to_act;
    uint64_t masks = 0u;
    int player;

    hash = preflop_mix_u64(hash, (uint64_t)actor);
    hash = preflop_mix_u64(hash, (uint64_t)state->street);
    hash = preflop_mix_u64(hash, (uint64_t)state->board);
    hash = preflop_mix_u64(hash, (uint64_t)betting->raises_made);
    for (player = 0; player < PE_PREFLOP_ALLIN_MAX_PLAYERS; ++player)
    {
        if (betting->active[player])
            masks |= 1u << player;
        if (betting->all_in[player])
            masks |= 1u << (player + PE_PREFLOP_ALLIN_MAX_PLAYERS);
    }
    hash = preflop_mix_u64(hash, masks);
    hash = preflop_mix_u64(hash, preflop_quantize(betting->pot));
    hash = preflop_mix_u64(hash, preflop_quantize(betting->to_call));
    hash = preflop_mix_u64(hash, preflop_quantize(betting->current_bet));
    for (player = 0; player < betting->player_count; ++player)
        hash = preflop_mix_u64(
            hash, preflop_quantize(betting->round_contrib[player]));
    if (actor >= 0)
        hash = preflop_mix_u64(hash, (uint64_t)state->holes[actor]);
    preflop_record_desc(game, hash, state);
    return hash;
}

/* ------------------------------------------------------------------ *
 * Terminals
 * ------------------------------------------------------------------ */

static int preflop_evaluate_board(const pe_preflop_allin_game_t *game,
                                  mask_t holes, mask_t board, eval_t *out)
{
    if (!game || !out || mask_popcount(board) != 5 ||
        (holes & board) != MASK_EMPTY)
        return -1;
    if (game->rules.variant == PE_PREFLOP_HOLDEM)
    {
        *out = pe_eval_7c(game->eval_ctx, holes | board);
        return 0;
    }
    {
        HandVal value = HandVal_NOTHING;
        if (StdDeck_OmahaHi_EVAL(mask_t_to_cardmask(holes),
                                 mask_t_to_cardmask(board), &value) != 0)
            return -1;
        *out = (eval_t)value;
    }
    return 0;
}

int pe_preflop_allin_showdown_equity(const pe_preflop_allin_game_t *game,
                                     const mask_t *holes, double *out_equity)
{
    pe_rng_t rng;
    uint64_t seed;
    mask_t dead = MASK_EMPTY;
    int players;
    int sample;

    if (!game || !holes || !out_equity)
        return -1;
    players = game->rules.player_count;
    seed = game->rules.showdown_seed;
    for (int player = 0; player < players; ++player)
    {
        uint8_t required_hole_cards = game->rules.variant == PE_PREFLOP_HOLDEM
            ? 2u
            : game->rules.variant == PE_PREFLOP_PLO4
                ? 4u
                : game->rules.variant == PE_PREFLOP_PLO5 ? 5u : 6u;
        if (mask_popcount(holes[player]) != required_hole_cards)
            return -1;
        if (dead & holes[player])
            return -1;
        dead |= holes[player];
        seed = pe_rng_mix(seed ^ (uint64_t)holes[player]);
    }
    pe_rng_seed(&rng, seed);
    for (int player = 0; player < players; ++player)
        out_equity[player] = 0.0;

    for (sample = 0; sample < game->rules.showdown_samples; ++sample)
    {
        mask_t board = MASK_EMPTY;
        mask_t used = dead;
        eval_t values[PE_PREFLOP_ALLIN_MAX_PLAYERS];
        eval_t best = EVAL_INVALID;
        int cards = 0;
        int winners = 0;
        int player;

        while (cards < 5)
        {
            int card = (int)pe_rng_below(&rng, 52u);
            mask_t bit = ((mask_t)1) << card;
            if (used & bit)
                continue;
            board |= bit;
            used |= bit;
            ++cards;
        }
        for (player = 0; player < players; ++player)
        {
            if (preflop_evaluate_board(game, holes[player], board,
                                       &values[player]) != 0)
                return -1;
            if (values[player] > best)
                best = values[player];
        }
        for (player = 0; player < players; ++player)
            if (values[player] == best)
                ++winners;
        if (winners > 0)
            for (player = 0; player < players; ++player)
                if (values[player] == best)
                    out_equity[player] += 1.0 / (double)winners;
    }
    for (int player = 0; player < players; ++player)
        out_equity[player] /= (double)game->rules.showdown_samples;
    return 0;
}

static int preflop_has_actionable_player(const pe_betting_state_t *betting)
{
    if (!betting)
        return 0;
    for (int player = 0; player < betting->player_count; ++player)
        if (betting->active[player] && !betting->all_in[player])
            return 1;
    return 0;
}

static int preflop_first_actionable_player(const pe_betting_state_t *betting)
{
    if (!betting)
        return -1;
    for (int player = 0; player < betting->player_count; ++player)
        if (betting->active[player] && !betting->all_in[player])
            return player;
    return -1;
}

static double preflop_known_board_value(const pe_preflop_allin_game_t *game,
                                        const pe_preflop_betting_state_t *state,
                                        int player)
{
    const pe_betting_state_t *betting = &state->betting;
    double levels[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    double payout[PE_PREFLOP_ALLIN_MAX_PLAYERS] = {0.0};
    eval_t values[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    int level_count = 0;
    int players = betting->player_count;

    for (int p = 0; p < players; ++p)
    {
        if (preflop_evaluate_board(game, state->holes[p], state->board,
                                   &values[p]) != 0)
            return 0.0;
        if (betting->invested[p] > 0.0)
            levels[level_count++] = betting->invested[p];
    }
    for (int i = 0; i < level_count; ++i)
        for (int j = i + 1; j < level_count; ++j)
            if (levels[j] < levels[i]) {
                double temp = levels[i]; levels[i] = levels[j]; levels[j] = temp;
            }

    int unique = 0;
    for (int i = 0; i < level_count; ++i) {
        double level = levels[i];
        double previous = unique > 0 ? levels[unique - 1] : 0.0;
        if (unique > 0 && level <= previous + PREFLOP_EPSILON)
            continue;
        levels[unique++] = level;
        double layer = level - previous;
        double pot = 0.0;
        eval_t best = EVAL_INVALID;
        int winners = 0;
        for (int p = 0; p < players; ++p)
            if (betting->invested[p] + PREFLOP_EPSILON >= level)
                pot += layer;
        for (int p = 0; p < players; ++p)
            if (betting->active[p] && betting->invested[p] + PREFLOP_EPSILON >= level) {
                if (values[p] > best) { best = values[p]; winners = 1; }
                else if (values[p] == best) ++winners;
            }
        if (winners > 0)
            for (int p = 0; p < players; ++p)
                if (betting->active[p] && betting->invested[p] + PREFLOP_EPSILON >= level &&
                    values[p] == best)
                    payout[p] += pot / (double)winners;
    }
    return payout[player] - betting->invested[player];
}

static double preflop_op_terminal_value(const pe_preflop_betting_state_t *state,
                                        int player, void *user)
{
    const pe_preflop_allin_game_t *game = user;
    const pe_betting_state_t *betting = &state->betting;
    double contrib[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    double pot_total = 0.0;
    int players = betting->player_count;

    for (int p = 0; p < players; ++p) {
        contrib[p] = betting->invested[p];
        pot_total += contrib[p];
    }
    if ((game->rules.postflop_streets || game->rules.tree_showdown) &&
        state->betting.terminal &&
        state->street == PE_HOLDEM_RIVER)
        return preflop_known_board_value(game, state, player);
    if (betting->terminal)
    {
        int winner = betting->winner;
        if (winner < 0)
            return 0.0; /* unreachable fold-out without a winner */
        return player == winner ? pot_total - contrib[player] : -contrib[player];
    }
    {
        double equity[PE_PREFLOP_ALLIN_MAX_PLAYERS];
        if (pe_preflop_allin_showdown_equity(game, state->holes, equity) != 0)
            return 0.0;
        return equity[player] * pot_total - contrib[player];
    }
}

static int preflop_is_terminal(const pe_preflop_betting_state_t *state,
                               void *user)
{
    const pe_preflop_allin_game_t *game = user;
    if (!state || !game)
        return 0;
    if (!game->rules.postflop_streets && !game->rules.tree_showdown)
        return state->betting.terminal || state->betting.round_complete;
    return state->betting.terminal;
}

static int preflop_after_action(const pe_preflop_betting_state_t *source,
                                const pe_action_t *action,
                                pe_preflop_betting_state_t *child, void *user)
{
    pe_preflop_allin_game_t *game = user;
    if (!game || !child)
        return 0;
    if (game->rules.tree && source->street == PE_HOLDEM_PREFLOP &&
        source->tree_node_index >= 0)
    {
        const mpf_tree_node_t *node = preflop_tree_node(game, source);
        int next_index = -1;
        if (node)
        {
            for (int i = 0; i < node->action_count; ++i)
            {
                pe_action_t candidate;
                if (tree_action_to_semantic(node, i, &candidate) != 0)
                    continue;
                if (candidate.kind == PE_ACTION_CALL &&
                    source->betting.to_call <= PREFLOP_EPSILON)
                    candidate.kind = PE_ACTION_CHECK;
                if (tree_action_matches(action, &candidate))
                {
                    next_index = node->actions[i].next_index;
                    break;
                }
            }
        }
        child->tree_node_index = next_index;
        if (game->rules.tree_showdown && !child->betting.terminal &&
            (next_index < 0 || next_index >= game->rules.tree->node_count ||
             game->rules.tree->nodes[next_index].type == MPF_TREE_NODE_TERMINAL))
        {
            child->betting.round_complete = 1;
            child->betting.to_act = -1;
            child->is_chance = 1;
            child->tree_node_index = -1;
            return 0;
        }
    }
    if (!game->rules.postflop_streets && !game->rules.tree_showdown)
        return 0;
    if (game->rules.tree_showdown && child->is_chance)
        return 0;
    if (child->betting.terminal)
        return 0;
    if (!child->betting.round_complete)
        return 0;
    if (child->street == PE_HOLDEM_RIVER) {
        child->betting.terminal = 1;
        child->betting.to_act = -1;
        return 0;
    }
    child->is_chance = 1;
    return 0;
}

static int preflop_draw_public_cards(const pe_preflop_betting_state_t *source,
                                     pe_rng_t *rng, mask_t *out_board)
{
    int available[52];
    int count = 0;
    uint8_t added;
    mask_t used;

    if (!source || !rng || !out_board || source->street == PE_HOLDEM_SHOWDOWN)
        return -1;
    added = pe_holdem_next_public_count(source->street);
    used = source->board | source->dead_cards;
    for (int card = 0; card < 52; ++card) {
        mask_t bit = ((mask_t)1) << card;
        if (!(used & bit))
            available[count++] = card;
    }
    if (count < added || added == 0u)
        return -1;
    *out_board = source->board;
    for (uint8_t i = 0u; i < added; ++i) {
        uint32_t index = pe_rng_below(rng, (uint32_t)(count - (int)i));
        int card = available[index];
        *out_board |= ((mask_t)1) << card;
        available[index] = available[count - 1 - (int)i];
    }
    return 0;
}

static int preflop_chance_child(const pe_preflop_betting_state_t *source,
                                pe_rng_t *rng, pe_chance_sample_t *sample,
                                pe_preflop_betting_state_t *child, void *user)
{
    pe_preflop_allin_game_t *game = user;
    if (!source || !rng || !sample || !child || !game)
        return -1;
    if (game->rules.tree_showdown &&
        ((source->street != PE_HOLDEM_PREFLOP &&
          source->street != PE_HOLDEM_RIVER) ||
         (source->street == PE_HOLDEM_PREFLOP &&
          source->dead_cards != MASK_EMPTY)))
    {
        mask_t next_board;
        pe_holdem_street_t next_street;
        if (preflop_draw_public_cards(source, rng, &next_board) != 0 ||
            pe_holdem_street_from_board(next_board, &next_street) != 0)
            return -1;
        *child = *source;
        child->board = next_board;
        child->street = next_street;
        child->is_chance = next_street != PE_HOLDEM_RIVER;
        child->betting.to_act = -1;
        child->betting.round_complete = 1;
        child->betting.terminal = next_street == PE_HOLDEM_RIVER;
        sample->outcome = 0;
        sample->importance_ratio = 1.0;
        return 0;
    }
    if (source->street == PE_HOLDEM_PREFLOP && source->dead_cards == MASK_EMPTY)
    {
        pe_preflop_deal_sample_t deal;
        if (pe_preflop_deal_sampler_sample(&game->sampler, rng, &deal) != 0)
            return -1;
        child->betting = source->betting;
        child->is_chance = 0;
        memcpy(child->holes, deal.holes, sizeof(child->holes));
        child->board = MASK_EMPTY;
        child->dead_cards = MASK_EMPTY;
        for (int p = 0; p < game->rules.player_count; ++p)
            child->dead_cards |= deal.holes[p];
        child->street = PE_HOLDEM_PREFLOP;
        sample->outcome = 0;
        sample->importance_ratio = deal.importance_ratio;
        return 0;
    }
    {
        mask_t next_board;
        pe_holdem_round_state_t round;
        pe_holdem_round_state_t next;
        pe_holdem_street_t next_street;
        int first_to_act;
        if (preflop_draw_public_cards(source, rng, &next_board) != 0 ||
            pe_holdem_street_from_board(next_board, &next_street) != 0)
            return -1;
        memset(&round, 0, sizeof(round));
        round.board = source->board;
        round.dead_cards = source->dead_cards;
        round.street = source->street;
        round.betting = source->betting;
        first_to_act = preflop_first_actionable_player(&source->betting);
        if (first_to_act < 0)
            first_to_act = 0;
        if (pe_holdem_round_advance(&round, &game->betting_rules, next_board,
                                    first_to_act, &next) != PE_HOLDEM_ROUND_OK)
            return -1;
        *child = *source;
        child->betting = next.betting;
        child->board = next_board;
        child->street = next_street;
        child->is_chance = 0;
        if (next_street == PE_HOLDEM_RIVER &&
            !preflop_has_actionable_player(&child->betting))
            child->betting.terminal = 1;
        else if (!preflop_has_actionable_player(&child->betting))
        {
            child->betting.round_complete = 1;
            child->is_chance = 1;
        }
        sample->outcome = 0;
        sample->importance_ratio = 1.0;
        return 0;
    }
}

/* ------------------------------------------------------------------ *
 * Policy for opponent sampling
 * ------------------------------------------------------------------ */

static double preflop_action_probability(const void *state, uint64_t key,
                                         uint16_t action, void *user)
{
    const pe_preflop_betting_game_t *wrapper = user;
    pe_preflop_allin_game_t *game = wrapper->user;
    const pe_preflop_betting_state_t *preflop_state = state;
    uint16_t count = preflop_enumerate(game, preflop_state, NULL,
                                       PREFLOP_MAX_ACTIONS);

    if (count == 0u || action >= count)
        return 0.0;
    if (!game->storage)
        return 1.0 / (double)count;
    {
        pe_infoset_id_t id = pe_storage_find(game->storage, key);
        const double *regrets;
        double positive_sum = 0.0;
        if (id == PE_INFOSET_ID_INVALID)
            return 1.0 / (double)count;
        regrets = pe_storage_values_const(game->storage, id, PE_VALUES_REGRET);
        if (!regrets)
            return 1.0 / (double)count;
        for (uint16_t i = 0u; i < count; ++i)
            if (regrets[i] > 0.0)
                positive_sum += regrets[i];
        if (positive_sum <= 0.0)
            return 1.0 / (double)count;
        return regrets[action] > 0.0 ? regrets[action] / positive_sum : 0.0;
    }
}

/* ------------------------------------------------------------------ *
 * Lifecycle
 * ------------------------------------------------------------------ */

pe_preflop_allin_game_t *pe_preflop_allin_game_create(
    const pe_preflop_allin_rules_t *rules, pe_range_t *const *ranges)
{
    pe_preflop_allin_game_t *game;
    double stacks_after[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    double posts[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    int player;

    if (!rules || !ranges || rules->player_count < 2 ||
        rules->player_count > PE_PREFLOP_ALLIN_MAX_PLAYERS ||
        rules->variant < PE_PREFLOP_HOLDEM ||
        rules->variant > PE_PREFLOP_PLO6 ||
        rules->showdown_samples <= 0 ||
        !(rules->small_blind > 0.0) || !(rules->big_blind > 0.0) ||
        rules->big_blind < rules->small_blind ||
        !isfinite(rules->ante) || rules->ante < 0.0 ||
        rules->ante >= rules->big_blind ||
        rules->raise_count < 0 || rules->raise_count > PE_PREFLOP_ALLIN_MAX_RAISE_SIZES ||
        !(rules->min_raise > 0.0))
        return NULL;
    for (player = 0; player < rules->player_count; ++player)
    {
        if (!ranges[player] || !ranges[player]->combos ||
            ranges[player]->count == 0 || !(rules->stacks[player] > 0.0))
            return NULL;
    }

    game = calloc(1u, sizeof(*game));
    if (!game)
        return NULL;
    game->rules = *rules;
    game->ranges = ranges;

    /* Convert prepared ranges to mask-based combos for the deal sampler. */
    for (player = 0; player < rules->player_count; ++player)
    {
        pe_range_view_t view = pe_solver_range_view(ranges[player]);
        unsigned required_cards = rules->variant == PE_PREFLOP_HOLDEM ? 2u :
            rules->variant == PE_PREFLOP_PLO4 ? 4u :
            rules->variant == PE_PREFLOP_PLO5 ? 5u : 6u;
        for (size_t i = 0u; i < view.count; ++i) {
            if (preflop_card_count(view.combos[i].hand) != required_cards) {
                pe_preflop_allin_game_destroy(game);
                return NULL;
            }
        }
        if (rules->variant == PE_PREFLOP_HOLDEM)
        {
            pe_holdem_combo_t *combos = calloc(view.count, sizeof(*combos));
            if (!combos)
            {
                pe_preflop_allin_game_destroy(game);
                return NULL;
            }
            for (size_t i = 0u; i < view.count; ++i)
            {
                combos[i].cards = cardmask_to_mask_t(view.combos[i].hand);
                combos[i].weight = view.combos[i].weight;
            }
            game->holdem_combo_owned[player] = combos;
            game->holdem_ranges[player].combos = combos;
            game->holdem_ranges[player].count = view.count;
        }
        else
        {
            pe_omaha_combo_t *combos = calloc(view.count, sizeof(*combos));
            if (!combos)
            {
                pe_preflop_allin_game_destroy(game);
                return NULL;
            }
            for (size_t i = 0u; i < view.count; ++i)
            {
                combos[i].cards = cardmask_to_mask_t(view.combos[i].hand);
                combos[i].weight = view.combos[i].weight;
            }
            game->omaha_combo_owned[player] = combos;
            game->omaha_ranges[player].combos = combos;
            game->omaha_ranges[player].count = view.count;
        }
    }

    if ((rules->variant == PE_PREFLOP_HOLDEM &&
         pe_preflop_deal_sampler_init_holdem(
             &game->sampler, MASK_EMPTY, game->holdem_ranges,
             (uint8_t)rules->player_count) != 0) ||
        (rules->variant != PE_PREFLOP_HOLDEM &&
         pe_preflop_deal_sampler_init_omaha(
             &game->sampler, MASK_EMPTY, game->omaha_ranges,
             (uint8_t)rules->player_count,
             rules->variant == PE_PREFLOP_PLO4 ? 4u :
             rules->variant == PE_PREFLOP_PLO5 ? 5u : 6u) != 0))
    {
        pe_preflop_allin_game_destroy(game);
        return NULL;
    }

    /* Forced bets: blinds plus an optional ante for every later seat. */
    posts[0] = rules->small_blind;
    posts[1] = rules->big_blind;
    for (player = 0; player < rules->player_count; ++player)
    {
        if (player >= 2)
            posts[player] = rules->ante;
        if (posts[player] >= rules->stacks[player])
        {
            pe_preflop_allin_game_destroy(game);
            return NULL;
        }
        stacks_after[player] = rules->stacks[player] - posts[player];
    }

    pe_betting_rules_default(&game->betting_rules, (uint8_t)rules->player_count);
    game->betting_rules.epsilon = PREFLOP_EPSILON;
    game->betting_rules.min_raise = rules->min_raise;
    game->betting_rules.pot_limit = 0;
    game->betting_rules.raise_cap = rules->raise_cap;

    memset(&game->root_betting, 0, sizeof(game->root_betting));
    game->root_betting.tree_node_index = rules->tree
        ? rules->tree->root_index : -1;
    /* Heads-up starts at the small blind. Multiway starts at the first player
       after the blinds/antes, which is the preflop UTG abstraction. */
    if (pe_betting_state_init(&game->root_betting.betting, &game->betting_rules,
                              stacks_after, (uint8_t)rules->player_count,
                              rules->player_count > 2 ? 2 : 0,
                              posts[0] + posts[1],
                              rules->big_blind - posts[rules->player_count > 2 ? 2 : 0]) != PE_BETTING_OK)
    {
        pe_preflop_allin_game_destroy(game);
        return NULL;
    }
    game->root_betting.betting.current_bet = rules->big_blind;
    for (player = 0; player < rules->player_count; ++player)
    {
        game->root_betting.betting.round_contrib[player] = posts[player];
        game->root_betting.betting.invested[player] = posts[player];
    }
    game->root_betting.betting.pot = 0.0;
    for (player = 0; player < rules->player_count; ++player)
        game->root_betting.betting.pot += posts[player];

    game->ops.action_count = preflop_op_action_count;
    game->ops.action_at = preflop_op_action_at;
    game->ops.infoset_key = preflop_op_infoset_key;
    game->ops.terminal_value = preflop_op_terminal_value;
    game->ops.is_terminal = preflop_is_terminal;
    game->ops.after_action = preflop_after_action;
    game->ops.chance_child = preflop_chance_child;

    if (pe_preflop_betting_game_init(&game->betting_game, &game->sampler,
                                     &game->betting_rules, &game->root_betting,
                                     &game->ops, game) != 0)
    {
        pe_preflop_allin_game_destroy(game);
        return NULL;
    }

    game->external = *pe_preflop_betting_external(&game->betting_game);
    game->external.action_probability = preflop_action_probability;

    {
        EvalConfig config = rules->variant == PE_PREFLOP_HOLDEM
            ? eval_config_holdem() : eval_config_omaha();
        game->eval_ctx = eval_context_create(&config);
        if (!game->eval_ctx)
        {
            pe_preflop_allin_game_destroy(game);
            return NULL;
        }
    }
    return game;
}

void pe_preflop_allin_game_destroy(pe_preflop_allin_game_t *game)
{
    int player;
    if (!game)
        return;
    pe_preflop_betting_game_destroy(&game->betting_game);
    if (game->eval_ctx)
        eval_context_destroy(game->eval_ctx);
    for (player = 0; player < PE_PREFLOP_ALLIN_MAX_PLAYERS; ++player)
    {
        free(game->holdem_combo_owned[player]);
        free(game->omaha_combo_owned[player]);
    }
    free(game->descs);
    free(game);
}

const pe_external_game_t *pe_preflop_allin_external(
    const pe_preflop_allin_game_t *game)
{
    return game ? &game->external : NULL;
}

int pe_preflop_allin_player_count(const pe_preflop_allin_game_t *game)
{
    return game ? game->rules.player_count : 0;
}

void pe_preflop_allin_game_set_storage(pe_preflop_allin_game_t *game,
                                       pe_storage_t *storage)
{
    if (game)
        game->storage = storage;
}

size_t pe_preflop_allin_infodesc_count(const pe_preflop_allin_game_t *game)
{
    return game ? game->desc_count : 0u;
}

int pe_preflop_allin_infodesc_at(const pe_preflop_allin_game_t *game,
                                 size_t index, uint64_t *out_key, char *out_text,
                                 size_t text_capacity)
{
    if (!game || index >= game->desc_count || !out_key || !out_text ||
        text_capacity == 0u)
        return -1;
    *out_key = game->descs[index].key;
    snprintf(out_text, text_capacity, "%s", game->descs[index].text);
    return 0;
}

int pe_preflop_allin_infodesc_view_at(
    const pe_preflop_allin_game_t *game, size_t index,
    pe_preflop_infodesc_view_t *out)
{
    if (!game || !out || index >= game->desc_count)
        return -1;
    memset(out, 0, sizeof(*out));
    out->key = game->descs[index].key;
    out->actor = game->descs[index].actor;
    out->tree_node_index = game->descs[index].tree_node_index;
    out->pot = game->descs[index].pot;
    out->to_call = game->descs[index].to_call;
    snprintf(out->hand, sizeof(out->hand), "%s", game->descs[index].hand);
    snprintf(out->context, sizeof(out->context), "%s", game->descs[index].text);
    out->action_count = game->descs[index].action_count;
    if (out->action_count > PE_PREFLOP_ALLIN_MAX_DESC_ACTIONS)
        out->action_count = PE_PREFLOP_ALLIN_MAX_DESC_ACTIONS;
    for (uint16_t action = 0u; action < out->action_count; ++action)
        snprintf(out->actions[action], PE_PREFLOP_ALLIN_MAX_ACTION_LABEL, "%s",
                 game->descs[index].actions[action]);
    return 0;
}

int pe_preflop_allin_infodesc_state_at(
    const pe_preflop_allin_game_t *game, size_t index,
    pe_preflop_betting_state_t *out)
{
    if (!game || !out || index >= game->desc_count)
        return -1;
    *out = game->descs[index].state;
    return 0;
}
