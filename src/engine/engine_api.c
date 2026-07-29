#include <stdlib.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/engine/game_engine.h>
#include <poker_eval/engine/engine_api.h>
#include <poker_eval/games/mixed_game.h>
struct pe_engine
{
    EvalContext *ctx;
    GameEngine *impl;
};

struct pe_game_state
{
    GameState *impl;
};

static pe_status_t pe__ensure_engine_state(const pe_engine_t *e, const pe_game_state_t *s)
{
    if (!e || !e->impl || !e->ctx)
        return PE_STATUS_INVALID_STATE;
    if (s && !s->impl)
        return PE_STATUS_INVALID_STATE;
    return PE_STATUS_OK;
}

static EvalConfig pe__map_rules_to_eval_config(const GameRules *rules)
{
    if (!rules)
    {
        return eval_config_holdem();
    }

    switch (rules->game_type)
    {
    case GAME_TEXAS_HOLDEM:
        return eval_config_holdem();

    case GAME_OMAHA:
    case GAME_OMAHA_HILO:
    case GAME_PLO_5CARD:
    case GAME_PLO_6CARD:
        return eval_config_omaha();

    case GAME_SEVEN_STUD:
    case GAME_SEVEN_STUD_HILO:
    case GAME_RAZZ:
        return eval_config_stud();

    case GAME_FIVE_DRAW:
    case GAME_DEUCE_TO_SEVEN:
    case GAME_BADUGI:
    case GAME_OFC:
    case GAME_MIXED_HORSE:
    case GAME_TYPE_COUNT:
        return eval_config_default();

    default:
        /* For deck-specific games, check deck type */
        /* Note: deck_type_t uses DECK_STANDARD/DECK_JOKER, but we need to handle other cases */
        /* Default fallback */
        return eval_config_holdem();
    }
}

