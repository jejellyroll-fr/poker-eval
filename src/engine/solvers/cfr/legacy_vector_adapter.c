/* legacy_vector_adapter.c - bridge legacy CFR games into solver v3 */

#include <poker_eval/engine/solvers/cfr/legacy_vector_adapter.h>

#include <stdlib.h>
#include <string.h>

#define PE_LEGACY_VECTOR_MAX_ACTIONS 256

static uint64_t legacy_key(const void *state)
{
    return (uint64_t)(uintptr_t)state;
}

static void *legacy_state(uint64_t key)
{
    return (void *)(uintptr_t)key;
}

static int track_state(pe_legacy_vector_adapter_t *adapter, uint64_t key)
{
    size_t i;
    uint64_t *grown;

    if (key == adapter->root_key)
        return 0;
    for (i = 0u; i < adapter->owned_count; ++i)
        if (adapter->owned_state_keys[i] == key)
            return 0;
    if (adapter->owned_count == adapter->owned_capacity) {
        size_t capacity = adapter->owned_capacity == 0u
            ? 32u : adapter->owned_capacity * 2u;
        grown = (uint64_t *)realloc(adapter->owned_state_keys,
                                    capacity * sizeof(*grown));
        if (!grown)
            return -1;
        adapter->owned_state_keys = grown;
        adapter->owned_capacity = capacity;
    }
    adapter->owned_state_keys[adapter->owned_count++] = key;
    return 0;
}

static int adapter_is_terminal(const void *state, void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    return adapter->legacy->is_terminal(
        adapter->legacy, legacy_key(state), adapter->legacy->game_data);
}

static int adapter_acting_player(const void *state, void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    return adapter->legacy->current_player(
        adapter->legacy, legacy_key(state), adapter->legacy->game_data);
}

static uint16_t adapter_action_count(const void *state, void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    int actions[PE_LEGACY_VECTOR_MAX_ACTIONS];
    int count = adapter->legacy->get_actions(
        adapter->legacy, legacy_key(state), actions,
        PE_LEGACY_VECTOR_MAX_ACTIONS,
        adapter->legacy->game_data);
    if (count <= 0 || count > PE_LEGACY_VECTOR_MAX_ACTIONS)
        return 0u;
    return (uint16_t)count;
}

static int adapter_action_value(const pe_legacy_vector_adapter_t *adapter,
                                uint64_t key, uint16_t ordinal,
                                int *out_action)
{
    int actions[PE_LEGACY_VECTOR_MAX_ACTIONS];
    int count;

    if (!adapter || !adapter->legacy || !adapter->legacy->get_actions ||
        !out_action)
        return -1;
    count = adapter->legacy->get_actions(
        adapter->legacy, key, actions, PE_LEGACY_VECTOR_MAX_ACTIONS,
        adapter->legacy->game_data);
    if (count <= 0 || count > PE_LEGACY_VECTOR_MAX_ACTIONS ||
        ordinal >= (uint16_t)count)
        return -1;
    *out_action = actions[ordinal];
    return 0;
}

static uint64_t adapter_infoset_key(const void *state, void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    return adapter->legacy->get_infoset_key
        ? adapter->legacy->get_infoset_key(state) : legacy_key(state);
}

static const void *adapter_apply_action(const void *state, uint16_t action,
                                        void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    int legacy_action;
    uint64_t key;

    if (adapter_action_value(adapter, legacy_key(state), action,
                             &legacy_action) != 0)
        return NULL;
    key = adapter->legacy->apply_action(
        adapter->legacy, legacy_key(state), legacy_action,
        adapter->legacy->game_data);
    if (key == 0u || track_state(adapter, key) != 0)
        return NULL;
    return legacy_state(key);
}

static int adapter_terminal_values(const void *state,
                                   const pe_reach_vec_t *reach,
                                   pe_value_vec_t *out_values,
                                   uint8_t players, void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    uint8_t player;
    uint16_t combo;
    (void)reach;
    for (player = 0u; player < players; ++player) {
        double value = adapter->legacy->get_utility(
            adapter->legacy, legacy_key(state), (int)player,
            adapter->legacy->game_data);
        for (combo = 0u; combo < out_values[player].n; ++combo)
            out_values[player].v[combo] = value;
    }
    return 0;
}

