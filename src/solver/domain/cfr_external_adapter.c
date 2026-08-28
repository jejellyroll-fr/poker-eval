/* cfr_external_adapter.c - bridge legacy multi-street games to Lane B. */

#include <poker_eval/solver/pe_cfr_external_adapter.h>

#include <float.h>
#include <math.h>
#include <string.h>

/* MinGW's isfinite macro may route a double through the float overload when
 * strict conversion warnings are enabled. Relational bounds reject NaN and
 * both infinities without narrowing the value. */
static int finite_double(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

static cfr_game_t *legacy(void *user)
{
    return ((pe_cfr_external_adapter_t *)user)->legacy;
}

static uint64_t key_of(const void *state)
{
    return (uint64_t)(uintptr_t)state;
}

static int is_terminal(const void *state, void *user)
{
    cfr_game_t *game = legacy(user);
    return game && game->is_terminal(game, key_of(state), game->game_data);
}

static int acting_player(const void *state, void *user)
{
    cfr_game_t *game = legacy(user);
    return game && game->current_player
        ? game->current_player(game, key_of(state), game->game_data) : -1;
}

static uint16_t action_count(const void *state, void *user)
{
    cfr_game_t *game = legacy(user);
    int actions[PE_EXTERNAL_MAX_ACTIONS];
    int count;
    if (!game || !game->get_actions) return 0u;
    count = game->get_actions(game, key_of(state), actions,
                              PE_EXTERNAL_MAX_ACTIONS, game->game_data);
    return count >= 0 && count <= (int)PE_EXTERNAL_MAX_ACTIONS
        ? (uint16_t)count : 0u;
}

static int action_value(cfr_game_t *game, uint64_t key, uint16_t ordinal,
                        int *out_value)
{
    int actions[PE_EXTERNAL_MAX_ACTIONS];
    int count;
    if (!game || !game->get_actions || !out_value) return -1;
    count = game->get_actions(game, key, actions, PE_EXTERNAL_MAX_ACTIONS,
                              game->game_data);
    if (count <= 0 || count > (int)PE_EXTERNAL_MAX_ACTIONS ||
        ordinal >= (uint16_t)count)
        return -1;
    *out_value = actions[ordinal];
    return 0;
}

static uint64_t infoset_key(const void *state, void *user)
{
    cfr_game_t *game = legacy(user);
    return game && game->get_infoset_key
        ? game->get_infoset_key(state) : key_of(state);
}

static const void *apply_action(const void *state, uint16_t action, void *user)
{
    cfr_game_t *game = legacy(user);
    uint64_t child;
    int legacy_action;
    if (!game || !game->apply_action) return NULL;
    if (action_value(game, key_of(state), action, &legacy_action) != 0)
        return NULL;
    child = game->apply_action(game, key_of(state), legacy_action,
                               game->game_data);
    return child == 0u ? NULL : (const void *)(uintptr_t)child;
}

static double terminal_value(const void *state, int player, void *user)
{
    cfr_game_t *game = legacy(user);
    return game && game->get_utility
        ? game->get_utility(game, key_of(state), player, game->game_data)
        : NAN;
}

static int sample_chance(const void *state, pe_rng_t *rng,
                         pe_chance_sample_t *out, void *user)
{
    cfr_game_t *game = legacy(user);
    int outcomes;
    double weights[PE_EXTERNAL_MAX_ACTIONS];
    double total = 0.0;
    double draw;
    int selected = -1;
    if (!game || !game->is_chance || !game->get_chance_outcomes ||
        !game->apply_chance || !rng || !out ||
        !game->is_chance(game, key_of(state), game->game_data))
        return -1;
    outcomes = game->get_chance_outcomes(game, key_of(state), game->game_data);
    if (outcomes <= 0) return -1;

    /* Chance spaces are not action spaces: a flop may have thousands of
       combination indices.  Sample large weighted domains without material-
       izing a weights array, keeping the adapter bounded in memory. */
    if (outcomes > (int)PE_EXTERNAL_MAX_ACTIONS)
    {
        for (int i = 0; i < outcomes; ++i)
        {
            double weight = game->get_chance_weight
                ? game->get_chance_weight(game, key_of(state), i,
                                          game->game_data) : 1.0;
            if (!finite_double(weight) || weight <= 0.0) weight = 1.0;
            total += weight;
        }
        if (!(total > 0.0) || !finite_double(total)) return -1;
        draw = pe_rng_uniform01(rng) * total;
        for (int i = 0; i < outcomes; ++i)
        {
            double weight = game->get_chance_weight
                ? game->get_chance_weight(game, key_of(state), i,
                                          game->game_data) : 1.0;
            if (!finite_double(weight) || weight <= 0.0) weight = 1.0;
            draw -= weight;
            if (draw < 0.0 || i + 1 == outcomes)
            {
                out->outcome = i;
                out->importance_ratio = 1.0;
                return 0;
            }
        }
        return -1;
    }
    for (int i = 0; i < outcomes; ++i)
    {
        double weight = game->get_chance_weight
            ? game->get_chance_weight(game, key_of(state), i, game->game_data)
            : 1.0;
        /* Legacy CFR treats a missing/non-positive weight as uniform. */
        if (!finite_double(weight) || weight <= 0.0) weight = 1.0;
        weights[i] = weight;
        total += weight;
    }
    if (!(total > 0.0) || !finite_double(total)) return -1;
    draw = pe_rng_uniform01(rng) * total;
    for (int i = 0; i < outcomes; ++i)
    {
        draw -= weights[i];
        if (draw < 0.0 || i + 1 == outcomes) { selected = i; break; }
    }
    out->outcome = selected;
    /* The proposal is the game's own chance distribution, so no correction is
       needed.  This is deliberately explicit for the importance-sampling
       contract rather than relying on a zero-initialised struct. */
    out->importance_ratio = 1.0;
    return selected >= 0 ? 0 : -1;
}

static const void *apply_chance(const void *state, int outcome, void *user)
{
    cfr_game_t *game = legacy(user);
    uint64_t child;
    if (!game || !game->apply_chance) return NULL;
    child = game->apply_chance(game, key_of(state), outcome, game->game_data);
    return child == 0u ? NULL : (const void *)(uintptr_t)child;
}

static void release_state(const void *state, void *user)
{
    cfr_game_t *game = legacy(user);
    if (game && game->release_state)
        game->release_state(game, key_of(state), game->game_data);
}

int pe_cfr_external_adapter_init(pe_cfr_external_adapter_t *out,
                                 cfr_game_t *legacy_game)
{
    if (!out || !legacy_game || !legacy_game->initial_state ||
        !legacy_game->is_terminal || !legacy_game->current_player ||
        !legacy_game->get_actions || !legacy_game->apply_action ||
        !legacy_game->get_utility || legacy_game->num_players <= 0 ||
        legacy_game->num_players > PE_TRAVERSAL_MAX_PLAYERS)
        return -1;
    memset(out, 0, sizeof(*out));
    out->legacy = legacy_game;
    out->external.root = legacy_game->initial_state;
    out->external.user = out;
    out->external.player_count = (uint8_t)legacy_game->num_players;
    out->external.is_terminal = is_terminal;
    out->external.acting_player = acting_player;
    out->external.action_count = action_count;
    out->external.infoset_key = infoset_key;
    out->external.apply_action = apply_action;
    out->external.terminal_value = terminal_value;
    out->external.sample_chance_with_user = sample_chance;
    out->external.apply_chance = apply_chance;
    out->external.release_state = release_state;
    return 0;
}

const pe_external_game_t *pe_cfr_external_adapter_game(
    const pe_cfr_external_adapter_t *adapter)
{
    return adapter ? &adapter->external : NULL;
}
