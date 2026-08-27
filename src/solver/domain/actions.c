#include <poker_eval/solver/pe_actions.h>

#include <float.h>

static int finite_nonnegative(double value)
{
    /* Avoid the MinGW isfinite macro selecting its float overload under
     * -Wconversion; NaN fails both ordered comparisons. */
    return value >= 0.0 && value <= DBL_MAX;
}

pe_action_t pe_action_invalid(void)
{
    pe_action_t action;
    action.kind = PE_ACTION_INVALID;
    action.amount_kind = PE_AMOUNT_NONE;
    action.amount = 0.0;
    action.size_index = -1;
    return action;
}

pe_action_status_t pe_action_validate(const pe_action_t *action)
{
    if (!action)
        return PE_ACTION_ERR_NULL_ARGUMENT;
    if (action->kind <= PE_ACTION_INVALID || action->kind > PE_ACTION_TERMINAL)
        return PE_ACTION_ERR_INVALID;
    if (action->size_index < -1)
        return PE_ACTION_ERR_INVALID;
    if (!finite_nonnegative(action->amount))
        return PE_ACTION_ERR_INVALID;

    if (action->kind == PE_ACTION_FOLD || action->kind == PE_ACTION_CHECK ||
        action->kind == PE_ACTION_CALL || action->kind == PE_ACTION_ALL_IN ||
        action->kind == PE_ACTION_CHANCE || action->kind == PE_ACTION_TERMINAL)
    {
        if (action->amount_kind != PE_AMOUNT_NONE ||
            action->amount > 0.0 || action->amount < 0.0)
            return PE_ACTION_ERR_AMBIGUOUS;
        return PE_ACTION_OK;
    }

    if (action->kind != PE_ACTION_BET && action->kind != PE_ACTION_RAISE)
        return PE_ACTION_ERR_INVALID;
    if (action->amount_kind == PE_AMOUNT_NONE)
        return PE_ACTION_ERR_AMBIGUOUS;
    if (action->amount_kind == PE_AMOUNT_POT_FRACTION && action->amount > 0.0 &&
        action->amount > 1000.0)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    return PE_ACTION_OK;
}

pe_action_status_t pe_action_from_legacy_code(
    int legacy_code,
    const double *raise_sizes,
    size_t raise_count,
    int pot_sizing,
    int all_in_code,
    pe_action_t *out)
{
    pe_action_t action;
    int raise_index;

    if (!out)
        return PE_ACTION_ERR_NULL_ARGUMENT;
    *out = pe_action_invalid();
    if (legacy_code < 0 || (all_in_code >= 0 && legacy_code == all_in_code))
    {
        if (all_in_code >= 0 && legacy_code == all_in_code)
        {
            out->kind = PE_ACTION_ALL_IN;
            return PE_ACTION_OK;
        }
        return PE_ACTION_ERR_OUT_OF_RANGE;
    }
    if (legacy_code == 0)
    {
        out->kind = PE_ACTION_FOLD;
        return PE_ACTION_OK;
    }
    if (legacy_code == 1)
    {
        out->kind = PE_ACTION_CALL;
        return PE_ACTION_OK;
    }

    raise_index = legacy_code - 2;
    if (raise_index < 0 || (size_t)raise_index >= raise_count || !raise_sizes)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    if (!finite_nonnegative(raise_sizes[raise_index]))
        return PE_ACTION_ERR_INVALID;

    action.kind = PE_ACTION_RAISE;
    action.amount_kind = pot_sizing ? PE_AMOUNT_POT_FRACTION : PE_AMOUNT_CHIPS;
    action.amount = raise_sizes[raise_index];
    action.size_index = raise_index;
    *out = action;
    return PE_ACTION_OK;
}