static int adapter_is_chance(const void *state, void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    return adapter->legacy->is_chance
        ? adapter->legacy->is_chance(adapter->legacy, legacy_key(state),
                                     adapter->legacy->game_data) : 0;
}

static uint16_t adapter_chance_count(const void *state, void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    int count;
    if (!adapter->legacy->get_chance_outcomes)
        return 0u;
    count = adapter->legacy->get_chance_outcomes(
        adapter->legacy, legacy_key(state), adapter->legacy->game_data);
    return count > 0 && count <= 65535 ? (uint16_t)count : 0u;
}

static double adapter_chance_weight(const void *state, uint16_t outcome,
                                    void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    if (!adapter->legacy->get_chance_weight)
        return 1.0;
    return adapter->legacy->get_chance_weight(
        adapter->legacy, legacy_key(state), (int)outcome,
        adapter->legacy->game_data);
}

static const void *adapter_apply_chance(const void *state, int outcome,
                                        void *user)
{
    pe_legacy_vector_adapter_t *adapter =
        (pe_legacy_vector_adapter_t *)user;
    uint64_t key;
    if (!adapter->legacy->apply_chance)
        return NULL;
    key = adapter->legacy->apply_chance(
        adapter->legacy, legacy_key(state), outcome,
        adapter->legacy->game_data);
    if (key == 0u || track_state(adapter, key) != 0)
        return NULL;
    return legacy_state(key);
}

int pe_legacy_vector_adapter_init(pe_legacy_vector_adapter_t *adapter,
                                  cfr_game_t *legacy,
                                  uint16_t combo_count)
{
    int players;
    if (!adapter || !legacy || combo_count == 0u || !legacy->initial_state ||
        !legacy->is_terminal || !legacy->current_player ||
        !legacy->get_actions || !legacy->apply_action ||
        !legacy->get_utility)
        return -1;
    players = legacy->num_players > 0 ? legacy->num_players : 2;
    if (players < 2 || players > (int)PE_SOLVER_MAX_PLAYERS)
        return -1;
    memset(adapter, 0, sizeof(*adapter));
    adapter->legacy = legacy;
    adapter->root_key = legacy_key(legacy->initial_state);
    adapter->vector.root = legacy_state(adapter->root_key);
    adapter->vector.user = adapter;
    adapter->vector.player_count = (uint8_t)players;
    adapter->vector.combo_count = combo_count;
    adapter->vector.is_terminal = adapter_is_terminal;
    adapter->vector.acting_player = adapter_acting_player;
    adapter->vector.action_count = adapter_action_count;
    adapter->vector.infoset_key = adapter_infoset_key;
    adapter->vector.apply_action = adapter_apply_action;
    adapter->vector.terminal_values = adapter_terminal_values;
    if (legacy->is_chance && legacy->get_chance_outcomes &&
        legacy->apply_chance) {
        adapter->vector.is_chance = adapter_is_chance;
        adapter->vector.chance_outcome_count = adapter_chance_count;
        adapter->vector.chance_outcome_weight = adapter_chance_weight;
        adapter->vector.apply_chance = adapter_apply_chance;
    }
    return 0;
}

void pe_legacy_vector_adapter_destroy(pe_legacy_vector_adapter_t *adapter)
{
    size_t i;
    if (!adapter)
        return;
    if (adapter->legacy && adapter->legacy->release_state)
        for (i = 0u; i < adapter->owned_count; ++i)
            adapter->legacy->release_state(
                adapter->legacy, adapter->owned_state_keys[i],
                adapter->legacy->game_data);
    free(adapter->owned_state_keys);
    memset(adapter, 0, sizeof(*adapter));
}

const pe_vector_game_t *pe_legacy_vector_adapter_game(
    const pe_legacy_vector_adapter_t *adapter)
{
    return adapter ? &adapter->vector : NULL;
}
