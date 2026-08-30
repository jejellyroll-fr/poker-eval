#include <poker_eval/solver/pe_betting_state.h>

#include <float.h>
#include <math.h>
#include <string.h>

static int finite_nonnegative(double value)
{
    /* Relational checks reject NaN and avoid MinGW's isfinite macro
       narrowing a double expression under -Wfloat-conversion. */
    return value >= 0.0 && value <= DBL_MAX;
}

static double epsilon_for(const pe_betting_rules_t *rules)
{
    return rules->epsilon > 0.0 ? rules->epsilon : 1e-9;
}

static int active_count(const pe_betting_state_t *state)
{
    int count = 0;
    uint8_t player;
    for (player = 0u; player < state->player_count; ++player)
        if (state->active[player])
            count++;
    return count;
}

static int next_actor(const pe_betting_state_t *state, int from)
{
    uint8_t offset;
    for (offset = 1u; offset <= state->player_count; ++offset)
    {
        int player = (from + (int)offset) % (int)state->player_count;
        if (state->active[player] && !state->all_in[player])
            return player;
    }
    return -1;
}

static void finish_if_ready(pe_betting_state_t *state)
{
    const double epsilon = 1e-9;
    uint8_t player;
    int pending = 0;

    if (active_count(state) <= 1)
    {
        state->terminal = 1;
        state->round_complete = 1;
        state->winner = -1;
        for (player = 0u; player < state->player_count; ++player)
            if (state->active[player])
                state->winner = player;
        state->to_act = -1;
        return;
    }
    for (player = 0u; player < state->player_count; ++player)
    {
        if (!state->active[player] || state->all_in[player])
            continue;
        if (!state->acted[player] ||
            state->round_contrib[player] + epsilon < state->to_call)
        {
            pending = 1;
            break;
        }
    }
    if (!pending)
    {
        state->round_complete = 1;
        state->to_act = -1;
    }
}

void pe_betting_rules_default(pe_betting_rules_t *out, uint8_t player_count)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->player_count = player_count;
    out->epsilon = 1e-9;
    out->min_raise = 1.0;
    out->raise_cap = 0;
}

pe_betting_status_t pe_betting_state_init(
    pe_betting_state_t *out,
    const pe_betting_rules_t *rules,
    const double *stacks,
    uint8_t player_count,
    int first_to_act,
    double pot,
    double to_call)
{
    uint8_t player;
    if (!out || !rules || !stacks)
        return PE_BETTING_ERR_NULL_ARGUMENT;
    if (player_count == 0u || player_count > PE_BETTING_MAX_PLAYERS ||
        player_count != rules->player_count || first_to_act < 0 ||
        first_to_act >= player_count || !finite_nonnegative(pot) ||
        !finite_nonnegative(to_call) || !finite_nonnegative(rules->min_raise) ||
        (rules->pot_limit != 0 && rules->pot_limit != 1) ||
        (rules->raise_cap < 0))
        return PE_BETTING_ERR_INVALID_STATE;
    memset(out, 0, sizeof(*out));
    out->player_count = player_count;
    out->to_act = (int8_t)first_to_act;
    out->pot = pot;
    out->to_call = to_call;
    out->current_bet = to_call;
    out->min_raise = rules->min_raise;
    out->winner = -1;
    for (player = 0u; player < player_count; ++player)
    {
        if (!finite_nonnegative(stacks[player]) || stacks[player] <= 0.0)
            return PE_BETTING_ERR_INVALID_STATE;
        out->active[player] = 1;
        out->stack[player] = stacks[player];
    }
    return pe_betting_state_validate(out, rules);
}

