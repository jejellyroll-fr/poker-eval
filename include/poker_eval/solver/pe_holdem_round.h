/* pe_holdem_round.h - public-card streets plus one-street betting actions */

#ifndef POKER_EVAL_PE_HOLDEM_ROUND_H
#define POKER_EVAL_PE_HOLDEM_ROUND_H

#include <stdint.h>

#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_holdem_streets.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    mask_t board;
    mask_t dead_cards; /* private cards and other known dead cards */
    pe_holdem_street_t street;
    pe_betting_state_t betting;
} pe_holdem_round_state_t;

typedef enum
{
    PE_HOLDEM_ROUND_OK = 0,
    PE_HOLDEM_ROUND_ERR_NULL_ARGUMENT,
    PE_HOLDEM_ROUND_ERR_INVALID_STATE,
    PE_HOLDEM_ROUND_ERR_INVALID_BOARD,
    PE_HOLDEM_ROUND_ERR_BETTING
} pe_holdem_round_status_t;

/* Initialize a preflop, flop, or turn betting state around a public board. */
pe_holdem_round_status_t pe_holdem_round_init(
    pe_holdem_round_state_t *out,
    mask_t board,
    mask_t dead_cards,
    const pe_betting_rules_t *rules,
    const double *stacks,
    uint8_t player_count,
    int first_to_act,
    double pot,
    double to_call);

/* Apply one semantic action while preserving the public street context. */
pe_holdem_round_status_t pe_holdem_round_apply_action(
    const pe_holdem_round_state_t *state,
    const pe_betting_rules_t *rules,
    const pe_action_t *action,
    pe_holdem_round_state_t *out);

/*
 * Commit a selected chance outcome and open the next betting street. The
 * caller obtains valid next boards with pe_holdem_public_chance_enumerate().
 */
pe_holdem_round_status_t pe_holdem_round_advance(
    const pe_holdem_round_state_t *state,
    const pe_betting_rules_t *rules,
    mask_t next_board,
    int first_to_act,
    pe_holdem_round_state_t *out);

/* Close a completed river betting round and mark the state showdown-ready. */
pe_holdem_round_status_t pe_holdem_round_showdown(
    const pe_holdem_round_state_t *state,
    const pe_betting_rules_t *rules,
    pe_holdem_round_state_t *out);

const char *pe_holdem_round_status_string(pe_holdem_round_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_HOLDEM_ROUND_H */
