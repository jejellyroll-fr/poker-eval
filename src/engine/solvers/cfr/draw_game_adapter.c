#include <poker_eval/engine/solvers/cfr/draw_game_adapter.h>

#include <stdlib.h>
#include <string.h>

static pe_draw_cfr_state_t *state(uint64_t key)
{
    return (pe_draw_cfr_state_t *)(uintptr_t)key;
}

static int draw_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *st = state(key);
    return st->phase == 0 ? 0 : st->phase == 2 ? 1 : -1;
}

static int draw_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *st = state(key);
    return st->phase == 4;
}

static double draw_get_utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *st = state(key);
    return st->config->terminal_value(st->hands[0], st->hands[1], player,
                                      st->config->terminal_user_data);
}

static int draw_get_actions(cfr_game_t *game, uint64_t key, int *out, int max, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *st = state(key);
    int player = st->phase == 0 ? 0 : st->phase == 2 ? 1 : -1;
    if (player < 0) return 0;
    int count = st->config->action_count[player];
    if (count > max) count = max;
    for (int i = 0; i < count; ++i) out[i] = i;
    return count;
}

static uint64_t draw_apply_action(cfr_game_t *game, uint64_t key, int action, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *old = state(key);
    int player = old->phase == 0 ? 0 : old->phase == 2 ? 1 : -1;
    if (player < 0 || action < 0 || action >= old->config->action_count[player]) return 0;
    pe_draw_cfr_state_t *next = (pe_draw_cfr_state_t *)malloc(sizeof(*next));
    if (!next) return 0;
    *next = *old;
    next->discard = old->config->discard_actions[player][action];
    if (pe_draw_chance_init(&next->chance, old->config->variant,
                            old->hands[player], next->discard) != 0) {
        free(next); return 0;
    }
    next->phase = player == 0 ? 1 : 3;
    return (uint64_t)(uintptr_t)next;
}

static void draw_release_state(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    free(state(key));
}

static int draw_is_chance(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *st = state(key);
    return st->phase == 1 || st->phase == 3;
}

static int draw_get_chance_outcomes(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *st = state(key);
    return (int)pe_draw_chance_outcome_count(&st->chance);
}

static uint64_t draw_apply_chance(cfr_game_t *game, uint64_t key, int outcome, void *user)
{
    (void)game; (void)user;
    pe_draw_cfr_state_t *old = state(key);
    pe_draw_cfr_state_t *next = (pe_draw_cfr_state_t *)malloc(sizeof(*next));
    mask_t hand;
    if (!next || pe_draw_chance_apply(&old->chance, (uint64_t)outcome, &hand) != 0) {
        free(next); return 0;
    }
    *next = *old;
    next->hands[old->phase == 1 ? 0 : 1] = hand;
    next->phase = old->phase == 1 ? 2 : 4;
    next->discard = MASK_EMPTY;
    return (uint64_t)(uintptr_t)next;
}

static uint64_t draw_infoset_key(const void *raw)
{
    const pe_draw_cfr_state_t *st = (const pe_draw_cfr_state_t *)raw;
    int player = st->phase == 0 ? 0 : st->phase == 2 ? 1 : 0;
    uint64_t key = 1469598103934665603ULL;
    key ^= (uint64_t)(unsigned)st->phase; key *= 1099511628211ULL;
    key ^= (uint64_t)st->hands[player]; key *= 1099511628211ULL;
    key ^= (uint64_t)st->discard; key *= 1099511628211ULL;
    return key;
}

int pe_draw_cfr_build_game(const pe_draw_cfr_config_t *config,
                           cfr_game_t *out_game,
                           pe_draw_cfr_state_t *out_state)
{
    if (!config || !out_game || !out_state || !config->terminal_value ||
        config->action_count[0] < 1 || config->action_count[0] > PE_DRAW_CFR_MAX_ACTIONS ||
        config->action_count[1] < 1 || config->action_count[1] > PE_DRAW_CFR_MAX_ACTIONS ||
        !mask_is_valid(config->player0_hand) || !mask_is_valid(config->player1_hand) ||
        (config->player0_hand & config->player1_hand) != MASK_EMPTY)
        return -1;
    memset(out_game, 0, sizeof(*out_game));
    memset(out_state, 0, sizeof(*out_state));
    out_state->config = config;
    out_state->hands[0] = config->player0_hand;
    out_state->hands[1] = config->player1_hand;
    out_state->phase = 0;
    out_game->current_player = draw_current_player;
    out_game->is_terminal = draw_is_terminal;
    out_game->get_utility = draw_get_utility;
    out_game->get_actions = draw_get_actions;
    out_game->apply_action = draw_apply_action;
    out_game->release_state = draw_release_state;
    out_game->get_infoset_key = draw_infoset_key;
    out_game->is_chance = draw_is_chance;
    out_game->get_chance_outcomes = draw_get_chance_outcomes;
    out_game->apply_chance = draw_apply_chance;
    out_game->game_data = NULL;
    out_game->initial_state = out_state;
    out_game->state_size = sizeof(*out_state);
    out_game->num_players = 2;
    return 0;
}