pe_betting_status_t pe_betting_state_validate(
    const pe_betting_state_t *state,
    const pe_betting_rules_t *rules)
{
    const double epsilon = rules ? epsilon_for(rules) : 1e-9;
    uint8_t player;
    if (!state || !rules)
        return PE_BETTING_ERR_NULL_ARGUMENT;
    if (state->player_count == 0u ||
        state->player_count > PE_BETTING_MAX_PLAYERS ||
        state->player_count != rules->player_count || state->to_act < -1 ||
        state->to_act >= (int8_t)state->player_count ||
        !finite_nonnegative(state->pot) || !finite_nonnegative(state->to_call) ||
        !finite_nonnegative(state->current_bet) ||
        !finite_nonnegative(state->min_raise) || state->raises_made > 10000u)
        return PE_BETTING_ERR_INVALID_STATE;
    if (state->current_bet + epsilon < state->to_call)
        return PE_BETTING_ERR_INVALID_STATE;
    for (player = 0u; player < state->player_count; ++player)
    {
        if ((state->active[player] != 0 && state->active[player] != 1) ||
            (state->all_in[player] != 0 && state->all_in[player] != 1) ||
            (state->acted[player] != 0 && state->acted[player] != 1) ||
            !finite_nonnegative(state->stack[player]) ||
            !finite_nonnegative(state->round_contrib[player]) ||
            !finite_nonnegative(state->invested[player]))
            return PE_BETTING_ERR_INVALID_STATE;
        if (state->all_in[player] && state->stack[player] > epsilon)
            return PE_BETTING_ERR_INVALID_STATE;
        if (state->round_contrib[player] > state->invested[player] + epsilon)
            return PE_BETTING_ERR_INVALID_STATE;
    }
    if (!state->terminal && !state->round_complete && state->to_act < 0)
        return PE_BETTING_ERR_INVALID_STATE;
    return PE_BETTING_OK;
}

pe_betting_status_t pe_betting_action_is_legal(
    const pe_betting_state_t *state,
    const pe_betting_rules_t *rules,
    const pe_action_t *action)
{
    pe_betting_status_t state_status;
    pe_action_status_t action_status;
    double epsilon;
    double commitment;
    double previous_contribution;
    double outstanding;

    state_status = pe_betting_state_validate(state, rules);
    if (state_status != PE_BETTING_OK)
        return state_status;
    if (!action)
        return PE_BETTING_ERR_NULL_ARGUMENT;
    if (state->terminal)
        return PE_BETTING_ERR_TERMINAL;
    if (state->round_complete)
        return PE_BETTING_ERR_ROUND_COMPLETE;
    if (state->to_act < 0 || !state->active[state->to_act] ||
        state->all_in[state->to_act])
        return PE_BETTING_ERR_INVALID_STATE;

    action_status = pe_action_validate(action);
    if (action_status != PE_ACTION_OK)
        return PE_BETTING_ERR_ILLEGAL_ACTION;
    epsilon = epsilon_for(rules);
    previous_contribution = state->round_contrib[state->to_act];
    outstanding = state->to_call > previous_contribution
                      ? state->to_call - previous_contribution
                      : 0.0;
    if (action->kind == PE_ACTION_CHECK && outstanding > epsilon)
        return PE_BETTING_ERR_ILLEGAL_ACTION;
    if (action->kind == PE_ACTION_CALL && state->to_call <= epsilon)
        return PE_BETTING_OK;
    if (action->kind == PE_ACTION_CALL)
        return outstanding <= state->stack[state->to_act] + epsilon
                   ? PE_BETTING_OK
                   : PE_BETTING_ERR_ILLEGAL_ACTION;
    if (action->kind == PE_ACTION_BET && state->to_call > epsilon)
        return PE_BETTING_ERR_ILLEGAL_ACTION;
    if (action->kind == PE_ACTION_RAISE && state->to_call <= epsilon)
        return PE_BETTING_ERR_ILLEGAL_ACTION;
    if ((action->kind == PE_ACTION_BET || action->kind == PE_ACTION_RAISE) &&
        rules->raise_cap > 0 && state->raises_made >= (uint16_t)rules->raise_cap)
        return PE_BETTING_ERR_ILLEGAL_ACTION;
    /* A short all-in does not reopen betting for players that already acted.
       apply_action deliberately preserves their acted flags in that case;
       enforce the flag here so callers cannot manufacture a raise where only
       call or fold is legal. */
    if ((action->kind == PE_ACTION_BET || action->kind == PE_ACTION_RAISE ||
         action->kind == PE_ACTION_ALL_IN) && state->acted[state->to_act])
        return PE_BETTING_ERR_ILLEGAL_ACTION;
    if (action->kind == PE_ACTION_CHANCE || action->kind == PE_ACTION_TERMINAL)
        return PE_BETTING_ERR_ILLEGAL_ACTION;
    if (action->kind == PE_ACTION_FOLD || action->kind == PE_ACTION_CHECK ||
        action->kind == PE_ACTION_ALL_IN)
        return PE_BETTING_OK;

    action_status = pe_action_commitment(
        action, outstanding, state->stack[state->to_act], state->pot,
        state->min_raise, rules->pot_limit, &commitment);
    return action_status == PE_ACTION_OK ? PE_BETTING_OK
                                         : PE_BETTING_ERR_ILLEGAL_ACTION;
}

