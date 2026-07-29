#include <poker_eval/engine/betting_engine.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Betting engine lifecycle */
BettingEngine* betting_engine_create(betting_structure_t structure, const BettingLimits* limits) {
    BettingEngine* engine = calloc(1, sizeof(BettingEngine));
    if (!engine) {
        return NULL;
    }

    engine->structure = structure;
    if (limits) {
        engine->limits = *limits;
    } else {
        /* Set default limits based on structure */
        switch (structure) {
            case BETTING_LIMIT:
                engine->limits.min_bet = 100;
                engine->limits.max_bet = 200;
                engine->limits.min_raise = 100;
                engine->limits.max_raise = 200;
                engine->limits.cap_raises = true;
                engine->limits.max_raises_per_round = 3;
                break;

            case BETTING_NO_LIMIT:
                engine->limits.min_bet = 100;
                engine->limits.max_bet = INT64_MAX;
                engine->limits.min_raise = 100;
                engine->limits.max_raise = INT64_MAX;
                engine->limits.cap_raises = false;
                engine->limits.max_raises_per_round = INT32_MAX;
                break;

            case BETTING_POT_LIMIT:
                engine->limits.min_bet = 100;
                engine->limits.max_bet = 0;  /* Calculated dynamically */
                engine->limits.min_raise = 100;
                engine->limits.max_raise = 0;  /* Calculated dynamically */
                engine->limits.cap_raises = false;
                engine->limits.max_raises_per_round = INT32_MAX;
                engine->calculate_pot_limit_dynamically = true;
                break;

            case BETTING_SPREAD_LIMIT:
                engine->limits.min_bet = 100;
                engine->limits.max_bet = 500;
                engine->limits.min_raise = 100;
                engine->limits.max_raise = 500;
                engine->limits.cap_raises = false;
                engine->limits.max_raises_per_round = INT32_MAX;
                engine->spread_min = 100;
                engine->spread_max = 500;
                break;

            case BETTING_STRUCTURE_COUNT:
            default:
                /* Default to no-limit */
                engine->limits.min_bet = 100;
                engine->limits.max_bet = INT64_MAX;
                engine->limits.min_raise = 100;
                engine->limits.max_raise = INT64_MAX;
                engine->limits.cap_raises = false;
                engine->limits.max_raises_per_round = INT32_MAX;
                break;
        }
    }

    engine->cap_raises = engine->limits.cap_raises;
    engine->max_raises_per_round = engine->limits.max_raises_per_round;

    return engine;
}

void betting_engine_destroy(BettingEngine* engine) {
    if (engine) {
        free(engine);
    }
}

/* Action calculation */
BettingAction* betting_engine_get_valid_actions(BettingEngine* engine, const GameState* state, int player_id) {
    if (!engine || !state || player_id < 0 || player_id >= state->num_players) {
        return NULL;
    }

    const PlayerState* player = &state->players[player_id];
    if (!betting_engine_can_player_act(state, player_id)) {
        return NULL;
    }

    /* Allocate space for maximum possible actions */
    BettingAction* actions = calloc(ACTION_COUNT, sizeof(BettingAction));
    if (!actions) {
        return NULL;
    }

    int action_count = 0;
    BettingContext context = betting_engine_create_context(state, player_id);

    /* Fold - always available (except when checking is free) */
    if (context.amount_to_call > 0) {
        actions[action_count].action = ACTION_FOLD;
        actions[action_count].amount = 0;
        actions[action_count].is_valid = true;
        actions[action_count].description = "Fold hand";
        action_count++;
    }

    /* Check - available if no bet to call */
    if (context.amount_to_call == 0) {
        actions[action_count].action = ACTION_CHECK;
        actions[action_count].amount = 0;
        actions[action_count].is_valid = true;
        actions[action_count].description = "Check";
        action_count++;
    }

    /* Call - available if there's a bet to call */
    if (context.amount_to_call > 0) {
        int64_t call_amount = (context.amount_to_call > player->stack_size) ?
                              player->stack_size : context.amount_to_call;
        actions[action_count].action = ACTION_CALL;
        actions[action_count].amount = call_amount;
        actions[action_count].is_valid = true;
        actions[action_count].description = (call_amount == player->stack_size) ?
                                          "Call all-in" : "Call";
        action_count++;
    }

    /* Bet - available if no previous bet this round */
    if (context.current_bet == 0 && player->stack_size > 0) {
        int64_t min_bet = betting_engine_calculate_min_bet(engine, state, player_id);
        int64_t max_bet = betting_engine_calculate_max_bet(engine, state, player_id);

        if (min_bet <= max_bet && min_bet <= player->stack_size) {
            actions[action_count].action = ACTION_BET;
            actions[action_count].amount = min_bet;
            actions[action_count].min_amount = min_bet;
            actions[action_count].max_amount = max_bet;
            actions[action_count].is_valid = true;
            actions[action_count].description = "Bet";
            action_count++;
        }
    }

    /* Raise - available if there's a bet and raises aren't capped */
    if (context.amount_to_call > 0 && !betting_engine_is_betting_capped(engine, state)) {
        int64_t min_raise = betting_engine_calculate_min_raise(engine, state, player_id);
        int64_t max_raise = betting_engine_calculate_max_raise(engine, state, player_id);

        if (min_raise <= max_raise && min_raise <= player->stack_size) {
            actions[action_count].action = ACTION_RAISE;
            actions[action_count].amount = min_raise;
            actions[action_count].min_amount = min_raise;
            actions[action_count].max_amount = max_raise;
            actions[action_count].is_valid = true;
            actions[action_count].description = "Raise";
            action_count++;
        }
    }

    /* All-in - always available if player has chips */
    if (player->stack_size > 0) {
        actions[action_count].action = ACTION_ALL_IN;
        actions[action_count].amount = player->stack_size;
        actions[action_count].is_valid = true;
        actions[action_count].description = "All-in";
        action_count++;
    }

    engine->actions_calculated++;

    /* Resize array to actual size (+1 sentinel with is_valid=false) */
    if (action_count == 0) {
        free(actions);
        return NULL;
    }
    BettingAction* result = realloc(actions, (action_count + 1) * sizeof(BettingAction));
    if (!result) {
        /* Fallback to original buffer; ensure sentinel exists */
        actions[action_count].is_valid = false;
        return actions;
    }
    result[action_count].is_valid = false;
    return result;
}

