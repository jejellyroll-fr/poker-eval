#include <stdlib.h>
#include <poker_eval/engine/game_engine.h>
#include <poker_eval/engine/betting_engine.h>
#include <poker_eval/engine/betting_api.h>

/* Overlay that mirrors pe_game_state_t layout to reach internal GameState* */
struct pe_game_state_overlay
{
    GameState *impl;
};

static inline GameState *pe_state_to_impl(const pe_game_state_t *state)
{
    const struct pe_game_state_overlay *overlay = (const struct pe_game_state_overlay *)state;
    return overlay ? overlay->impl : NULL;
}

struct pe_betting_engine
{
    BettingEngine *impl;
    BettingAction *cached_actions;
    int cached_count;
};

static inline pe_status_t pe__validate_handle(pe_betting_engine_t *engine)
{
    if (!engine || !engine->impl)
    {
        return PE_STATUS_INVALID_ARGS;
    }
    return PE_STATUS_OK;
}

pe_status_t pe_betting_engine_create(betting_structure_t structure,
                                     const BettingLimits *limits,
                                     pe_betting_engine_t **out_engine)
{
    if (!out_engine)
        return PE_STATUS_INVALID_ARGS;
    *out_engine = NULL;

    BettingEngine *be = betting_engine_create(structure, limits);
    if (!be)
    {
        return PE_STATUS_OUT_OF_MEMORY;
    }

    pe_betting_engine_t *h = (pe_betting_engine_t *)calloc(1, sizeof(*h));
    if (!h)
    {
        betting_engine_destroy(be);
        return PE_STATUS_OUT_OF_MEMORY;
    }

    h->impl = be;
    *out_engine = h;
    return PE_STATUS_OK;
}

void pe_betting_engine_destroy(pe_betting_engine_t *engine)
{
    if (!engine)
        return;

    if (engine->cached_actions)
        free(engine->cached_actions);
    if (engine->impl)
        betting_engine_destroy(engine->impl);

    free(engine);
}

pe_status_t pe_betting_engine_get_valid_actions(pe_betting_engine_t *engine,
                                                const pe_game_state_t *state,
                                                int player_id,
                                                const BettingAction **out_actions,
                                                int *out_count)
{
    pe_status_t st = pe__validate_handle(engine);
    if (st != PE_STATUS_OK)
        return st;
    if (!state || !out_actions || !out_count)
        return PE_STATUS_INVALID_ARGS;

    GameState *gs = pe_state_to_impl(state);
    if (!gs)
        return PE_STATUS_INVALID_STATE;
    if (player_id < 0 || player_id >= gs->num_players)
        return PE_STATUS_INVALID_ARGS;

    /* If player cannot act, return empty set gracefully */
    if (!betting_engine_can_player_act(gs, player_id))
    {
        if (engine->cached_actions)
        {
            free(engine->cached_actions);
            engine->cached_actions = NULL;
            engine->cached_count = 0;
        }
        *out_actions = NULL;
        *out_count = 0;
        return PE_STATUS_OK;
    }

    if (engine->cached_actions)
    {
        free(engine->cached_actions);
        engine->cached_actions = NULL;
        engine->cached_count = 0;
    }

    BettingAction *actions = betting_engine_get_valid_actions(engine->impl, gs, player_id);
    if (!actions)
    {
        *out_actions = NULL;
        *out_count = 0;
        return PE_STATUS_OUT_OF_MEMORY;
    }

    int count = 0;
    while (actions[count].is_valid && count < ACTION_COUNT)
    {
        count++;
    }

    engine->cached_actions = actions;
    engine->cached_count = count;
    *out_actions = engine->cached_actions;
    *out_count = engine->cached_count;
    return PE_STATUS_OK;
}