pe_status_t pe_engine_create_default(pe_engine_t **out_engine)
{
    if (!out_engine)
        return PE_STATUS_INVALID_ARGS;
    *out_engine = NULL;
    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
        return PE_STATUS_OUT_OF_MEMORY;
    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    GameEngine *ge = game_engine_create(ctx, &rules);
    if (!ge)
    {
        eval_context_destroy(ctx);
        return PE_STATUS_OUT_OF_MEMORY;
    }
    pe_engine_t *e = (pe_engine_t *)calloc(1, sizeof(*e));
    if (!e)
    {
        game_engine_destroy(ge);
        eval_context_destroy(ctx);
        return PE_STATUS_OUT_OF_MEMORY;
    }
    e->ctx = ctx;
    e->impl = ge;

    if (rules.game_type == GAME_MIXED_HORSE)
    {
        mixed_variant_t horse_rotation[] = {
            {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
            {.game = GAME_OMAHA_HILO, .hands_per_round = 1},
            {.game = GAME_RAZZ, .hands_per_round = 1},
            {.game = GAME_SEVEN_STUD, .hands_per_round = 1},
            {.game = GAME_SEVEN_STUD_HILO, .hands_per_round = 1},
        };
        mixed_game_t *mixed = mixed_game_create(horse_rotation, (int)(sizeof(horse_rotation) / sizeof(horse_rotation[0])),
                                                MIXED_ROTATION_SEQUENTIAL);
        if (!mixed || !game_engine_attach_mixed_game(ge, mixed))
        {
            if (mixed)
                mixed_game_destroy(mixed);
            game_engine_destroy(ge);
            eval_context_destroy(ctx);
            free(e);
            return PE_STATUS_OUT_OF_MEMORY;
        }
    }

    *out_engine = e;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_create_with_rules(const GameRules *rules, pe_engine_t **out_engine)
{
    if (!out_engine || !rules)
        return PE_STATUS_INVALID_ARGS;
    *out_engine = NULL;
    EvalConfig cfg = pe__map_rules_to_eval_config(rules);
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
        return PE_STATUS_OUT_OF_MEMORY;
    GameEngine *ge = game_engine_create(ctx, rules);
    if (!ge)
    {
        eval_context_destroy(ctx);
        return PE_STATUS_OUT_OF_MEMORY;
    }
    pe_engine_t *e = (pe_engine_t *)calloc(1, sizeof(*e));
    if (!e)
    {
        game_engine_destroy(ge);
        eval_context_destroy(ctx);
        return PE_STATUS_OUT_OF_MEMORY;
    }
    e->ctx = ctx;
    e->impl = ge;
    *out_engine = e;
    return PE_STATUS_OK;
}

void pe_engine_destroy(pe_engine_t *engine)
{
    if (!engine)
        return;
    if (engine->impl)
        game_engine_destroy(engine->impl);
    if (engine->ctx)
        eval_context_destroy(engine->ctx);
    free(engine);
}

pe_status_t pe_game_state_create(const GameRules *rules, const BlindStructure *blinds,
                                 int num_players, pe_game_state_t **out_state)
{
    if (!out_state || num_players <= 0)
        return PE_STATUS_INVALID_ARGS;
    *out_state = NULL;
    GameRules r = rules ? *rules : game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure b = blinds ? *blinds : game_engine_get_default_blinds(100);
    GameState *gs = game_state_create(&r, &b, num_players);
    if (!gs)
        return PE_STATUS_OUT_OF_MEMORY;
    pe_game_state_t *s = (pe_game_state_t *)calloc(1, sizeof(*s));
    if (!s)
    {
        game_state_destroy(gs);
        return PE_STATUS_OUT_OF_MEMORY;
    }
    s->impl = gs;
    *out_state = s;
    return PE_STATUS_OK;
}

void pe_game_state_destroy(pe_game_state_t *state)
{
    if (!state)
        return;
    if (state->impl)
        game_state_destroy(state->impl);
    free(state);
}

pe_status_t pe_engine_set_player_stack(pe_game_state_t *state, int player_id, int64_t stack_size)
{
    if (!state || !state->impl || player_id < 0 || stack_size < 0)
        return PE_STATUS_INVALID_ARGS;
    GameState *gs = state->impl;
    if (player_id >= gs->num_players)
        return PE_STATUS_INVALID_ARGS;
    gs->players[player_id].stack_size = stack_size;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_setup_hand(pe_engine_t *engine, pe_game_state_t *state, int button_position)
{
    pe_status_t st = pe__ensure_engine_state(engine, state);
    if (st != PE_STATUS_OK)
        return st;
    return game_engine_setup_hand(engine->impl, state->impl, button_position) ? PE_STATUS_OK : PE_STATUS_INVALID_STATE;
}

pe_status_t pe_engine_apply_action_struct(pe_engine_t *engine, pe_game_state_t *state,
                                          const PlayerAction *action)
{
    pe_status_t st = pe__ensure_engine_state(engine, state);
    if (st != PE_STATUS_OK)
        return st;
    if (!action)
        return PE_STATUS_INVALID_ARGS;
    return game_engine_execute_action(engine->impl, state->impl, action) ? PE_STATUS_OK : PE_STATUS_INVALID_ARGS;
}

pe_status_t pe_engine_apply_action(pe_engine_t *engine, pe_game_state_t *state,
                                   int action_type, int64_t amount, int player_id)
{
    PlayerAction action;
    action.type = action_type;
    action.amount = amount;
    action.player_id = player_id;
    action.round = 0; /* Will be filled by engine */
    action.is_valid = true;
    return pe_engine_apply_action_struct(engine, state, &action);
}

pe_status_t pe_engine_complete_hand_eval(pe_engine_t *engine, pe_game_state_t *state,
                                         EvaluationResult *out_result)
{
    pe_status_t st = pe__ensure_engine_state(engine, state);
    if (st != PE_STATUS_OK)
        return st;
    if (!out_result)
        return PE_STATUS_INVALID_ARGS;

    /* Allocate temporary HandResult array for internal use */
    HandResult results[MAX_PLAYERS];
    if (!game_engine_complete_hand(engine->impl, state->impl, results))
    {
        return PE_STATUS_INVALID_STATE;
    }

    /* Map results - for now just return success, full mapping can be added later */
    return PE_STATUS_OK;
}

pe_status_t pe_engine_complete_hand(pe_engine_t *engine, pe_game_state_t *state,
                                    HandResult *results, int max_results)
{
    pe_status_t st = pe__ensure_engine_state(engine, state);
    if (st != PE_STATUS_OK)
        return st;
    if (!results || max_results <= 0)
        return PE_STATUS_INVALID_ARGS;

    /* Call internal game engine function directly with HandResult array */
    if (!game_engine_complete_hand(engine->impl, state->impl, results))
    {
        return PE_STATUS_INVALID_STATE;
    }

    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_pot_size(const pe_game_state_t *state, int64_t *out_pot)
{
    if (!state || !state->impl || !out_pot)
        return PE_STATUS_INVALID_ARGS;
    *out_pot = game_engine_get_pot_size(state->impl);
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_active_player_count(const pe_game_state_t *state, int *out_count)
{
    if (!state || !state->impl || !out_count)
        return PE_STATUS_INVALID_ARGS;
    *out_count = game_engine_get_active_player_count(state->impl);
    return PE_STATUS_OK;
}

/* Safe getters for game state */
pe_status_t pe_engine_get_current_round(const pe_game_state_t *state, int *out_round)
{
    if (!state || !state->impl || !out_round)
        return PE_STATUS_INVALID_ARGS;
    *out_round = (int)state->impl->current_round;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_hand_number(const pe_game_state_t *state, uint64_t *out_hand_number)
{
    if (!state || !state->impl || !out_hand_number)
        return PE_STATUS_INVALID_ARGS;
    *out_hand_number = state->impl->hand_number;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_action_on_player(const pe_game_state_t *state, int *out_player_id)
{
    if (!state || !state->impl || !out_player_id)
        return PE_STATUS_INVALID_ARGS;
    *out_player_id = state->impl->action_on_player;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_num_community_cards(const pe_game_state_t *state, int *out_count)
{
    if (!state || !state->impl || !out_count)
        return PE_STATUS_INVALID_ARGS;
    *out_count = state->impl->num_community_cards_dealt;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_is_hand_complete(const pe_game_state_t *state, bool *out_complete)
{
    if (!state || !state->impl || !out_complete)
        return PE_STATUS_INVALID_ARGS;
    *out_complete = state->impl->hand_complete;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_is_showdown_reached(const pe_game_state_t *state, bool *out_showdown)
{
    if (!state || !state->impl || !out_showdown)
        return PE_STATUS_INVALID_ARGS;
    *out_showdown = state->impl->showdown_reached;
    return PE_STATUS_OK;
}

/* Safe getters for player state */
static pe_status_t pe__validate_player_id(const pe_game_state_t *state, int player_id)
{
    if (!state || !state->impl)
        return PE_STATUS_INVALID_ARGS;
    if (player_id < 0 || player_id >= state->impl->num_players)
        return PE_STATUS_INVALID_ARGS;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_player_stack_size(const pe_game_state_t *state, int player_id, int64_t *out_stack)
{
    if (!out_stack)
        return PE_STATUS_INVALID_ARGS;
    pe_status_t st = pe__validate_player_id(state, player_id);
    if (st != PE_STATUS_OK)
        return st;
    *out_stack = state->impl->players[player_id].stack_size;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_player_bet_amount(const pe_game_state_t *state, int player_id, int64_t *out_bet)
{
    if (!out_bet)
        return PE_STATUS_INVALID_ARGS;
    pe_status_t st = pe__validate_player_id(state, player_id);
    if (st != PE_STATUS_OK)
        return st;
    *out_bet = state->impl->players[player_id].bet_amount;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_player_last_action(const pe_game_state_t *state, int player_id, int *out_action)
{
    if (!out_action)
        return PE_STATUS_INVALID_ARGS;
    pe_status_t st = pe__validate_player_id(state, player_id);
    if (st != PE_STATUS_OK)
        return st;
    *out_action = (int)state->impl->players[player_id].last_action;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_is_player_active(const pe_game_state_t *state, int player_id, bool *out_active)
{
    if (!out_active)
        return PE_STATUS_INVALID_ARGS;
    pe_status_t st = pe__validate_player_id(state, player_id);
    if (st != PE_STATUS_OK)
        return st;
    *out_active = state->impl->players[player_id].is_active;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_is_player_all_in(const pe_game_state_t *state, int player_id, bool *out_all_in)
{
    if (!out_all_in)
        return PE_STATUS_INVALID_ARGS;
    pe_status_t st = pe__validate_player_id(state, player_id);
    if (st != PE_STATUS_OK)
        return st;
    *out_all_in = state->impl->players[player_id].is_all_in;
    return PE_STATUS_OK;
}

pe_status_t pe_engine_has_player_folded(const pe_game_state_t *state, int player_id, bool *out_folded)
{
    if (!out_folded)
        return PE_STATUS_INVALID_ARGS;
    pe_status_t st = pe__validate_player_id(state, player_id);
    if (st != PE_STATUS_OK)
        return st;
    *out_folded = state->impl->players[player_id].has_folded;
    return PE_STATUS_OK;
}

static const GameState *pe_engine_state_cptr(const pe_game_state_t *state)
{
    return state ? state->impl : NULL;
}

static GameState *pe_engine_state_ptr(pe_game_state_t *state)
{
    return state ? state->impl : NULL;
}

pe_status_t pe_engine_attach_mixed_game(pe_engine_t *engine, struct mixed_game *mixed)
{
    pe_status_t st = pe__ensure_engine_state(engine, NULL);
    if (st != PE_STATUS_OK)
        return st;
    if (!mixed)
        return PE_STATUS_INVALID_ARGS;
    return game_engine_attach_mixed_game(engine->impl, mixed) ? PE_STATUS_OK : PE_STATUS_INVALID_STATE;
}

pe_status_t pe_engine_detach_mixed_game(pe_engine_t *engine, struct mixed_game **out_mixed)
{
    pe_status_t st = pe__ensure_engine_state(engine, NULL);
    if (st != PE_STATUS_OK)
        return st;
    if (out_mixed)
        *out_mixed = NULL;
    struct mixed_game *mixed = game_engine_detach_mixed_game(engine->impl);
    if (!mixed)
        return PE_STATUS_INVALID_STATE;
    if (out_mixed)
        *out_mixed = mixed;
    else
        mixed_game_destroy(mixed);
    return PE_STATUS_OK;
}

pe_status_t pe_engine_get_mixed_report(const pe_engine_t *engine, struct mixed_game_report *out_report)
{
    pe_status_t st = pe__ensure_engine_state(engine, NULL);
    if (st != PE_STATUS_OK)
        return st;
    return game_engine_get_mixed_report(engine->impl, out_report) ? PE_STATUS_OK : PE_STATUS_INVALID_STATE;
}

pe_status_t pe_engine_choose_mixed_variant(pe_engine_t *engine, int variant_index)
{
    pe_status_t st = pe__ensure_engine_state(engine, NULL);
    if (st != PE_STATUS_OK)
        return st;
    return game_engine_choose_mixed_variant(engine->impl, variant_index) ? PE_STATUS_OK : PE_STATUS_INVALID_ARGS;
}

bool pe_engine_is_mixed_mode(const pe_engine_t *engine)
{
    return game_engine_is_mixed_mode(engine ? engine->impl : NULL);
}
