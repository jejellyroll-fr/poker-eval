#include <poker_eval/solver/pe_holdem_round.h>

static int board_is_betting_street(mask_t board,
                                   pe_holdem_street_t *out_street)
{
    if (pe_holdem_street_from_board(board, out_street) != 0 ||
        *out_street == PE_HOLDEM_SHOWDOWN)
        return 0;
    return 1;
}

static int board_transition_is_valid(mask_t board, mask_t next_board,
                                     mask_t dead_cards)
{
    pe_holdem_street_t street;
    pe_holdem_street_t next_street;
    uint8_t expected;

    if (!board_is_betting_street(board, &street) ||
        pe_holdem_street_from_board(next_board, &next_street) != 0 ||
        next_street == PE_HOLDEM_PREFLOP ||
        (board & dead_cards) != MASK_EMPTY ||
        (next_board & dead_cards) != MASK_EMPTY ||
        (next_board & board) != board)
        return 0;
    expected = pe_holdem_next_public_count(street);
    if (expected == 0u ||
        !((street == PE_HOLDEM_PREFLOP && next_street == PE_HOLDEM_FLOP) ||
          (street == PE_HOLDEM_FLOP && next_street == PE_HOLDEM_TURN) ||
          (street == PE_HOLDEM_TURN && next_street == PE_HOLDEM_RIVER)))
        return 0;
    return mask_popcount(next_board ^ board) == (int)expected;
}

pe_holdem_round_status_t pe_holdem_round_init(
    pe_holdem_round_state_t *out,
    mask_t board,
    mask_t dead_cards,
    const pe_betting_rules_t *rules,
    const double *stacks,
    uint8_t player_count,
    int first_to_act,
    double pot,
    double to_call)
{
    pe_holdem_street_t street;
    pe_betting_status_t status;

    if (!out || !rules || !stacks)
        return PE_HOLDEM_ROUND_ERR_NULL_ARGUMENT;
    if (!board_is_betting_street(board, &street) ||
        !mask_is_valid(dead_cards) || (board & dead_cards) != MASK_EMPTY)
        return PE_HOLDEM_ROUND_ERR_INVALID_BOARD;
    status = pe_betting_state_init(&out->betting, rules, stacks,
                                   player_count, first_to_act, pot, to_call);
    if (status != PE_BETTING_OK)
        return PE_HOLDEM_ROUND_ERR_BETTING;
    out->board = board;
    out->dead_cards = dead_cards;
    out->street = street;
    return PE_HOLDEM_ROUND_OK;
}

pe_holdem_round_status_t pe_holdem_round_apply_action(
    const pe_holdem_round_state_t *state,
    const pe_betting_rules_t *rules,
    const pe_action_t *action,
    pe_holdem_round_state_t *out)
{
    pe_betting_status_t status;

    if (!state || !rules || !action || !out)
        return PE_HOLDEM_ROUND_ERR_NULL_ARGUMENT;
    if (!board_is_betting_street(state->board, &out->street) ||
        (state->board & state->dead_cards) != MASK_EMPTY)
        return PE_HOLDEM_ROUND_ERR_INVALID_STATE;
    status = pe_betting_apply_action(&state->betting, rules, action,
                                     &out->betting);
    if (status != PE_BETTING_OK)
        return PE_HOLDEM_ROUND_ERR_BETTING;
    out->board = state->board;
    out->dead_cards = state->dead_cards;
    return PE_HOLDEM_ROUND_OK;
}

pe_holdem_round_status_t pe_holdem_round_advance(
    const pe_holdem_round_state_t *state,
    const pe_betting_rules_t *rules,
    mask_t next_board,
    int first_to_act,
    pe_holdem_round_state_t *out)
{
    uint8_t player;
    pe_holdem_street_t next_street;

    if (!state || !rules || !out)
        return PE_HOLDEM_ROUND_ERR_NULL_ARGUMENT;
    if (!board_transition_is_valid(state->board, next_board,
                                   state->dead_cards) ||
        state->betting.terminal || !state->betting.round_complete ||
        pe_holdem_street_from_board(next_board, &next_street) != 0)
        return PE_HOLDEM_ROUND_ERR_INVALID_BOARD;
    if (first_to_act < 0 || first_to_act >= state->betting.player_count)
        return PE_HOLDEM_ROUND_ERR_INVALID_STATE;

    *out = *state;
    out->board = next_board;
    out->street = next_street;
    out->betting.to_act = (int8_t)first_to_act;
    out->betting.to_call = 0.0;
    out->betting.current_bet = 0.0;
    out->betting.min_raise = rules->min_raise;
    out->betting.raises_made = 0u;
    out->betting.round_complete = 0;
    out->betting.terminal = 0;
    out->betting.winner = -1;
    for (player = 0u; player < out->betting.player_count; ++player)
    {
        out->betting.acted[player] = 0;
        out->betting.round_contrib[player] = 0.0;
    }
    if (pe_betting_state_validate(&out->betting, rules) != PE_BETTING_OK)
        return PE_HOLDEM_ROUND_ERR_BETTING;
    return PE_HOLDEM_ROUND_OK;
}

pe_holdem_round_status_t pe_holdem_round_showdown(
    const pe_holdem_round_state_t *state,
    const pe_betting_rules_t *rules,
    pe_holdem_round_state_t *out)
{
    if (!state || !rules || !out)
        return PE_HOLDEM_ROUND_ERR_NULL_ARGUMENT;
    if (state->street != PE_HOLDEM_RIVER ||
        !state->betting.round_complete || state->betting.terminal)
        return PE_HOLDEM_ROUND_ERR_INVALID_STATE;
    *out = *state;
    out->street = PE_HOLDEM_SHOWDOWN;
    out->betting.terminal = 1;
    out->betting.to_act = -1;
    if (pe_betting_state_validate(&out->betting, rules) != PE_BETTING_OK)
        return PE_HOLDEM_ROUND_ERR_BETTING;
    return PE_HOLDEM_ROUND_OK;
}

const char *pe_holdem_round_status_string(pe_holdem_round_status_t status)
{
    switch (status)
    {
    case PE_HOLDEM_ROUND_OK:
        return "ok";
    case PE_HOLDEM_ROUND_ERR_NULL_ARGUMENT:
        return "null argument";
    case PE_HOLDEM_ROUND_ERR_INVALID_STATE:
        return "invalid state";
    case PE_HOLDEM_ROUND_ERR_INVALID_BOARD:
        return "invalid board transition";
    case PE_HOLDEM_ROUND_ERR_BETTING:
        return "betting state error";
    default:
        return "unknown status";
    }
}