pe_betting_status_t pe_betting_apply_action(
    const pe_betting_state_t *state,
    const pe_betting_rules_t *rules,
    const pe_action_t *action,
    pe_betting_state_t *out)
{
    pe_betting_status_t status;
    double epsilon;
    const int player = state ? state->to_act : -1;
    double commitment = 0.0;
    double previous_to_call;
    double previous_contribution;
    double increment;
    int full_raise = 0;
    uint8_t other;

    if (!out)
        return PE_BETTING_ERR_NULL_ARGUMENT;
    if (!state || !rules || !action)
        return PE_BETTING_ERR_NULL_ARGUMENT;
    status = pe_betting_action_is_legal(state, rules, action);
    if (status != PE_BETTING_OK)
        return status;
    *out = *state;
    epsilon = epsilon_for(rules);
    previous_to_call = state->to_call;
    previous_contribution = state->round_contrib[player];

    if (action->kind == PE_ACTION_FOLD)
    {
        out->active[player] = 0;
        out->acted[player] = 1;
        if (active_count(out) <= 1)
        {
            out->terminal = 1;
            out->round_complete = 1;
            out->winner = -1;
            for (other = 0u; other < out->player_count; ++other)
                if (out->active[other])
                    out->winner = other;
            out->to_act = -1;
            return PE_BETTING_OK;
        }
    }
    else
    {
        if (action->kind == PE_ACTION_CHECK ||
            (action->kind == PE_ACTION_CALL && state->to_call <= epsilon))
            commitment = 0.0;
        else if (action->kind == PE_ACTION_CALL)
            commitment = state->to_call > previous_contribution
                             ? state->to_call - previous_contribution
                             : 0.0;
        else
        {
            double outstanding = state->to_call > previous_contribution
                                     ? state->to_call - previous_contribution
                                     : 0.0;
            pe_action_status_t action_status = pe_action_commitment(
                action, outstanding, state->stack[player], state->pot,
                state->min_raise, rules->pot_limit, &commitment);
            if (action_status != PE_ACTION_OK)
                return PE_BETTING_ERR_ILLEGAL_ACTION;
        }
        if (commitment > out->stack[player] + epsilon)
            return PE_BETTING_ERR_INVALID_STATE;
        out->stack[player] -= commitment;
        out->round_contrib[player] += commitment;
        out->invested[player] += commitment;
        out->pot += commitment;
        if (out->stack[player] <= epsilon)
        {
            out->stack[player] = 0.0;
            out->all_in[player] = 1;
        }
        out->acted[player] = 1;

        if (action->kind == PE_ACTION_BET || action->kind == PE_ACTION_RAISE ||
            action->kind == PE_ACTION_ALL_IN)
        {
            if (out->round_contrib[player] > previous_to_call + epsilon)
            {
                increment = out->round_contrib[player] - previous_to_call;
                out->to_call = out->round_contrib[player];
                out->current_bet = out->round_contrib[player];
                full_raise = increment + epsilon >= state->min_raise;
                if (full_raise)
                {
                    out->min_raise = increment;
                    out->raises_made++;
                    for (other = 0u; other < out->player_count; ++other)
                        if (out->active[other] && other != (uint8_t)player &&
                            !out->all_in[other])
                            out->acted[other] = 0;
                }
            }
        }
    }
    out->to_act = (int8_t)next_actor(out, player);
    finish_if_ready(out);
    return pe_betting_state_validate(out, rules);
}

const char *pe_betting_status_string(pe_betting_status_t status)
{
    switch (status)
    {
    case PE_BETTING_OK:
        return "ok";
    case PE_BETTING_ERR_NULL_ARGUMENT:
        return "null argument";
    case PE_BETTING_ERR_INVALID_STATE:
        return "invalid state";
    case PE_BETTING_ERR_ILLEGAL_ACTION:
        return "illegal action";
    case PE_BETTING_ERR_OUT_OF_RANGE:
        return "out of range";
    case PE_BETTING_ERR_TERMINAL:
        return "terminal";
    case PE_BETTING_ERR_ROUND_COMPLETE:
        return "round complete";
    default:
        return "unknown betting status";
    }
}
