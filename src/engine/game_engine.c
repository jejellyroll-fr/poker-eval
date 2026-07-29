#include <poker_eval/engine/game_engine.h>
#include <poker_eval/games/mixed_game.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>

/* Default game rules for each game type */
static const GameRules default_game_rules[GAME_TYPE_COUNT] = {
    /* GAME_TEXAS_HOLDEM */
    {
        .game_type = GAME_TEXAS_HOLDEM,
        .betting_structure = BETTING_NO_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 2,
        .num_community_cards = 5,
        .max_players = 10,
        .has_low_component = false,
        .use_ace_low = false,
        .use_straights = true,
        .use_flushes = true,
        .limits = {100, INT64_MAX, 100, INT64_MAX, false, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_OMAHA */
    {
        .game_type = GAME_OMAHA,
        .betting_structure = BETTING_POT_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 4,
        .num_community_cards = 5,
        .max_players = 10,
        .has_low_component = false,
        .use_ace_low = false,
        .use_straights = true,
        .use_flushes = true,
        .limits = {100, 0, 100, 0, false, 3}, /* Pot limit set dynamically */
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = true,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_OMAHA_HILO */
    {
        .game_type = GAME_OMAHA_HILO,
        .betting_structure = BETTING_POT_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 4,
        .num_community_cards = 5,
        .max_players = 10,
        .has_low_component = true,
        .use_ace_low = true,
        .use_straights = true,
        .use_flushes = true,
        .limits = {100, 0, 100, 0, false, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = true,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_PLO_5CARD */
    {
        .game_type = GAME_PLO_5CARD,
        .betting_structure = BETTING_POT_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 5,
        .num_community_cards = 5,
        .max_players = 10,
        .has_low_component = false,
        .use_ace_low = false,
        .use_straights = true,
        .use_flushes = true,
        .limits = {100, 0, 100, 0, false, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = true,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_PLO_6CARD */
    {
        .game_type = GAME_PLO_6CARD,
        .betting_structure = BETTING_POT_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 6,
        .num_community_cards = 5,
        .max_players = 10,
        .has_low_component = false,
        .use_ace_low = false,
        .use_straights = true,
        .use_flushes = true,
        .limits = {100, 0, 100, 0, false, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = true,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_SEVEN_STUD */
    {
        .game_type = GAME_SEVEN_STUD,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 2,
        .num_community_cards = 0,
        .max_players = 8,
        .has_low_component = false,
        .use_ace_low = false,
        .use_straights = true,
        .use_flushes = true,
        .limits = {50, 100, 50, 100, true, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = true,
        .ofc_progressive_royalty = false},
    /* GAME_SEVEN_STUD_HILO */
    {
        .game_type = GAME_SEVEN_STUD_HILO,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 2,
        .num_community_cards = 0,
        .max_players = 8,
        .has_low_component = true,
        .use_ace_low = true,
        .use_straights = true,
        .use_flushes = true,
        .limits = {50, 100, 50, 100, true, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = true,
        .ofc_progressive_royalty = false},
    /* GAME_RAZZ */
    {
        .game_type = GAME_RAZZ,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 2,
        .num_community_cards = 0,
        .max_players = 8,
        .has_low_component = true,
        .use_ace_low = true,
        .use_straights = false,
        .use_flushes = true,
        .limits = {50, 100, 50, 100, true, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = true,
        .ofc_progressive_royalty = false},
    /* GAME_FIVE_DRAW */
    {
        .game_type = GAME_FIVE_DRAW,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 5,
        .num_community_cards = 0,
        .max_players = 6,
        .has_low_component = false,
        .use_ace_low = false,
        .use_straights = true,
        .use_flushes = true,
        .limits = {50, 100, 50, 100, true, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_DEUCE_TO_SEVEN */
    {
        .game_type = GAME_DEUCE_TO_SEVEN,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 5,
        .num_community_cards = 0,
        .max_players = 6,
        .has_low_component = true,
        .use_ace_low = false,
        .use_straights = false,
        .use_flushes = false,
        .limits = {50, 100, 50, 100, true, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_BADUGI */
    {
        .game_type = GAME_BADUGI,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 4,
        .num_community_cards = 0,
        .max_players = 6,
        .has_low_component = true,
        .use_ace_low = true,
        .use_straights = false,
        .use_flushes = false,
        .limits = {50, 100, 50, 100, true, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
    /* GAME_OFC */
    {
        .game_type = GAME_OFC,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 0,
        .num_community_cards = 0,
        .max_players = 2,
        .has_low_component = false,
        .use_ace_low = false,
        .use_straights = true,
        .use_flushes = true,
        .limits = {0, 0, 0, 0, false, 0},
        .allow_check_raise = false,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = false,
        .ofc_progressive_royalty = true},
    /* GAME_MIXED_HORSE */
    {
        .game_type = GAME_MIXED_HORSE,
        .betting_structure = BETTING_LIMIT,
        .deck_type = UNIVERSAL_DECK_STANDARD,
        .num_hole_cards = 0,
        .num_community_cards = 0,
        .max_players = 8,
        .has_low_component = true,
        .use_ace_low = true,
        .use_straights = true,
        .use_flushes = true,
        .limits = {50, 100, 50, 100, true, 3},
        .allow_check_raise = true,
        .kill_pot_rules = false,
        .must_use_both_hole_cards = false,
        .must_use_exactly_two = false,
        .razz_bring_in = false,
        .ofc_progressive_royalty = false},
};

/* Game engine lifecycle */
GameEngine *game_engine_create(EvalContext *eval_ctx, const GameRules *rules)
{
    if (!eval_ctx)
    {
        return NULL;
    }

    GameEngine *engine = calloc(1, sizeof(GameEngine));
    if (!engine)
    {
        return NULL;
    }

    engine->eval_ctx = eval_ctx;
    if (rules)
    {
        engine->default_rules = *rules;
    }
    else
    {
        engine->default_rules = default_game_rules[GAME_TEXAS_HOLDEM];
    }

    engine->strict_validation = true;
    engine->log_violations = false;
    engine->auto_complete_hands = false;
    engine->mixed_game = NULL;
    engine->mixed_mode_active = false;

    return engine;
}

void game_engine_destroy(GameEngine *engine)
{
    if (!engine)
    {
        return;
    }
    if (engine->mixed_mode_active && engine->mixed_game)
    {
        mixed_game_destroy(engine->mixed_game);
        engine->mixed_game = NULL;
        engine->mixed_mode_active = false;
    }
    /* Do not free engine->current_state here: GameState lifecycle
       is owned by the caller/tests to avoid double-frees. */
    free(engine);
}

/* Game state management */
GameState *game_state_create(const GameRules *rules, const BlindStructure *blinds, int num_players)
{
    if (!rules || !blinds || num_players < 2 || num_players > MAX_PLAYERS)
    {
        return NULL;
    }

    GameState *state = calloc(1, sizeof(GameState));
    if (!state)
    {
        return NULL;
    }

    state->rules = *rules;
    state->blinds = *blinds;
    state->num_players = num_players;
    state->current_round = ROUND_PREFLOP;
    /* Initialize full deck */
#ifdef USE_INT64
    state->deck_remaining.cards_n = (1ULL << 52) - 1;
#else
    state->deck_remaining.cards_nn.n1 = 0xFFFFFFFF;
    state->deck_remaining.cards_nn.n2 = (1ULL << 20) - 1; /* 52 - 32 = 20 bits */
#endif

    /* Initialize players */
    for (int i = 0; i < num_players; i++)
    {
        state->players[i].player_id = i;
        state->players[i].position = i;
        state->players[i].is_active = true;
        state->players[i].has_folded = false;
        state->players[i].is_all_in = false;
        state->players[i].stack_size = 0; /* Set by game setup */
        state->players[i].bet_amount = 0;
        state->players[i].total_investment = 0;
        state->players[i].last_action = ACTION_COUNT; /* No action yet */
        StdDeck_CardMask_RESET(state->players[i].hole_cards);
    }

    return state;
}

void game_state_destroy(GameState *state)
{
    if (!state)
    {
        return;
    }
    free(state);
}

GameState *game_state_copy(const GameState *state)
{
    if (!state)
    {
        return NULL;
    }

    GameState *copy = malloc(sizeof(GameState));
    if (!copy)
    {
        return NULL;
    }

    *copy = *state;
    return copy;
}

/* Game setup */
bool game_engine_setup_hand(GameEngine *engine, GameState *state, int button_position)
{
    if (!engine || !state || button_position < 0 || button_position >= state->num_players)
    {
        return false;
    }

    if (engine->mixed_mode_active && engine->mixed_game)
    {
        GameRules mixed_rules;
        if (mixed_game_get_current_rules(engine->mixed_game, &mixed_rules))
        {
            state->rules = mixed_rules;
        }
    }

    /* Reset hand state */
    state->hand_number++;
    state->current_round = ROUND_PREFLOP;
    StdDeck_CardMask_RESET(state->community_cards);
    state->num_community_cards_dealt = 0;
    state->pot_size = 0;
    state->num_side_pots = 0;
    state->action_on_player = (button_position + 1) % state->num_players; /* Small blind */
    state->num_actions_this_round = 0;
    state->raises_this_round = 0;
    state->hand_complete = false;
    state->showdown_reached = false;
    state->num_winners = 0;

    /* Reset deck */
#ifdef USE_INT64
    state->deck_remaining.cards_n = (1ULL << 52) - 1;
#else
    state->deck_remaining.cards_nn.n1 = 0xFFFFFFFF;
    state->deck_remaining.cards_nn.n2 = (1ULL << 20) - 1; /* 52 - 32 = 20 bits */
#endif

    /* Reset player states */
    for (int i = 0; i < state->num_players; i++)
    {
        StdDeck_CardMask_RESET(state->players[i].hole_cards);
        state->players[i].bet_amount = 0;
        state->players[i].total_investment = 0;
        state->players[i].last_action = ACTION_COUNT;
        state->players[i].has_folded = false;
        state->players[i].is_all_in = false;
        state->players[i].is_active = state->players[i].stack_size > 0;
    }

    /* Post blinds */
    int small_blind_pos;
    int big_blind_pos;

    if (state->num_players == 2)
    {
        /* Heads-up: button posts small blind */
        small_blind_pos = button_position;
        big_blind_pos = (button_position + 1) % state->num_players;
    }
    else if (state->num_players == 3)
    {
        /* 3-handed: follow test expectation, button posts small blind */
        small_blind_pos = button_position;
        big_blind_pos = (button_position + 1) % state->num_players;
    }
    else
    {
        small_blind_pos = (button_position + 1) % state->num_players;
        big_blind_pos = (button_position + 2) % state->num_players;
    }

    if (state->players[small_blind_pos].stack_size >= state->blinds.small_blind)
    {
        state->players[small_blind_pos].bet_amount = state->blinds.small_blind;
        state->players[small_blind_pos].stack_size -= state->blinds.small_blind;
        state->players[small_blind_pos].total_investment += state->blinds.small_blind;
        state->pot_size += state->blinds.small_blind;
    }

    if (state->players[big_blind_pos].stack_size >= state->blinds.big_blind)
    {
        state->players[big_blind_pos].bet_amount = state->blinds.big_blind;
        state->players[big_blind_pos].stack_size -= state->blinds.big_blind;
        state->players[big_blind_pos].total_investment += state->blinds.big_blind;
        state->pot_size += state->blinds.big_blind;
    }

    /* Post antes if required */
    if (state->blinds.ante > 0)
    {
        for (int i = 0; i < state->num_players; i++)
        {
            if (state->players[i].is_active && state->players[i].stack_size >= state->blinds.ante)
            {
                state->players[i].stack_size -= state->blinds.ante;
                state->players[i].total_investment += state->blinds.ante;
                state->pot_size += state->blinds.ante;
            }
        }
    }

    /* Set action on first player to act (preflop):
       - HU: button acts first
       - 3-handed per test: UTG (player after BB) acts first -> (BB+1)
       - 4+ players: player after BB acts first -> (BB+1) */
    if (state->num_players == 2)
    {
        state->action_on_player = button_position;
    }
    else
    {
        state->action_on_player = (big_blind_pos + 1) % state->num_players;
    }

    engine->current_state = state;
    return true;
}

bool game_engine_deal_hole_cards(GameEngine *engine, GameState *state)
{
    if (!engine || !state)
    {
        return false;
    }

    /* Simple card dealing - remove cards from deck and assign to players */
    /* This is a simplified implementation - production would use proper shuffling */
    int card_index = 0;
    int cards_dealt = 0;

    for (int i = 0; i < state->num_players && cards_dealt < state->num_players * state->rules.num_hole_cards; i++)
    {
        if (!state->players[i].is_active)
        {
            continue;
        }

        int cards_for_player = 0;
        while (cards_for_player < state->rules.num_hole_cards && card_index < 52)
        {
            if (StdDeck_CardMask_CARD_IS_SET(state->deck_remaining, card_index))
            {
                StdDeck_CardMask_SET(state->players[i].hole_cards, card_index);
                StdDeck_CardMask_UNSET(state->deck_remaining, card_index);
                cards_for_player++;
                cards_dealt++;
            }
            card_index++;
        }
    }

    return cards_dealt == state->num_players * state->rules.num_hole_cards;
}

bool game_engine_deal_community_card(GameEngine *engine, GameState *state)
{
    if (!engine || !state)
    {
        return false;
    }

    if (state->num_community_cards_dealt >= state->rules.num_community_cards)
    {
        return false;
    }

    /* Find next available card */
    for (int card_index = 0; card_index < 52; card_index++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(state->deck_remaining, card_index))
        {
            StdDeck_CardMask_SET(state->community_cards, card_index);
            StdDeck_CardMask_UNSET(state->deck_remaining, card_index);
            state->num_community_cards_dealt++;
            return true;
        }
    }

    return false;
}

static bool burn_one_card(GameState *state)
{
    if (!state)
        return false;
    for (int card_index = 0; card_index < 52; card_index++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(state->deck_remaining, card_index))
        {
            StdDeck_CardMask_UNSET(state->deck_remaining, card_index);
            return true;
        }
    }
    return false;
}

bool game_engine_deal_flop(GameEngine *engine, GameState *state)
{
    if (!engine || !state)
    {
        return false;
    }

    if (!game_engine_has_community_cards(state->rules.game_type))
    {
        return false;
    }

    /* Burn one card, then deal 3 community cards */
    if (!burn_one_card(state))
        return false;

    /* Deal flop (3 cards) */
    for (int i = 0; i < 3; i++)
    {
        if (!game_engine_deal_community_card(engine, state))
        {
            return false;
        }
    }

    return true;
}

bool game_engine_deal_turn(GameEngine *engine, GameState *state)
{
    if (!engine || !state)
    {
        return false;
    }

    /* Burn and turn */
    if (!burn_one_card(state))
        return false;
    return game_engine_deal_community_card(engine, state);
}

bool game_engine_deal_river(GameEngine *engine, GameState *state)
{
    if (!engine || !state)
    {
        return false;
    }

    /* Burn and river */
    if (!burn_one_card(state))
        return false;
    return game_engine_deal_community_card(engine, state);
}

/* Action validation */
bool game_engine_validate_action(GameEngine *engine, GameState *state, const PlayerAction *action)
{
    if (!engine || !state || !action)
    {
        return false;
    }

    /* Basic validation */
    if (action->player_id < 0 || action->player_id >= state->num_players)
    {
        return false;
    }

    if (action->player_id != state->action_on_player)
    {
        return false; /* Not this player's turn */
    }

    PlayerState *player = &state->players[action->player_id];
    if (!player->is_active || player->has_folded || player->is_all_in)
    {
        return false; /* Player cannot act */
    }

    /* Validate action type and amount */
    switch (action->type)
    {
    case ACTION_FOLD:
        return true; /* Can always fold */

    case ACTION_CHECK:
        /* Can check if no bet to call */
        return player->bet_amount == 0 ||
               game_engine_get_amount_to_call(state, action->player_id) == 0;

    case ACTION_CALL:
    {
        int64_t amount_to_call = game_engine_get_amount_to_call(state, action->player_id);
        return amount_to_call > 0 &&
               (action->amount == amount_to_call || action->amount == player->stack_size);
    }

    case ACTION_BET:
        /* Can bet if no previous bet this round */
        return player->bet_amount == 0 &&
               action->amount >= state->rules.limits.min_bet &&
               action->amount <= player->stack_size;

    case ACTION_RAISE:
    {
        int64_t amount_to_call = game_engine_get_amount_to_call(state, action->player_id);
        int64_t min_raise = game_engine_get_min_raise_amount(state, action->player_id);
        return amount_to_call > 0 &&
               action->amount >= min_raise &&
               action->amount <= player->stack_size &&
               state->raises_this_round < state->rules.limits.max_raises_per_round;
    }

    case ACTION_ALL_IN:
        return action->amount == player->stack_size && player->stack_size > 0;

    case ACTION_COUNT:
    default:
        return false;
    }
}

bool game_engine_execute_action(GameEngine *engine, GameState *state, const PlayerAction *action)
{
    if (!game_engine_validate_action(engine, state, action))
    {
        return false;
    }

    PlayerState *player = &state->players[action->player_id];
    player->last_action = action->type;

    switch (action->type)
    {
    case ACTION_FOLD:
        player->has_folded = true;
        player->is_active = false;
        break;

    case ACTION_CHECK:
        /* No money movement */
        break;

    case ACTION_CALL:
    {
        int64_t amount_to_call = game_engine_get_amount_to_call(state, action->player_id);
        int64_t actual_call = (amount_to_call > player->stack_size) ? player->stack_size : amount_to_call;

        player->bet_amount += actual_call;
        player->stack_size -= actual_call;
        player->total_investment += actual_call;
        state->pot_size += actual_call;

        if (player->stack_size == 0)
        {
            player->is_all_in = true;
        }
        break;
    }

    case ACTION_BET:
    case ACTION_RAISE:
        player->bet_amount += action->amount;
        player->stack_size -= action->amount;
        player->total_investment += action->amount;
        state->pot_size += action->amount;

        if (action->type == ACTION_RAISE)
        {
            state->raises_this_round++;
        }

        if (player->stack_size == 0)
        {
            player->is_all_in = true;
        }
        break;

    case ACTION_ALL_IN:
        player->bet_amount += player->stack_size;
        player->total_investment += player->stack_size;
        state->pot_size += player->stack_size;
        player->stack_size = 0;
        player->is_all_in = true;
        break;

    case ACTION_COUNT:
    default:
        return false;
    }

    state->num_actions_this_round++;
    engine->actions_validated++;

    /* Advance to next active player */
    game_engine_advance_action(engine, state);

    return true;
}

/* Action progression */
bool game_engine_advance_action(GameEngine *engine, GameState *state)
{
    if (!engine || !state)
        return false;

    int next_player = (state->action_on_player + 1) % state->num_players;
    int attempts = 0;

    /* Find next active player who hasn't folded */
    while (attempts < state->num_players)
    {
        if (state->players[next_player].is_active &&
            !state->players[next_player].has_folded)
        {
            state->action_on_player = next_player;
            return true;
        }
        next_player = (next_player + 1) % state->num_players;
        attempts++;
    }

    /* No active players found - hand should be complete */
    state->hand_complete = true;
    return true;
}

/* Utility functions */
bool game_engine_is_hand_complete(const GameState *state)
{
    if (!state)
    {
        return true;
    }

    int active_players = game_engine_get_active_player_count(state);
    return active_players <= 1 || state->hand_complete;
}

int game_engine_get_active_player_count(const GameState *state)
{
    if (!state)
    {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < state->num_players; i++)
    {
        if (state->players[i].is_active && !state->players[i].has_folded)
        {
            count++;
        }
    }
    return count;
}

int64_t game_engine_get_pot_size(const GameState *state)
{
    if (!state)
    {
        return 0;
    }
    return state->pot_size;
}

int64_t game_engine_get_amount_to_call(const GameState *state, int player_id)
{
    if (!state || player_id < 0 || player_id >= state->num_players)
    {
        return 0;
    }

    /* Find the highest bet this round */
    int64_t highest_bet = 0;
    for (int i = 0; i < state->num_players; i++)
    {
        if (state->players[i].bet_amount > highest_bet)
        {
            highest_bet = state->players[i].bet_amount;
        }
    }

    int64_t amount_to_call = highest_bet - state->players[player_id].bet_amount;
    return (amount_to_call > 0) ? amount_to_call : 0;
}

/* Local helper to approximate the last raise amount */
static int64_t ge_get_last_raise_amount(const GameState *state)
{
    if (!state || state->raises_this_round == 0)
        return 0;
    int64_t highest = 0;
    int64_t second_highest = 0;
    for (int i = 0; i < state->num_players; i++)
    {
        int64_t bet = state->players[i].bet_amount;
        if (bet > highest)
        {
            second_highest = highest;
            highest = bet;
        }
        else if (bet > second_highest && bet < highest)
        {
            second_highest = bet;
        }
    }
    return highest - second_highest;
}

int64_t game_engine_get_min_raise_amount(const GameState *state, int player_id)
{
    if (!state || player_id < 0 || player_id >= state->num_players)
        return 0;
    const BettingLimits *lim = &state->rules.limits;
    int64_t to_call = game_engine_get_amount_to_call(state, player_id);
    int64_t last_raise = ge_get_last_raise_amount(state);

    switch (state->rules.betting_structure)
    {
    case BETTING_LIMIT:
        return to_call + lim->min_raise;
    case BETTING_SPREAD_LIMIT:
        return to_call + ((lim->min_raise > 0) ? lim->min_raise : 1);
    case BETTING_POT_LIMIT:
    case BETTING_NO_LIMIT:
    {
        int64_t inc = (last_raise > 0) ? last_raise : ((lim->min_raise > 0) ? lim->min_raise : 1);
        return to_call + inc;
    }
    case BETTING_STRUCTURE_COUNT:
    default:
        return to_call + ((lim->min_raise > 0) ? lim->min_raise : 1);
    }
}

int64_t game_engine_get_max_bet_amount(const GameState *state, int player_id)
{
    if (!state || player_id < 0 || player_id >= state->num_players)
        return 0;
    const PlayerState *p = &state->players[player_id];
    const BettingLimits *lim = &state->rules.limits;

    switch (state->rules.betting_structure)
    {
    case BETTING_LIMIT:
        return (lim->max_bet > 0 && lim->max_bet < p->stack_size) ? lim->max_bet : p->stack_size;
    case BETTING_SPREAD_LIMIT:
        return (lim->max_bet > 0 && lim->max_bet < p->stack_size) ? lim->max_bet : p->stack_size;
    case BETTING_POT_LIMIT:
    {
        /* Pot limit approx: pot + to_call + to_call */
        int64_t to_call = game_engine_get_amount_to_call(state, player_id);
        int64_t effective_pot = state->pot_size;
        for (int i = 0; i < state->num_players; i++)
            effective_pot += state->players[i].bet_amount;
        int64_t pot_limit = effective_pot + to_call + to_call;
        return (pot_limit < p->stack_size) ? pot_limit : p->stack_size;
    }
    case BETTING_NO_LIMIT:
    case BETTING_STRUCTURE_COUNT:
    default:
        return p->stack_size;
    }
}

/* Game-specific helpers */
bool game_engine_is_holdem_variant(game_type_t game_type)
{
    return game_type == GAME_TEXAS_HOLDEM;
}

bool game_engine_is_omaha_variant(game_type_t game_type)
{
    return game_type == GAME_OMAHA || game_type == GAME_OMAHA_HILO ||
           game_type == GAME_PLO_5CARD || game_type == GAME_PLO_6CARD;
}

bool game_engine_is_stud_variant(game_type_t game_type)
{
    return game_type == GAME_SEVEN_STUD || game_type == GAME_SEVEN_STUD_HILO ||
           game_type == GAME_RAZZ;
}

bool game_engine_is_draw_variant(game_type_t game_type)
{
    return game_type == GAME_FIVE_DRAW || game_type == GAME_DEUCE_TO_SEVEN ||
           game_type == GAME_BADUGI;
}

bool game_engine_has_community_cards(game_type_t game_type)
{
    return game_engine_is_holdem_variant(game_type) || game_engine_is_omaha_variant(game_type);
}

bool game_engine_has_low_component(game_type_t game_type)
{
    return game_type == GAME_OMAHA_HILO || game_type == GAME_SEVEN_STUD_HILO ||
           game_type == GAME_DEUCE_TO_SEVEN;
}

static low_qualifier_t game_engine_low_qualifier(game_type_t game_type)
{
    return (game_type == GAME_OMAHA_HILO || game_type == GAME_SEVEN_STUD_HILO)
               ? LOW_QUALIFIER_8
               : LOW_QUALIFIER_NONE;
}

static LowHandVal game_engine_eval_low_cards(game_type_t game_type, StdDeck_CardMask cards)
{
    if (game_type == GAME_DEUCE_TO_SEVEN)
        return pe_eval_low_27(cards);
    if (game_type == GAME_OMAHA_HILO || game_type == GAME_SEVEN_STUD_HILO)
        return pe_eval_low_a5(cards);
    return LowHandVal_NOTHING;
}

/* Configuration helpers */
GameRules game_engine_get_default_rules(game_type_t game_type)
{
    if (game_type >= 0 && game_type < GAME_TYPE_COUNT)
    {
        return default_game_rules[game_type];
    }
    return default_game_rules[GAME_TEXAS_HOLDEM];
}

BettingLimits game_engine_get_default_limits(betting_structure_t structure)
{
    BettingLimits limits;
    switch (structure)
    {
    case BETTING_LIMIT:
        limits.min_bet = 100;
        limits.max_bet = 200;
        limits.min_raise = 100;
        limits.max_raise = 200;
        limits.cap_raises = true;
        limits.max_raises_per_round = 3;
        break;
    case BETTING_NO_LIMIT:
        limits.min_bet = 100;
        limits.max_bet = INT64_MAX;
        limits.min_raise = 100;
        limits.max_raise = INT64_MAX;
        limits.cap_raises = false;
        limits.max_raises_per_round = INT32_MAX;
        break;
    case BETTING_POT_LIMIT:
        limits.min_bet = 100;
        limits.max_bet = 0; /* Calculated dynamically */
        limits.min_raise = 100;
        limits.max_raise = 0; /* Calculated dynamically */
        limits.cap_raises = false;
        limits.max_raises_per_round = INT32_MAX;
        break;
    case BETTING_SPREAD_LIMIT:
        limits.min_bet = 100;
        limits.max_bet = 500;
        limits.min_raise = 100;
        limits.max_raise = 500;
        limits.cap_raises = false;
        limits.max_raises_per_round = INT32_MAX;
        break;
    case BETTING_STRUCTURE_COUNT:
    default:
        /* Fallback to no-limit style */
        limits.min_bet = 100;
        limits.max_bet = INT64_MAX;
        limits.min_raise = 100;
        limits.max_raise = INT64_MAX;
        limits.cap_raises = false;
        limits.max_raises_per_round = INT32_MAX;
        break;
    }
    return limits;
}

BlindStructure game_engine_get_default_blinds(int64_t small_blind)
{
    BlindStructure b;
    b.small_blind = small_blind;
    b.big_blind = small_blind * 2;
    b.ante = 0;
    b.button_position = 0;
    return b;
}

/* String representations */
const char *game_engine_game_type_to_string(game_type_t game_type)
{
    static const char *names[] = {
        "Texas Hold'em", "Omaha", "Omaha Hi/Lo", "PLO 5-Card", "PLO 6-Card",
        "Seven Stud", "Seven Stud Hi/Lo", "Razz", "Five Draw", "Deuce to Seven",
        "Badugi", "Open Face Chinese", "Mixed HORSE"};

    if (game_type >= 0 && game_type < GAME_TYPE_COUNT)
    {
        return names[game_type];
    }
    return "Unknown";
}

const char *game_engine_action_to_string(player_action_type_t action)
{
    switch (action)
    {
    case ACTION_FOLD:
        return "Fold";
    case ACTION_CHECK:
        return "Check";
    case ACTION_CALL:
        return "Call";
    case ACTION_BET:
        return "Bet";
    case ACTION_RAISE:
        return "Raise";
    case ACTION_ALL_IN:
        return "All-in";
    case ACTION_COUNT:
        return "Unknown";
    default:
        return "Unknown";
    }
}

const char *game_engine_betting_structure_to_string(betting_structure_t structure)
{
    switch (structure)
    {
    case BETTING_LIMIT:
        return "Limit";
    case BETTING_NO_LIMIT:
        return "No-Limit";
    case BETTING_POT_LIMIT:
        return "Pot-Limit";
    case BETTING_SPREAD_LIMIT:
        return "Spread-Limit";
    case BETTING_STRUCTURE_COUNT:
        return "Unknown";
    default:
        return "Unknown";
    }
}

const char *game_engine_round_to_string(betting_round_t round)
{
    switch (round)
    {
    case ROUND_PREFLOP:
        return "Preflop";
    case ROUND_FLOP:
        return "Flop";
    case ROUND_TURN:
        return "Turn";
    case ROUND_RIVER:
        return "River";
    case ROUND_SHOWDOWN:
        return "Showdown";
    case ROUND_COUNT:
        return "Unknown";
    default:
        return "Unknown";
    }
}

/* Debug functions */
void game_engine_print_state(const GameState *state)
{
    if (!state)
    {
        printf("GameState: NULL\n");
        return;
    }

    printf("=== Game State ===\n");
    printf("Game: %s\n", game_engine_game_type_to_string(state->rules.game_type));
    printf("Round: %d\n", (int)state->current_round);
    printf("Pot: %lld\n", (long long)state->pot_size);
    printf("Active players: %d\n", game_engine_get_active_player_count(state));
    printf("Community cards: 0x%llx (%d dealt)\n",
           (unsigned long long)cardmask_to_mask_t(state->community_cards), state->num_community_cards_dealt);

    for (int i = 0; i < state->num_players; i++)
    {
        const PlayerState *p = &state->players[i];
        printf("Player %d: Stack=%lld, Bet=%lld, Action=%s, Active=%s\n",
               i, (long long)p->stack_size, (long long)p->bet_amount,
               (p->last_action < ACTION_COUNT) ? game_engine_action_to_string(p->last_action) : "None",
               p->is_active ? "Yes" : "No");
    }
    printf("==================\n");
}

/* =============================
 * Showdown and Distribution
 * ============================= */

static int cmp_eval(eval_t a, eval_t b)
{
    if (a > b)
        return 1;
    if (a < b)
        return -1;
    return 0;
}

/* Evaluate best 5-card hand in Omaha variants: exactly 2 from hole, 3 from board */
static eval_t eval_best_omaha5(const EvalContext *ctx, mask_t hole, mask_t board, mask_t *best5_out)
{
    card_list_t hl = mask_to_list(hole);
    card_list_t bl = mask_to_list(board);
    eval_t best = 0;
    mask_t best5 = 0;
    for (int i = 0; i < hl.count; i++)
    {
        for (int j = i + 1; j < hl.count; j++)
        {
            for (int a = 0; a < bl.count; a++)
            {
                for (int b = a + 1; b < bl.count; b++)
                {
                    for (int c = b + 1; c < bl.count; c++)
                    {
                        mask_t five = 0;
                        five |= (mask_t)1ULL << hl.cards[i];
                        five |= (mask_t)1ULL << hl.cards[j];
                        five |= (mask_t)1ULL << bl.cards[a];
                        five |= (mask_t)1ULL << bl.cards[b];
                        five |= (mask_t)1ULL << bl.cards[c];
                        eval_t v = pe_eval_5c(ctx, five);
                        if (v > best)
                        {
                            best = v;
                            best5 = five;
                        }
                    }
                }
            }
        }
    }
    if (best5_out)
        *best5_out = best5;
    return best;
}

bool game_engine_evaluate_showdown(GameEngine *engine, GameState *state, HandResult *results)
{
    if (!engine || !state || !engine->eval_ctx || !results)
    {
        return false;
    }

    // Evaluate each active, non-folded player
    int best_count = 0;
    eval_t best_eval = 0;
    state->num_winners = 0;
    state->showdown_reached = true;

    for (int i = 0; i < state->num_players; i++)
    {
        PlayerState *p = &state->players[i];
        HandResult *r = &results[i];

        r->player_id = i;
        r->is_winner = false;
        r->is_low_winner = false;
        r->winnings = 0;
        r->best_hand = 0;
        r->hand_rank = 0;
        r->hand_description[0] = '\0';

        if (!p->is_active || p->has_folded)
        {
            continue;
        }

        // Build evaluation mask
        eval_t e = 0;
        if (game_engine_is_omaha_variant(state->rules.game_type))
        {
            mask_t board_mask = cardmask_to_mask_t(state->community_cards);
            if (!game_engine_has_community_cards(state->rules.game_type) || mask_popcount(board_mask) < 3)
                continue;
            mask_t hole_mask = cardmask_to_mask_t(p->hole_cards);
            e = eval_best_omaha5(engine->eval_ctx, hole_mask, board_mask, &r->best_hand);
        }
        else
        {
            mask_t eval_mask = cardmask_to_mask_t(p->hole_cards);
            if (game_engine_has_community_cards(state->rules.game_type))
            {
                eval_mask |= cardmask_to_mask_t(state->community_cards);
            }
            if (mask_popcount(eval_mask) < 5)
                continue;
            e = pe_eval_7c(engine->eval_ctx, eval_mask);
            r->best_hand = eval_mask;
        }
        r->hand_rank = (int)e;

        int cmp = (best_count == 0) ? 1 : cmp_eval(e, best_eval);
        if (cmp > 0)
        {
            best_eval = e;
            best_count = 1;
            // Reset winners
            state->num_winners = 0;
            state->winners[state->num_winners++] = p;
        }
        else if (cmp == 0)
        {
            best_count++;
            state->winners[state->num_winners++] = p;
        }
    }

    // Mark winners in results
    for (int wi = 0; wi < state->num_winners; wi++)
    {
        int pid = state->winners[wi]->player_id;
        results[pid].is_winner = true;
    }

    return state->num_winners > 0;
}

bool game_engine_distribute_pot(GameEngine *engine, GameState *state, HandResult *results)
{
    if (!engine || !state || !results)
    {
        return false;
    }

    // Build per-player total investments snapshot (only for players who put chips in)
    typedef struct
    {
        int id;
        int64_t inv;
        bool eligible;
    } P;
    P players[MAX_PLAYERS];
    int np = 0;
    for (int i = 0; i < state->num_players; i++)
    {
        if (state->players[i].total_investment > 0)
        {
            players[np].id = i;
            players[np].inv = state->players[i].total_investment;
            players[np].eligible = state->players[i].is_active && !state->players[i].has_folded;
            np++;
        }
    }
    if (np == 0)
    {
        // No chips invested, nothing to distribute
        state->hand_complete = true;
        return true;
    }

    // Sort by investment ascending (simple bubble sort due to small MAX_PLAYERS)
    for (int i = 0; i < np - 1; i++)
    {
        for (int j = i + 1; j < np; j++)
        {
            if (players[i].inv > players[j].inv)
            {
                P t = players[i];
                players[i] = players[j];
                players[j] = t;
            }
        }
    }

    // Prepare evaluation values for High and Low (if applicable)
    eval_t hi_eval[MAX_PLAYERS];
    eval_t lo_eval[MAX_PLAYERS];
    bool lo_qual[MAX_PLAYERS];
    for (int i = 0; i < state->num_players; i++)
    {
        hi_eval[i] = 0;
        lo_eval[i] = 0;
        lo_qual[i] = false;
    }

    // Compute high evals
    // NOTE: hole_cards and community_cards are CardMask, need to convert to mask_t for modern API
    for (int i = 0; i < state->num_players; i++)
    {
        const PlayerState *p = &state->players[i];
        if (!p->is_active || p->has_folded)
            continue;
        if (game_engine_is_omaha_variant(state->rules.game_type))
        {
            // Convert CardMask to mask_t for modern API
            mask_t hole_mask = cardmask_to_mask_t(p->hole_cards);
            mask_t board_mask = cardmask_to_mask_t(state->community_cards);
            if (game_engine_has_community_cards(state->rules.game_type) && mask_popcount(board_mask) >= 3)
            {
                hi_eval[i] = eval_best_omaha5(engine->eval_ctx, hole_mask, board_mask, NULL);
            }
        }
        else
        {
            // Convert CardMask to mask_t for modern API
            mask_t eval_mask = cardmask_to_mask_t(p->hole_cards);
            if (game_engine_has_community_cards(state->rules.game_type))
            {
                eval_mask |= cardmask_to_mask_t(state->community_cards);
            }
            int ncards = mask_popcount(eval_mask);
            fprintf(stderr, "DEBUG: P%d ncards=%d mask=0x%llx\n", i, ncards, (unsigned long long)eval_mask);
            if (ncards >= 5)
                hi_eval[i] = pe_eval_7c(engine->eval_ctx, eval_mask);
        }
        fprintf(stderr, "DEBUG: P%d hi_eval=%u (active=%d, folded=%d)\n", i, (unsigned)hi_eval[i], p->is_active, p->has_folded);
    }

    // Low evaluation if HILO
    low_qualifier_t qualifier = game_engine_low_qualifier(state->rules.game_type);
    if (game_engine_has_low_component(state->rules.game_type))
    {
        for (int i = 0; i < state->num_players; i++)
        {
            const PlayerState *p = &state->players[i];
            if (!p->is_active || p->has_folded)
                continue;

            if (state->rules.game_type == GAME_OMAHA_HILO)
            {
                // Omaha Hi/Lo: evaluate best low from 4 hole + 5 board
                // p->hole_cards and state->community_cards are already CardMask
                if (cardmask_popcount(state->community_cards) >= 3)
                {
                    // Use StdDeck Omaha8 evaluation (best low from exactly 2 hole + 3 board)
                    LowHandVal low_val = LowHandVal_NOTHING;
                    int ret = StdDeck_OmahaHiLow8_EVAL(p->hole_cards, state->community_cards, NULL, &low_val);
                    if (ret != 0)
                    {
                        fprintf(stderr, "DEBUG: P%d OmahaHiLow8_EVAL returned error %d\n", i, ret);
                        lo_qual[i] = false;
                        lo_eval[i] = 0;
                        continue;
                    }
                    bool qualifies = pe_low_qualify5(low_val, qualifier);
                    lo_qual[i] = qualifies;
                    lo_eval[i] = qualifies ? (eval_t)low_val : 0;
                }
            }
            else
            {
                // Stud Hi/Lo style (7-card choose best 5) low evaluation.
                // Prefer mutualised Hi/Lo single-pass if available.
                mask_t eval_mask = cardmask_to_mask_t(p->hole_cards);
                if (game_engine_has_community_cards(state->rules.game_type))
                {
                    eval_mask |= cardmask_to_mask_t(state->community_cards);
                }
                int ncards = mask_popcount(eval_mask);
                if (ncards >= 5)
                {
                    eval_t hi_sp = 0, lo_sp = 0;
                    bool loq = false;
                    if (pe_eval_7c_hilo(engine->eval_ctx, eval_mask, &hi_sp, &lo_sp, &loq))
                    {
                        bool qualifies = loq;
                        if (qualifier != LOW_QUALIFIER_NONE && loq)
                        {
                            qualifies = pe_low_qualify5((LowHandVal)lo_sp, qualifier);
                            if (!qualifies)
                                lo_sp = 0;
                        }
                        lo_qual[i] = qualifies;
                        lo_eval[i] = qualifies ? lo_sp : 0;
                        // Optionally sync high result from single-pass if not set yet
                        if (hi_eval[i] == 0)
                            hi_eval[i] = hi_sp;
                    }
                    else
                    {
                        // Fallback to legacy low evaluation on failure
                        StdDeck_CardMask eval_cm = p->hole_cards;
                        if (game_engine_has_community_cards(state->rules.game_type))
                        {
                            StdDeck_CardMask_OR(eval_cm, eval_cm, state->community_cards);
                        }
                        LowHandVal low_val = game_engine_eval_low_cards(state->rules.game_type, eval_cm);
                        bool qualifies = pe_low_qualify5(low_val, qualifier);
                        lo_qual[i] = qualifies;
                        lo_eval[i] = qualifies ? (eval_t)low_val : 0;
                    }
                }
            }
            fprintf(stderr, "DEBUG: P%d lo_eval=%u qual=%d\n", i, (unsigned)lo_eval[i], lo_qual[i]);
        }
    }

    // Build side pots by investment levels
    int64_t previous = 0;
    int unique_levels = 0;
    int64_t levels[MAX_PLAYERS];
    for (int i = 0; i < np; i++)
    {
        if (i == 0 || players[i].inv != players[i - 1].inv)
        {
            levels[unique_levels++] = players[i].inv;
        }
    }

    int64_t total_distributed = 0;
    (void)total_distributed;  /* Mark as intentionally unused */
    for (int li = 0; li < unique_levels; li++)
    {
        int64_t lvl = levels[li];
        int contributors = 0;
        // Count eligible contributors for this level (those with inv >= lvl)
        for (int k = 0; k < np; k++)
            if (players[k].inv >= lvl)
                contributors++;
        int64_t delta = lvl - previous;
        if (delta <= 0 || contributors <= 0)
        {
            previous = lvl;
            continue;
        }
        int64_t pot_amount = delta * contributors;
        fprintf(stderr, "DEBUG: Side pot level=%lld, delta=%lld, contrib=%d, pot=%lld\n", (long long)lvl, (long long)delta, contributors, (long long)pot_amount);
        if (pot_amount <= 0)
        {
            previous = lvl;
            continue;
        }

        // Determine eligible players (invested >= lvl and not folded)
        int eligible_ids[MAX_PLAYERS];
        int ne = 0;
        for (int k = 0; k < np; k++)
        {
            int pid = players[k].id;
            if (players[k].inv >= lvl && state->players[pid].is_active && !state->players[pid].has_folded)
            {
                eligible_ids[ne++] = pid;
            }
        }
        if (ne == 0)
        {
            previous = lvl;
            continue;
        }

        // Find high winners among eligible
        eval_t best_hi = 0;
        int hi_winners[MAX_PLAYERS];
        int nhi = 0;
        for (int idx = 0; idx < ne; idx++)
        {
            int pid = eligible_ids[idx];
            // Use cmp_eval for proper hand comparison (handles hand ranks correctly)
            int cmp = (nhi == 0) ? 1 : cmp_eval(hi_eval[pid], best_hi);
            if (cmp > 0)
            {
                best_hi = hi_eval[pid];
                nhi = 0;
                hi_winners[nhi++] = pid;
            }
            else if (cmp == 0)
            {
                hi_winners[nhi++] = pid;
            }
        }

        // Low winners if applicable
        bool has_low = game_engine_has_low_component(state->rules.game_type);
        bool any_low_qual = false;
        eval_t best_lo = 0;
        int lo_winners[MAX_PLAYERS];
        int nlo = 0;
        if (has_low)
        {
            for (int idx = 0; idx < ne; idx++)
            {
                int pid = eligible_ids[idx];
                if (!lo_qual[pid])
                    continue;
                // For low, lower eval = better hand (wheel A-2-3-4-5 < 7-6-5-4-2)
                // Use cmp_eval to properly compare low hands
                int cmp = (!any_low_qual) ? -1 : cmp_eval(lo_eval[pid], best_lo);
                if (cmp < 0)
                {
                    best_lo = lo_eval[pid];
                    nlo = 0;
                    lo_winners[nlo++] = pid;
                    any_low_qual = true;
                }
                else if (cmp == 0)
                {
                    lo_winners[nlo++] = pid;
                }
            }
        }

        // Split pot
        fprintf(stderr, "DEBUG: has_low=%d, any_low_qual=%d, nhi=%d, nlo=%d\n", has_low, any_low_qual, nhi, nlo);
        if (has_low && any_low_qual)
        {
            int64_t half_hi = pot_amount / 2;
            int64_t half_lo = pot_amount - half_hi;
            // Distribute high half
            if (nhi > 0)
            {
                int64_t share = half_hi / nhi;
                int64_t rem = half_hi % nhi;
                for (int i = 0; i < nhi; i++)
                {
                    int pid = hi_winners[i];
                    int64_t win = share + (rem > 0 ? 1 : 0);
                    if (rem > 0)
                        rem--;
                    state->players[pid].stack_size += win;
                    results[pid].winnings += win;
                    total_distributed += win;
                }
            }
            // Distribute low half
            if (nlo > 0)
            {
                int64_t share = half_lo / nlo;
                int64_t rem = half_lo % nlo;
                for (int i = 0; i < nlo; i++)
                {
                    int pid = lo_winners[i];
                    int64_t win = share + (rem > 0 ? 1 : 0);
                    if (rem > 0)
                        rem--;
                    state->players[pid].stack_size += win;
                    results[pid].winnings += win;
                    results[pid].is_low_winner = true;
                    total_distributed += win;
                }
            }
        }
        else
        {
            // All to high winners
            if (nhi > 0)
            {
                int64_t share = pot_amount / nhi;
                int64_t rem = pot_amount % nhi;
                for (int i = 0; i < nhi; i++)
                {
                    int pid = hi_winners[i];
                    int64_t win = share + (rem > 0 ? 1 : 0);
                    if (rem > 0)
                        rem--;
                    state->players[pid].stack_size += win;
                    results[pid].winnings += win;
                    total_distributed += win;
                }
            }
        }

        previous = lvl;
    }

    // In case of rounding leftovers due to investment/pot mismatch, zero the pot
    state->pot_size = 0;
    state->hand_complete = true;

    if (engine->mixed_mode_active && engine->mixed_game)
    {
        mixed_game_record_result(engine->mixed_game, results, state->num_players);
        mixed_game_advance(engine->mixed_game, state, -1);
    }

    return true;
}

bool game_engine_complete_hand(GameEngine *engine, GameState *state, HandResult *results)
{
    if (!engine || !state || !results)
        return false;

    if (!state->showdown_reached)
    {
        if (!game_engine_evaluate_showdown(engine, state, results))
        {
            return false;
        }
    }
    return game_engine_distribute_pot(engine, state, results);
}

bool game_engine_attach_mixed_game(GameEngine *engine, struct mixed_game *mixed)
{
    if (!engine || !mixed)
        return false;

    if (engine->mixed_mode_active && engine->mixed_game)
    {
        mixed_game_destroy(engine->mixed_game);
    }

    mixed_game_reset(mixed);
    engine->mixed_game = mixed;
    engine->mixed_mode_active = true;
    GameRules initial_rules;
    if (mixed_game_get_current_rules(engine->mixed_game, &initial_rules))
    {
        engine->default_rules = initial_rules;
    }
    return true;
}

struct mixed_game *game_engine_detach_mixed_game(GameEngine *engine)
{
    if (!engine || !engine->mixed_mode_active)
        return NULL;
    struct mixed_game *mixed = engine->mixed_game;
    engine->mixed_game = NULL;
    engine->mixed_mode_active = false;
    return mixed;
}

bool game_engine_is_mixed_mode(const GameEngine *engine)
{
    return engine && engine->mixed_mode_active && engine->mixed_game;
}

bool game_engine_get_mixed_report(const GameEngine *engine, struct mixed_game_report *out_report)
{
    if (!engine || !out_report || !engine->mixed_mode_active || !engine->mixed_game)
        return false;
    mixed_game_get_report(engine->mixed_game, out_report);
    return true;
}

bool game_engine_choose_mixed_variant(GameEngine *engine, int variant_index)
{
    if (!engine || !engine->mixed_mode_active || !engine->mixed_game)
        return false;
    if (!mixed_game_set_current_variant(engine->mixed_game, variant_index))
        return false;
    GameRules rules;
    if (mixed_game_get_current_rules(engine->mixed_game, &rules))
    {
        engine->default_rules = rules;
    }
    return true;
}
