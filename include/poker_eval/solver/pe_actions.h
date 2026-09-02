/*
 * pe_actions.h - semantic action contract for game and format adapters
 *
 * Action numbers are local implementation details. This surface is the
 * boundary used by the generic rules kernel: adapters translate their own
 * action codes into these values once, and the rules code never interprets a
 * serialized integer directly.
 */

#ifndef POKER_EVAL_PE_ACTIONS_H
#define POKER_EVAL_PE_ACTIONS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PE_ACTION_INVALID = 0,
    PE_ACTION_FOLD,
    PE_ACTION_CHECK,
    PE_ACTION_CALL,
    PE_ACTION_BET,
    PE_ACTION_RAISE,
    PE_ACTION_ALL_IN,
    PE_ACTION_CHANCE,
    PE_ACTION_TERMINAL
} pe_action_kind_t;

typedef enum
{
    PE_AMOUNT_NONE = 0,
    PE_AMOUNT_CHIPS,
    PE_AMOUNT_POT_FRACTION,
    PE_AMOUNT_MINIMUM,
    PE_AMOUNT_MAXIMUM,
    PE_AMOUNT_AUTO
} pe_amount_kind_t;

typedef enum
{
    PE_ACTION_OK = 0,
    PE_ACTION_ERR_NULL_ARGUMENT,
    PE_ACTION_ERR_INVALID,
    PE_ACTION_ERR_OUT_OF_RANGE,
    PE_ACTION_ERR_AMBIGUOUS
} pe_action_status_t;

typedef struct
{
    pe_action_kind_t kind;
    pe_amount_kind_t amount_kind;
    double amount;
    int size_index;
} pe_action_t;

/* Return a zero-initialized action with an explicit invalid kind. */
pe_action_t pe_action_invalid(void);

/* Validate the action's shape without consulting a game state. */
pe_action_status_t pe_action_validate(const pe_action_t *action);

/*
 * Translate the legacy MPF action numbering used by the current adapter.
 *
 * `raise_sizes` are amounts above the call, matching the existing MPF
 * transition contract. If `pot_sizing` is non-zero they are fractions of the
 * current pot instead. `all_in_code` is supplied by the adapter because it is
 * a format-local reserved code, not a generic poker action number.
 */
pe_action_status_t pe_action_from_legacy_code(
    int legacy_code,
    const double *raise_sizes,
    size_t raise_count,
    int pot_sizing,
    int all_in_code,
    pe_action_t *out);

/* Translate a semantic action back to the legacy numbering during migration. */
pe_action_status_t pe_action_to_legacy_code(
    const pe_action_t *action,
    size_t raise_count,
    int all_in_code,
    int *out_legacy_code);

/*
 * Resolve the amount committed by an action for one state.
 *
 * The returned value is the total amount paid from the stack by this action,
 * including the outstanding call. `min_raise` is the increment above the
 * current call required for a legal full raise. A pot-fraction action uses the
 * supplied pot as its base. This function does not mutate a state and rejects
 * an amount that cannot be represented as a legal action.
 */
pe_action_status_t pe_action_commitment(
    const pe_action_t *action,
    double to_call,
    double stack,
    double pot,
    double min_raise,
    int pot_limit,
    double *out_commitment);

const char *pe_action_kind_string(pe_action_kind_t kind);
const char *pe_action_status_string(pe_action_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_ACTIONS_H */
