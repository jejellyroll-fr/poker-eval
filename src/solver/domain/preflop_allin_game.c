/* preflop_allin_game.c - production preflop game over sampled deals (Lane B)
 *
 * Model: heads-up preflop only (v1). Forced bets are player 0 = small blind,
 * player 1 = big blind. Terminals are fold-outs or called pots resolved as an
 * all-in showdown with boards sampled deterministically per deal, so MCCFR
 * iterations see a stable value for a given deal.
 */

#include <poker_eval/solver/pe_preflop_allin_game.h>

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/solver/pe_range.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFLOP_FNV_OFFSET 0xcbf29ce484222325ULL
#define PREFLOP_FNV_PRIME 0x100000001b3ULL
#define PREFLOP_EPSILON 1e-6
#define PREFLOP_MAX_ACTIONS 16
#define PREFLOP_DESC_TEXT 96

typedef struct
{
    uint64_t key;
    char text[PREFLOP_DESC_TEXT];
} preflop_infodesc_t;

struct pe_preflop_allin_game_t
{
    pe_preflop_allin_rules_t rules;
    pe_range_t *const *ranges;
    pe_holdem_combo_t *combo_owned[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_holdem_range_t holdem_ranges[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_preflop_deal_sampler_t sampler;
    pe_betting_rules_t betting_rules;
    pe_preflop_betting_state_t root_betting;
    pe_preflop_betting_ops_t ops;
    pe_preflop_betting_game_t betting_game;
    pe_external_game_t external;
    pe_storage_t *storage;
    EvalContext *eval_ctx;
    double initial_contrib[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    preflop_infodesc_t *descs;
    size_t desc_count;
    size_t desc_capacity;
};

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

/* ------------------------------------------------------------------ *
 * Infset identity and descriptions
 * ------------------------------------------------------------------ */

static void preflop_record_desc(pe_preflop_allin_game_t *game, uint64_t key,
                                const pe_preflop_betting_state_t *state)
{
    const pe_betting_state_t *betting = &state->betting;
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
    snprintf(game->descs[game->desc_count].text, PREFLOP_DESC_TEXT,
             "P%d pot=%.1f tocall=%.1f bet=%.1f raises=%d", betting->to_act,
             betting->pot, betting->to_call, betting->current_bet,
             (int)betting->raises_made);
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
        if (mask_popcount(holes[player]) != 2u)
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
            values[player] = pe_eval_7c(game->eval_ctx, holes[player] | board);
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

static double preflop_op_terminal_value(const pe_preflop_betting_state_t *state,
                                        int player, void *user)
{
    const pe_preflop_allin_game_t *game = user;
    const pe_betting_state_t *betting = &state->betting;
    double contrib[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    double pot_total = 0.0;
    int players = betting->player_count;

    for (int p = 0; p < players; ++p)
    {
        contrib[p] = game->initial_contrib[p] + betting->invested[p];
        pot_total += contrib[p];
    }
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

    if (!rules || !ranges || rules->player_count != 2 ||
        rules->showdown_samples <= 0 ||
        !(rules->small_blind > 0.0) || !(rules->big_blind > 0.0) ||
        rules->big_blind < rules->small_blind ||
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
        pe_holdem_combo_t *combos =
            calloc(view.count, sizeof(*combos));
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
        game->combo_owned[player] = combos;
        game->holdem_ranges[player].combos = combos;
        game->holdem_ranges[player].count = view.count;
    }

    if (pe_preflop_deal_sampler_init_holdem(
            &game->sampler, MASK_EMPTY, game->holdem_ranges,
            (uint8_t)rules->player_count) != 0)
    {
        pe_preflop_allin_game_destroy(game);
        return NULL;
    }

    /* Forced bets (v1: heads-up blinds). */
    posts[0] = rules->small_blind;
    posts[1] = rules->big_blind;
    for (player = 0; player < rules->player_count; ++player)
    {
        if (posts[player] >= rules->stacks[player])
        {
            pe_preflop_allin_game_destroy(game);
            return NULL;
        }
        game->initial_contrib[player] = posts[player];
        stacks_after[player] = rules->stacks[player] - posts[player];
    }

    pe_betting_rules_default(&game->betting_rules, (uint8_t)rules->player_count);
    game->betting_rules.epsilon = PREFLOP_EPSILON;
    game->betting_rules.min_raise = rules->min_raise;
    game->betting_rules.pot_limit = 0;
    game->betting_rules.raise_cap = rules->raise_cap;

    memset(&game->root_betting, 0, sizeof(game->root_betting));
    /* Heads-up: the small blind acts first and must complete the big blind. */
    if (pe_betting_state_init(&game->root_betting.betting, &game->betting_rules,
                              stacks_after, (uint8_t)rules->player_count, 0,
                              posts[0] + posts[1],
                              posts[1] - posts[0]) != PE_BETTING_OK)
    {
        pe_preflop_allin_game_destroy(game);
        return NULL;
    }

    game->ops.action_count = preflop_op_action_count;
    game->ops.action_at = preflop_op_action_at;
    game->ops.infoset_key = preflop_op_infoset_key;
    game->ops.terminal_value = preflop_op_terminal_value;

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
        EvalConfig config = eval_config_holdem();
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
        free(game->combo_owned[player]);
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