pe_action_status_t pe_action_to_legacy_code(
    const pe_action_t *action,
    size_t raise_count,
    int all_in_code,
    int *out_legacy_code)
{
    pe_action_status_t status;

    if (!out_legacy_code)
        return PE_ACTION_ERR_NULL_ARGUMENT;
    status = pe_action_validate(action);
    if (status != PE_ACTION_OK)
        return status;
    if (action->kind == PE_ACTION_FOLD)
    {
        *out_legacy_code = 0;
        return PE_ACTION_OK;
    }
    if (action->kind == PE_ACTION_CALL || action->kind == PE_ACTION_CHECK)
    {
        *out_legacy_code = 1;
        return PE_ACTION_OK;
    }
    if (action->kind == PE_ACTION_ALL_IN)
    {
        if (all_in_code < 0)
            return PE_ACTION_ERR_OUT_OF_RANGE;
        *out_legacy_code = all_in_code;
        return PE_ACTION_OK;
    }
    if (action->kind != PE_ACTION_RAISE || action->size_index < 0 ||
        (size_t)action->size_index >= raise_count)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    *out_legacy_code = action->size_index + 2;
    return PE_ACTION_OK;
}

pe_action_status_t pe_action_commitment(
    const pe_action_t *action,
    double to_call,
    double stack,
    double pot,
    double min_raise,
    int pot_limit,
    double *out_commitment)
{
    double increment;
    double commitment;
    pe_action_status_t status;

    if (!out_commitment)
        return PE_ACTION_ERR_NULL_ARGUMENT;
    *out_commitment = 0.0;
    status = pe_action_validate(action);
    if (status != PE_ACTION_OK)
        return status;
    if (!finite_nonnegative(to_call) || !finite_nonnegative(stack) ||
        !finite_nonnegative(pot) || !finite_nonnegative(min_raise))
        return PE_ACTION_ERR_INVALID;
    if (to_call > stack + 1e-9)
        return PE_ACTION_ERR_INVALID;

    if (action->kind == PE_ACTION_FOLD || action->kind == PE_ACTION_CHECK)
    {
        if (action->kind == PE_ACTION_CHECK && to_call > 1e-9)
            return PE_ACTION_ERR_INVALID;
        return PE_ACTION_OK;
    }
    if (action->kind == PE_ACTION_CALL)
    {
        *out_commitment = to_call;
        return PE_ACTION_OK;
    }
    if (action->kind == PE_ACTION_ALL_IN)
    {
        *out_commitment = stack;
        return PE_ACTION_OK;
    }
    if (action->kind != PE_ACTION_BET && action->kind != PE_ACTION_RAISE)
        return PE_ACTION_ERR_INVALID;

    if (action->amount_kind == PE_AMOUNT_CHIPS)
        increment = action->amount;
    else if (action->amount_kind == PE_AMOUNT_POT_FRACTION)
        increment = action->amount * pot;
    else if (action->amount_kind == PE_AMOUNT_MINIMUM)
        increment = min_raise;
    else if (action->amount_kind == PE_AMOUNT_MAXIMUM)
        increment = stack - to_call;
    else
        return PE_ACTION_ERR_AMBIGUOUS;

    if (!finite_nonnegative(increment) || increment + 1e-9 < min_raise)
        return PE_ACTION_ERR_INVALID;
    if (pot_limit && increment > pot + to_call + 1e-9)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    commitment = to_call + increment;
    if (commitment > stack + 1e-9)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    *out_commitment = commitment;
    return PE_ACTION_OK;
}

const char *pe_action_kind_string(pe_action_kind_t kind)
{
    static const char *const names[] = {
        "invalid", "fold", "check", "call", "bet", "raise", "all-in",
        "chance", "terminal"};
    if (kind < PE_ACTION_INVALID || kind > PE_ACTION_TERMINAL)
        return "invalid";
    return names[kind];
}

const char *pe_action_status_string(pe_action_status_t status)
{
    switch (status)
    {
    case PE_ACTION_OK:
        return "ok";
    case PE_ACTION_ERR_NULL_ARGUMENT:
        return "null argument";
    case PE_ACTION_ERR_INVALID:
        return "invalid action";
    case PE_ACTION_ERR_OUT_OF_RANGE:
        return "out of range";
    case PE_ACTION_ERR_AMBIGUOUS:
        return "ambiguous action";
    default:
        return "unknown action status";
    }
}