pe_status_t pe_betting_engine_validate_action(pe_betting_engine_t *engine,
                                              const pe_game_state_t *state,
                                              int player_id,
                                              const BettingAction *action,
                                              bool *out_valid)
{
    pe_status_t st = pe__validate_handle(engine);
    if (st != PE_STATUS_OK)
        return st;
    if (!state || !action || !out_valid)
        return PE_STATUS_INVALID_ARGS;

    GameState *gs = pe_state_to_impl(state);
    if (!gs)
        return PE_STATUS_INVALID_STATE;
    if (player_id < 0 || player_id >= gs->num_players)
        return PE_STATUS_INVALID_ARGS;

    if (!betting_engine_can_player_act(gs, player_id))
    {
        *out_valid = false;
        return PE_STATUS_OK;
    }

    bool valid = betting_engine_is_action_valid(engine->impl, gs, action->action, action->amount, player_id);
    *out_valid = valid;
    return PE_STATUS_OK;
}

pe_status_t pe_betting_engine_get_min_bet(pe_betting_engine_t *engine,
                                          const pe_game_state_t *state,
                                          int player_id,
                                          int64_t *out_min_bet)
{
    pe_status_t st = pe__validate_handle(engine);
    if (st != PE_STATUS_OK)
        return st;
    if (!state || !out_min_bet)
        return PE_STATUS_INVALID_ARGS;

    GameState *gs = pe_state_to_impl(state);
    if (!gs)
        return PE_STATUS_INVALID_STATE;
    if (player_id < 0 || player_id >= gs->num_players)
        return PE_STATUS_INVALID_ARGS;

    *out_min_bet = betting_engine_calculate_min_bet(engine->impl, gs, player_id);
    return PE_STATUS_OK;
}

pe_status_t pe_betting_engine_get_max_bet(pe_betting_engine_t *engine,
                                          const pe_game_state_t *state,
                                          int player_id,
                                          int64_t *out_max_bet)
{
    pe_status_t st = pe__validate_handle(engine);
    if (st != PE_STATUS_OK)
        return st;
    if (!state || !out_max_bet)
        return PE_STATUS_INVALID_ARGS;

    GameState *gs = pe_state_to_impl(state);
    if (!gs)
        return PE_STATUS_INVALID_STATE;
    if (player_id < 0 || player_id >= gs->num_players)
        return PE_STATUS_INVALID_ARGS;

    *out_max_bet = betting_engine_calculate_max_bet(engine->impl, gs, player_id);
    return PE_STATUS_OK;
}

pe_status_t pe_betting_engine_get_call_amount(pe_betting_engine_t *engine,
                                              const pe_game_state_t *state,
                                              int player_id,
                                              int64_t *out_call_amount)
{
    pe_status_t st = pe__validate_handle(engine);
    if (st != PE_STATUS_OK)
        return st;
    if (!state || !out_call_amount)
        return PE_STATUS_INVALID_ARGS;

    GameState *gs = pe_state_to_impl(state);
    if (!gs)
        return PE_STATUS_INVALID_STATE;
    if (player_id < 0 || player_id >= gs->num_players)
        return PE_STATUS_INVALID_ARGS;

    *out_call_amount = game_engine_get_amount_to_call(gs, player_id);
    return PE_STATUS_OK;
}

pe_status_t pe_betting_engine_get_structure(pe_betting_engine_t *engine,
                                            betting_structure_t *out_structure)
{
    pe_status_t st = pe__validate_handle(engine);
    if (st != PE_STATUS_OK)
        return st;
    if (!out_structure)
        return PE_STATUS_INVALID_ARGS;

    *out_structure = engine->impl->structure;
    return PE_STATUS_OK;
}

pe_status_t pe_betting_engine_get_limits(pe_betting_engine_t *engine,
                                         const BettingLimits **out_limits)
{
    pe_status_t st = pe__validate_handle(engine);
    if (st != PE_STATUS_OK)
        return st;
    if (!out_limits)
        return PE_STATUS_INVALID_ARGS;

    *out_limits = &engine->impl->limits;
    return PE_STATUS_OK;
}