int betting_engine_get_num_valid_actions(BettingEngine* engine, const GameState* state, int player_id) {
    BettingAction* actions = betting_engine_get_valid_actions(engine, state, player_id);
    if (!actions) {
        return 0;
    }

    int count = 0;
    while (actions[count].is_valid && count < ACTION_COUNT) {
        count++;
    }

    free(actions);
    return count;
}

bool betting_engine_is_action_valid(BettingEngine* engine, const GameState* state,
                                   player_action_type_t action, int64_t amount, int player_id) {
    BettingAction* actions = betting_engine_get_valid_actions(engine, state, player_id);
    if (!actions) {
        return false;
    }

    bool valid = false;
    for (int i = 0; actions[i].is_valid && i < ACTION_COUNT; i++) {
        if (actions[i].action == action) {
            if (action == ACTION_FOLD || action == ACTION_CHECK || action == ACTION_ALL_IN) {
                valid = true;  /* Amount doesn't matter for these actions */
                break;
            } else if (action == ACTION_CALL) {
                valid = (amount == actions[i].amount);
                break;
            } else if (action == ACTION_BET || action == ACTION_RAISE) {
                valid = (amount >= actions[i].min_amount && amount <= actions[i].max_amount);
                break;
            }
        }
    }

    free(actions);
    return valid;
}

/* Amount calculations */
int64_t betting_engine_calculate_min_bet(BettingEngine* engine, const GameState* state, int player_id) {
    if (!engine || !state) {
        return 0;
    }

    switch (engine->structure) {
        case BETTING_LIMIT:
            return engine->limits.min_bet;

        case BETTING_NO_LIMIT:
        case BETTING_POT_LIMIT:
            return engine->limits.min_bet;

        case BETTING_SPREAD_LIMIT:
            return engine->spread_min;

        case BETTING_STRUCTURE_COUNT:
        default:
            return engine->limits.min_bet;
    }
}

int64_t betting_engine_calculate_max_bet(BettingEngine* engine, const GameState* state, int player_id) {
    if (!engine || !state || player_id < 0 || player_id >= state->num_players) {
        return 0;
    }

    const PlayerState* player = &state->players[player_id];

    switch (engine->structure) {
        case BETTING_LIMIT:
            return (engine->limits.max_bet > player->stack_size) ?
                   player->stack_size : engine->limits.max_bet;

        case BETTING_NO_LIMIT:
            return player->stack_size;

        case BETTING_POT_LIMIT: {
            int64_t pot_limit = betting_engine_calculate_pot_limit(engine, state, player_id);
            return (pot_limit > player->stack_size) ? player->stack_size : pot_limit;
        }

        case BETTING_SPREAD_LIMIT:
            return (engine->spread_max > player->stack_size) ?
                   player->stack_size : engine->spread_max;

        case BETTING_STRUCTURE_COUNT:
        default:
            return player->stack_size;
    }
}

