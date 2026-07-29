#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <poker_eval/engine/game_engine.h>
#include <poker_eval/engine/engine_api.h>
#include <poker_eval/engine/betting_engine.h>
#include <poker_eval/engine/betting_api.h>

/* Mirror of pe_game_state_t layout to reach GameState for setup */
struct pe_game_state_overlay
{
    GameState *impl;
};

static GameState *get_state_impl(pe_game_state_t *state)
{
    return state ? ((struct pe_game_state_overlay *)state)->impl : NULL;
}

int main(void)
{
    pe_betting_engine_t *betting = NULL;
    pe_status_t st = pe_betting_engine_create(BETTING_NO_LIMIT, NULL, &betting);
    if (st != PE_STATUS_OK)
        return 1;
    assert(betting != NULL);

    pe_game_state_t *state = NULL;
    st = pe_game_state_create(NULL, NULL, 2, &state);
    if (st != PE_STATUS_OK)
        return 1;
    assert(state != NULL);

    /* Setup stacks */
    st = pe_engine_set_player_stack(state, 0, 1000);
    if (st != PE_STATUS_OK)
        return 1;
    st = pe_engine_set_player_stack(state, 1, 1000);
    if (st != PE_STATUS_OK)
        return 1;

    GameState *gs = get_state_impl(state);
    if (!gs) {
        return 1;
    }

    /* Scenario 1: No bet yet, player should be able to check/bet/all-in */
    gs->players[0].bet_amount = 0;
    gs->players[1].bet_amount = 0;
    gs->raises_this_round = 0;

    const BettingAction *actions = NULL;
    int count = -1;
    st = pe_betting_engine_get_valid_actions(betting, state, 0, &actions, &count);
    if (st != PE_STATUS_OK)
        return 1;
    assert(actions != NULL);

    /* When there is no amount to call we expect three actions: check, bet, all-in */
    assert(count == 3);
    assert(actions[0].action == ACTION_CHECK);
    assert(actions[1].action == ACTION_BET);
    assert(actions[1].min_amount == gs->rules.limits.min_bet);
    assert(actions[1].max_amount == gs->players[0].stack_size);
    assert(actions[2].action == ACTION_ALL_IN);
    assert(actions[2].amount == gs->players[0].stack_size);

    /* Scenario 2: Facing a bet of 200 with 500 chips remaining */
    gs->players[1].bet_amount = 200;
    gs->players[0].bet_amount = 0;
    gs->players[0].stack_size = 500;
    gs->raises_this_round = 1;

    st = pe_betting_engine_get_valid_actions(betting, state, 0, &actions, &count);
    if (st != PE_STATUS_OK)
        return 1;
    assert(count == 4);
    assert(actions[0].action == ACTION_FOLD);
    assert(actions[1].action == ACTION_CALL);
    assert(actions[1].amount == 200);
    assert(actions[2].action == ACTION_RAISE);
    assert(actions[2].min_amount == 400);
    assert(actions[2].max_amount == 500);
    assert(actions[3].action == ACTION_ALL_IN);
    assert(actions[3].amount == 500);

    /* Validate the call action through the API helper */
    bool valid = false;
    st = pe_betting_engine_validate_action(betting, state, 0, &actions[1], &valid);
    if (st != PE_STATUS_OK)
        return 1;
    assert(valid);

    int64_t call_amount = 0;
    st = pe_betting_engine_get_call_amount(betting, state, 0, &call_amount);
    if (st != PE_STATUS_OK)
        return 1;
    assert(call_amount == 200);

    int64_t min_bet = 0;
    st = pe_betting_engine_get_min_bet(betting, state, 0, &min_bet);
    if (st != PE_STATUS_OK)
        return 1;
    assert(min_bet == gs->rules.limits.min_bet);

    int64_t max_bet = 0;
    st = pe_betting_engine_get_max_bet(betting, state, 0, &max_bet);
    if (st != PE_STATUS_OK)
        return 1;
    assert(max_bet == gs->players[0].stack_size);

    const BettingLimits *limits = NULL;
    st = pe_betting_engine_get_limits(betting, &limits);
    if (st != PE_STATUS_OK)
        return 1;
    assert(limits != NULL);
    assert(limits->min_bet == gs->rules.limits.min_bet);

    betting_structure_t structure = BETTING_LIMIT;
    st = pe_betting_engine_get_structure(betting, &structure);
    if (st != PE_STATUS_OK)
        return 1;
    assert(structure == BETTING_NO_LIMIT);

    /* Scenario 3: Player cannot act (folded) */
    gs->players[0].has_folded = true;
    st = pe_betting_engine_get_valid_actions(betting, state, 0, &actions, &count);
    if (st != PE_STATUS_OK)
        return 1;
    assert(actions == NULL);
    assert(count == 0);
    gs->players[0].has_folded = false;

    /* Scenario 4: Pot-limit sizing */
    pe_betting_engine_t *pot_engine = NULL;
    st = pe_betting_engine_create(BETTING_POT_LIMIT, NULL, &pot_engine);
    if (st != PE_STATUS_OK)
        return 1;
    assert(pot_engine != NULL);

    gs->rules.betting_structure = BETTING_POT_LIMIT;
    gs->pot_size = 300;
    gs->players[0].bet_amount = 50;
    gs->players[1].bet_amount = 150;
    gs->players[0].stack_size = 800;
    gs->players[1].stack_size = 1200;
    gs->raises_this_round = 2;

    st = pe_betting_engine_get_valid_actions(pot_engine, state, 0, &actions, &count);
    if (st != PE_STATUS_OK)
        return 1;
    assert(actions != NULL);
    assert(count == 4); /* fold, call, raise, all-in */
    assert(actions[2].action == ACTION_RAISE);

    int64_t pot_min = 0;
    st = pe_betting_engine_get_min_bet(pot_engine, state, 0, &pot_min);
    if (st != PE_STATUS_OK)
        return 1;
    assert(pot_min == 100);

    int64_t pot_max = 0;
    st = pe_betting_engine_get_max_bet(pot_engine, state, 0, &pot_max);
    if (st != PE_STATUS_OK)
        return 1;
    assert(pot_max == 700);

    pe_betting_engine_destroy(pot_engine);

    /* Scenario 5: Fixed-limit raise sizes */
    pe_betting_engine_t *limit_engine = NULL;
    st = pe_betting_engine_create(BETTING_LIMIT, NULL, &limit_engine);
    if (st != PE_STATUS_OK)
        return 1;
    assert(limit_engine != NULL);

    gs->rules.betting_structure = BETTING_LIMIT;
    gs->players[0].stack_size = 1000;
    gs->players[1].stack_size = 1200;
    gs->players[0].bet_amount = 100;
    gs->players[1].bet_amount = 200;
    gs->pot_size = 400;
    gs->raises_this_round = 1;

    st = pe_betting_engine_get_valid_actions(limit_engine, state, 0, &actions, &count);
    if (st != PE_STATUS_OK)
        return 1;
    assert(actions != NULL);
    assert(count == 4); /* fold, call, raise, all-in */
    assert(actions[1].action == ACTION_CALL);
    assert(actions[1].amount == 100);
    assert(actions[2].action == ACTION_RAISE);
    assert(actions[2].min_amount == 200);
    assert(actions[2].max_amount == 300);

    pe_betting_engine_destroy(limit_engine);

    /* Scenario 6: Spread-limit bet window */
    pe_betting_engine_t *spread_engine = NULL;
    st = pe_betting_engine_create(BETTING_SPREAD_LIMIT, NULL, &spread_engine);
    if (st != PE_STATUS_OK)
        return 1;
    assert(spread_engine != NULL);

    gs->rules.betting_structure = BETTING_SPREAD_LIMIT;
    gs->players[0].stack_size = 600;
    gs->players[1].stack_size = 600;
    gs->players[0].bet_amount = 0;
    gs->players[1].bet_amount = 0;
    gs->pot_size = 200;
    gs->raises_this_round = 0;

    st = pe_betting_engine_get_valid_actions(spread_engine, state, 0, &actions, &count);
    if (st != PE_STATUS_OK)
        return 1;
    assert(actions != NULL);
    assert(actions[0].action == ACTION_CHECK);
    assert(actions[1].action == ACTION_BET);
    assert(actions[1].min_amount == 100);
    assert(actions[1].max_amount == 500);

    pe_betting_engine_destroy(spread_engine);

    pe_betting_engine_destroy(betting);
    pe_game_state_destroy(state);
    return 0;
}
