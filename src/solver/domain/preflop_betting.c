/* preflop_betting.c - sampled private deals + one complete betting street. */

#include <poker_eval/solver/pe_preflop_betting.h>

#include <stdlib.h>
#include <string.h>

static int is_terminal(const void *state, void *user)
{
    const pe_preflop_betting_state_t *current = state;
    (void)user;
    return !current->is_chance &&
           (current->betting.terminal || current->betting.round_complete);
}

static int acting_player(const void *state, void *user)
{
    const pe_preflop_betting_state_t *current = state;
    (void)user;
    return current->is_chance ? -1 : current->betting.to_act;
}

static uint16_t action_count(const void *state, void *user)
{
    pe_preflop_betting_game_t *game = user;
    const pe_preflop_betting_state_t *current = state;
    if (current->is_chance || is_terminal(state, user)) return 0u;
    return game->ops.action_count(current, game->user);
}

static uint64_t infoset_key(const void *state, void *user)
{
    pe_preflop_betting_game_t *game = user;
    return game->ops.infoset_key(state, game->user);
}

static int own_state(pe_preflop_betting_game_t *game,
                     pe_preflop_betting_state_t *state)
{
    if (game->owned_count == game->owned_capacity)
    {
        size_t capacity = game->owned_capacity ? game->owned_capacity * 2u : 16u;
        pe_preflop_betting_state_t **grown = realloc(
            game->owned_states, capacity * sizeof(*grown));
        if (!grown) return -1;
        game->owned_states = grown;
        game->owned_capacity = capacity;
    }
    game->owned_states[game->owned_count++] = state;
    return 0;
}

static const void *apply_action(const void *state, uint16_t action, void *user)
{
    pe_preflop_betting_game_t *game = user;
    const pe_preflop_betting_state_t *source = state;
    pe_preflop_betting_state_t *child;
    pe_action_t semantic;
    if (source->is_chance || action >= action_count(state, user) ||
        game->ops.action_at(source, action, &semantic, game->user) != PE_ACTION_OK)
        return NULL;
    child = calloc(1u, sizeof(*child));
    if (!child || pe_betting_apply_action(&source->betting, &game->rules,
                                          &semantic, &child->betting) !=
                    PE_BETTING_OK || own_state(game, child) != 0)
    {
        free(child);
        return NULL;
    }
    memcpy(child->holes, source->holes, sizeof(child->holes));
    return child;
}

static double terminal_value(const void *state, int player, void *user)
{
    pe_preflop_betting_game_t *game = user;
    return game->ops.terminal_value(state, player, game->user);
}

static const void *sample_chance_child(const void *state, pe_rng_t *rng,
                                       pe_chance_sample_t *out, void *user)
{
    pe_preflop_betting_game_t *game = user;
    pe_preflop_deal_sample_t sample;
    pe_preflop_betting_state_t *child;
    const pe_preflop_betting_state_t *source = state;
    if (!source->is_chance || pe_preflop_deal_sampler_sample(
            &game->sampler, rng, &sample) != 0)
        return NULL;
    child = calloc(1u, sizeof(*child));
    if (!child || own_state(game, child) != 0)
    {
        free(child);
        return NULL;
    }
    child->betting = source->betting;
    child->is_chance = 0;
    memcpy(child->holes, sample.holes, sizeof(child->holes));
    out->outcome = 0;
    out->importance_ratio = sample.importance_ratio;
    return child;
}

int pe_preflop_betting_game_init(
    pe_preflop_betting_game_t *out,
    const pe_preflop_deal_sampler_t *sampler,
    const pe_betting_rules_t *rules,
    const pe_preflop_betting_state_t *root_betting,
    const pe_preflop_betting_ops_t *ops,
    void *user)
{
    if (!out || !sampler || !rules || !root_betting || !ops ||
        !ops->action_count || !ops->action_at || !ops->infoset_key ||
        !ops->terminal_value ||
        sampler->player_count != root_betting->betting.player_count ||
        pe_betting_state_validate(&root_betting->betting, rules) != PE_BETTING_OK)
        return -1;
    memset(out, 0, sizeof(*out));
    out->sampler = *sampler;
    out->rules = *rules;
    out->root = *root_betting;
    out->root.is_chance = 1;
    out->ops = *ops;
    out->user = user;
    out->game.root = &out->root;
    out->game.user = out;
    out->game.player_count = root_betting->betting.player_count;
    out->game.is_terminal = is_terminal;
    out->game.acting_player = acting_player;
    out->game.action_count = action_count;
    out->game.infoset_key = infoset_key;
    out->game.apply_action = apply_action;
    out->game.terminal_value = terminal_value;
    out->game.sample_chance_child = sample_chance_child;
    return 0;
}

void pe_preflop_betting_game_destroy(pe_preflop_betting_game_t *game)
{
    if (!game) return;
    for (size_t i = 0u; i < game->owned_count; ++i)
        free(game->owned_states[i]);
    free(game->owned_states);
    memset(game, 0, sizeof(*game));
}

const pe_external_game_t *pe_preflop_betting_external(
    const pe_preflop_betting_game_t *game)
{
    return game ? &game->game : NULL;
}