int64_t betting_engine_calculate_min_raise(BettingEngine* engine, const GameState* state, int player_id) {
    if (!engine || !state || player_id < 0 || player_id >= state->num_players) {
        return 0;
    }

    int64_t amount_to_call = game_engine_get_amount_to_call(state, player_id);
    int64_t last_raise = betting_engine_get_last_raise_amount(state);

    switch (engine->structure) {
        case BETTING_LIMIT:
            return amount_to_call + engine->limits.min_raise;

        case BETTING_NO_LIMIT:
        case BETTING_POT_LIMIT: {
            /* Minimum raise is call + last raise amount, or min raise if first raise */
            int64_t min_raise_increment = (last_raise > 0) ? last_raise : engine->limits.min_raise;
            return amount_to_call + min_raise_increment;
        }

        case BETTING_SPREAD_LIMIT:
            return amount_to_call + engine->spread_min;

        case BETTING_STRUCTURE_COUNT:
        default:
            return amount_to_call + engine->limits.min_raise;
    }
}

int64_t betting_engine_calculate_max_raise(BettingEngine* engine, const GameState* state, int player_id) {
    if (!engine || !state || player_id < 0 || player_id >= state->num_players) {
        return 0;
    }

    const PlayerState* player = &state->players[player_id];
    int64_t amount_to_call = game_engine_get_amount_to_call(state, player_id);

    switch (engine->structure) {
        case BETTING_LIMIT: {
            int64_t max_raise_total = amount_to_call + engine->limits.max_raise;
            return (max_raise_total > player->stack_size) ? player->stack_size : max_raise_total;
        }

        case BETTING_NO_LIMIT:
            return player->stack_size;

        case BETTING_POT_LIMIT: {
            int64_t pot_limit = betting_engine_calculate_pot_limit(engine, state, player_id);
            return (pot_limit > player->stack_size) ? player->stack_size : pot_limit;
        }

        case BETTING_SPREAD_LIMIT: {
            int64_t max_raise_total = amount_to_call + engine->spread_max;
            return (max_raise_total > player->stack_size) ? player->stack_size : max_raise_total;
        }

        case BETTING_STRUCTURE_COUNT:
        default:
            return player->stack_size;
    }
}

/* Pot limit calculations */
int64_t betting_engine_calculate_pot_limit(BettingEngine* engine, const GameState* state, int player_id) {
    if (!engine || !state || player_id < 0 || player_id >= state->num_players) {
        return 0;
    }

    engine->pot_calculations++;

    /* Pot limit = size of pot + amount to call + amount to call again */
    int64_t effective_pot = betting_engine_calculate_effective_pot_size(engine, state, player_id);
    int64_t amount_to_call = game_engine_get_amount_to_call(state, player_id);

    /* Pot limit calculation: pot + call + call again */
    return effective_pot + amount_to_call + amount_to_call;
}

int64_t betting_engine_calculate_effective_pot_size(BettingEngine* engine, const GameState* state, int player_id) {
    if (!engine || !state) {
        return 0;
    }

    /* Total pot includes all bets made this hand */
    int64_t total_pot = state->pot_size;

    /* Add all current round bets that haven't been added to main pot yet */
    for (int i = 0; i < state->num_players; i++) {
        total_pot += state->players[i].bet_amount;
    }

    return total_pot;
}

/* Utility functions */
BettingContext betting_engine_create_context(const GameState* state, int player_id) {
    BettingContext context = {0};

    if (!state || player_id < 0 || player_id >= state->num_players) {
        return context;
    }

    context.game_state = state;
    context.player_id = player_id;
    context.pot_size = state->pot_size;
    context.amount_to_call = game_engine_get_amount_to_call(state, player_id);
    context.current_bet = betting_engine_get_highest_bet(state);
    context.last_raise_amount = betting_engine_get_last_raise_amount(state);
    context.num_raises_this_round = state->raises_this_round;
    context.is_heads_up = (game_engine_get_active_player_count(state) == 2);

    return context;
}

int64_t betting_engine_get_highest_bet(const GameState* state) {
    if (!state) {
        return 0;
    }

    int64_t highest = 0;
    for (int i = 0; i < state->num_players; i++) {
        if (state->players[i].bet_amount > highest) {
            highest = state->players[i].bet_amount;
        }
    }
    return highest;
}

int64_t betting_engine_get_last_raise_amount(const GameState* state) {
    if (!state || state->raises_this_round == 0) {
        return 0;
    }

    /* This is a simplified implementation - in production, you'd track raise history */
    int64_t highest_bet = betting_engine_get_highest_bet(state);
    int64_t second_highest = 0;

    /* Find second highest bet to calculate last raise amount */
    for (int i = 0; i < state->num_players; i++) {
        int64_t bet = state->players[i].bet_amount;
        if (bet < highest_bet && bet > second_highest) {
            second_highest = bet;
        }
    }

    return highest_bet - second_highest;
}

