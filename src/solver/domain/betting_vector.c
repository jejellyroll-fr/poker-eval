#include <poker_eval/solver/pe_betting_vector.h>

#include <stdlib.h>
#include <string.h>

static int vector_is_terminal(const void *state, void *user)
{
    const pe_betting_state_t *betting = (const pe_betting_state_t *)state;
    (void)user;
    return betting->terminal || betting->round_complete;
}

static int vector_acting_player(const void *state, void *user)
{
    (void)user;
    return ((const pe_betting_state_t *)state)->to_act;
}

static uint16_t vector_action_count(const void *state, void *user)
{
    pe_betting_vector_game_t *game = (pe_betting_vector_game_t *)user;
    const pe_betting_state_t *betting = (const pe_betting_state_t *)state;
    if (vector_is_terminal(state, user))
        return 0u;
    return game->ops.action_count(betting, game->user);
}

static uint64_t vector_infoset_key(const void *state, void *user)
{
    pe_betting_vector_game_t *game = (pe_betting_vector_game_t *)user;
    return game->ops.infoset_key((const pe_betting_state_t *)state,
                                 game->user);
}

static int vector_strategy(const void *state, uint64_t infoset_key,
                           uint16_t action, pe_value_vec_t *out, void *user)
{
    pe_betting_vector_game_t *game = (pe_betting_vector_game_t *)user;
    return game->ops.strategy((const pe_betting_state_t *)state, infoset_key,
                              action, out, game->user);
}

static int vector_terminal_values(const void *state,
                                  const pe_reach_vec_t *reach,
                                  pe_value_vec_t *out_values,
                                  uint8_t player_count, void *user)
{
    pe_betting_vector_game_t *game = (pe_betting_vector_game_t *)user;
    return game->ops.terminal_values((const pe_betting_state_t *)state, reach,
                                     out_values, player_count, game->user);
}

static int own_state(pe_betting_vector_game_t *game,
                     pe_betting_state_t *state)
{
    pe_betting_state_t **grown;
    size_t capacity;
    if (game->owned_count == game->owned_capacity)
    {
        capacity = game->owned_capacity == 0u ? 16u
                                               : game->owned_capacity * 2u;
        grown = (pe_betting_state_t **)realloc(
            game->owned_states, capacity * sizeof(*grown));
        if (!grown)
            return -1;
        game->owned_states = grown;
        game->owned_capacity = capacity;
    }
    game->owned_states[game->owned_count++] = state;
    return 0;
}

static const void *vector_apply_action(const void *state, uint16_t action,
                                       void *user)
{
    pe_betting_vector_game_t *game = (pe_betting_vector_game_t *)user;
    const pe_betting_state_t *source = (const pe_betting_state_t *)state;
    pe_action_t semantic;
    pe_betting_state_t *child;
    uint16_t count;

    count = vector_action_count(state, user);
    if (action >= count ||
        game->ops.action_at(source, action, &semantic, game->user) !=
            PE_ACTION_OK)
        return NULL;
    child = (pe_betting_state_t *)malloc(sizeof(*child));
    if (!child)
        return NULL;
    if (pe_betting_apply_action(source, &game->rules, &semantic, child) !=
        PE_BETTING_OK || own_state(game, child) != 0)
    {
        free(child);
        return NULL;
    }
    return child;
}

pe_betting_status_t pe_betting_vector_game_init(
    pe_betting_vector_game_t *out,
    const pe_betting_rules_t *rules,
    const pe_betting_state_t *root,
    uint16_t combo_count,
    const pe_betting_vector_ops_t *ops,
    void *user)
{
    if (!out || !rules || !root || !ops)
        return PE_BETTING_ERR_NULL_ARGUMENT;
    if (combo_count == 0u || !ops->action_count || !ops->action_at ||
        !ops->infoset_key || !ops->terminal_values ||
        pe_betting_state_validate(root, rules) != PE_BETTING_OK)
        return PE_BETTING_ERR_INVALID_STATE;
    memset(out, 0, sizeof(*out));
    out->rules = *rules;
    out->root = *root;
    out->ops = *ops;
    out->user = user;
    out->vector.root = &out->root;
    out->vector.user = out;
    out->vector.player_count = root->player_count;
    out->vector.combo_count = combo_count;
    out->vector.is_terminal = vector_is_terminal;
    out->vector.acting_player = vector_acting_player;
    out->vector.action_count = vector_action_count;
    out->vector.infoset_key = vector_infoset_key;
    out->vector.strategy = ops->strategy ? vector_strategy : NULL;
    out->vector.apply_action = vector_apply_action;
    out->vector.terminal_values = vector_terminal_values;
    return PE_BETTING_OK;
}

void pe_betting_vector_game_destroy(pe_betting_vector_game_t *game)
{
    size_t index;
    if (!game)
        return;
    for (index = 0u; index < game->owned_count; ++index)
        free(game->owned_states[index]);
    free(game->owned_states);
    memset(game, 0, sizeof(*game));
}
