/*
 * pe_betting_state.h - generic one-street betting state machine
 *
 * The state machine owns betting semantics only. Streets, cards, pots at
 * showdown and strategies are deliberately outside this surface.
 */

#ifndef POKER_EVAL_PE_BETTING_STATE_H
#define POKER_EVAL_PE_BETTING_STATE_H

#include <stdint.h>

#include <poker_eval/solver/pe_actions.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_BETTING_MAX_PLAYERS 8u

typedef enum
{
    PE_BETTING_OK = 0,
    PE_BETTING_ERR_NULL_ARGUMENT,
    PE_BETTING_ERR_INVALID_STATE,
    PE_BETTING_ERR_ILLEGAL_ACTION,
    PE_BETTING_ERR_OUT_OF_RANGE,
    PE_BETTING_ERR_TERMINAL,
    PE_BETTING_ERR_ROUND_COMPLETE
} pe_betting_status_t;

typedef struct
{
    uint8_t player_count;
    double epsilon;
    double min_raise;
    int pot_limit;
    int raise_cap; /* 0 means unlimited. */
} pe_betting_rules_t;

typedef struct
{
    uint8_t player_count;
    int8_t to_act;
    int active[PE_BETTING_MAX_PLAYERS];
    int all_in[PE_BETTING_MAX_PLAYERS];
    int acted[PE_BETTING_MAX_PLAYERS];
    double stack[PE_BETTING_MAX_PLAYERS];
    double round_contrib[PE_BETTING_MAX_PLAYERS];
    double invested[PE_BETTING_MAX_PLAYERS];
    double pot;
    double to_call;
    double current_bet;
    double min_raise;
    uint16_t raises_made;
    int round_complete;
    int terminal;
    int winner;
} pe_betting_state_t;

void pe_betting_rules_default(pe_betting_rules_t *out, uint8_t player_count);

pe_betting_status_t pe_betting_state_init(
    pe_betting_state_t *out,
    const pe_betting_rules_t *rules,
    const double *stacks,
    uint8_t player_count,
    int first_to_act,
    double pot,
    double to_call);

pe_betting_status_t pe_betting_state_validate(
    const pe_betting_state_t *state,
    const pe_betting_rules_t *rules);

/* Check a semantic action against the current state without mutating it. */
pe_betting_status_t pe_betting_action_is_legal(
    const pe_betting_state_t *state,
    const pe_betting_rules_t *rules,
    const pe_action_t *action);

/* Apply one action into a separate destination state. */
pe_betting_status_t pe_betting_apply_action(
    const pe_betting_state_t *state,
    const pe_betting_rules_t *rules,
    const pe_action_t *action,
    pe_betting_state_t *out);

const char *pe_betting_status_string(pe_betting_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BETTING_STATE_H */