bool betting_engine_can_player_act(const GameState* state, int player_id) {
    if (!state || player_id < 0 || player_id >= state->num_players) {
        return false;
    }

    const PlayerState* player = &state->players[player_id];
    return player->is_active && !player->has_folded && !player->is_all_in;
}

/* Betting structure helpers */
bool betting_engine_is_limit_structure(betting_structure_t structure) {
    return structure == BETTING_LIMIT;
}

bool betting_engine_is_no_limit_structure(betting_structure_t structure) {
    return structure == BETTING_NO_LIMIT;
}

bool betting_engine_is_pot_limit_structure(betting_structure_t structure) {
    return structure == BETTING_POT_LIMIT;
}

bool betting_engine_is_betting_capped(BettingEngine* engine, const GameState* state) {
    if (!engine || !state) {
        return false;
    }

    return engine->cap_raises && (state->raises_this_round >= engine->max_raises_per_round);
}

/* Side pot calculation */
SidePotCalculation* betting_engine_calculate_side_pots(BettingEngine* engine, const GameState* state) {
    if (!engine || !state) {
        return NULL;
    }

    SidePotCalculation* calc = calloc(1, sizeof(SidePotCalculation));
    if (!calc) {
        return NULL;
    }

    /* Collect all player investments sorted by amount */
    typedef struct {
        int player_id;
        int64_t total_investment;
    } PlayerInvestment;

    PlayerInvestment investments[MAX_PLAYERS];
    int num_investments = 0;

    for (int i = 0; i < state->num_players; i++) {
        if (state->players[i].total_investment > 0) {
            investments[num_investments].player_id = i;
            investments[num_investments].total_investment = state->players[i].total_investment;
            num_investments++;
        }
    }

    /* Sort by investment amount */
    for (int i = 0; i < num_investments - 1; i++) {
        for (int j = i + 1; j < num_investments; j++) {
            if (investments[i].total_investment > investments[j].total_investment) {
                PlayerInvestment temp = investments[i];
                investments[i] = investments[j];
                investments[j] = temp;
            }
        }
    }

    /* Create side pots */
    int64_t previous_level = 0;
    calc->num_side_pots = 0;

    for (int level = 0; level < num_investments; level++) {
        int64_t current_level = investments[level].total_investment;
        int64_t level_contribution = current_level - previous_level;

        if (level_contribution > 0) {
            SidePot* pot = &calc->side_pots[calc->num_side_pots];
            pot->amount = 0;
            pot->num_eligible_players = 0;

            /* Add all players who contributed to this level */
            for (int i = level; i < num_investments; i++) {
                int player_id = investments[i].player_id;
                pot->eligible_players[pot->num_eligible_players] = player_id;
                pot->contribution_per_player[player_id] = level_contribution;
                pot->amount += level_contribution;
                pot->num_eligible_players++;
            }

            calc->total_pot_value += pot->amount;
            calc->num_side_pots++;
        }

        previous_level = current_level;
    }

    return calc;
}

void betting_engine_free_side_pot_calculation(SidePotCalculation* calculation) {
    if (calculation) {
        free(calculation);
    }
}

/* Debug functions */
void betting_engine_print_actions(const BettingAction* actions, int num_actions) {
    if (!actions) {
        printf("No valid actions\n");
        return;
    }

    printf("=== Valid Betting Actions ===\n");
    for (int i = 0; i < num_actions; i++) {
        if (!actions[i].is_valid) break;

        printf("%d. %s", i + 1, game_engine_action_to_string(actions[i].action));
        if (actions[i].amount > 0) {
            printf(" %lld", (long long)actions[i].amount);
        }
        if (actions[i].min_amount != actions[i].max_amount) {
            printf(" (range: %lld-%lld)", (long long)actions[i].min_amount, (long long)actions[i].max_amount);
        }
        if (actions[i].description) {
            printf(" - %s", actions[i].description);
        }
        printf("\n");
    }
    printf("==============================\n");
}

void betting_engine_print_side_pots(const SidePotCalculation* calculation) {
    if (!calculation) {
        printf("No side pot calculation\n");
        return;
    }

    printf("=== Side Pot Calculation ===\n");
    printf("Total pot value: %lld\n", (long long)calculation->total_pot_value);
    printf("Number of side pots: %d\n", calculation->num_side_pots);

    for (int i = 0; i < calculation->num_side_pots; i++) {
        const SidePot* pot = &calculation->side_pots[i];
        printf("Side pot %d: %lld (eligible: ", i + 1, (long long)pot->amount);
        for (int j = 0; j < pot->num_eligible_players; j++) {
            printf("%d", pot->eligible_players[j]);
            if (j < pot->num_eligible_players - 1) printf(",");
        }
        printf(")\n");
    }
    printf("=============================\n");
}
