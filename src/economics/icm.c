#include <poker_eval/economics/icm.h>
#include <string.h>
#include <math.h>

#include "../solver/domain/finite_double.h"

static void asymmetric_recurse(int depth,
                               const icm_asymmetric_input_t *input,
                               int *active,
                               double total_chips,
                               double probability,
                               icm_asymmetric_result_t *result)
{
    if (depth >= input->num_payouts || depth >= input->num_players ||
        total_chips <= 0.0)
        return;
    for (int player = 0; player < input->num_players; ++player) {
        if (!active[player]) continue;
        double branch = probability * input->stacks[player] / total_chips;
        result->finish_probability[player][depth] += branch;
        result->ev[player] += branch * input->payouts[player][depth];
        active[player] = 0;
        asymmetric_recurse(depth + 1, input, active,
                           total_chips - input->stacks[player], branch,
                           result);
        active[player] = 1;
    }
}

int pe_icm_calculate_asymmetric(const icm_asymmetric_input_t *input,
                                icm_asymmetric_result_t *result)
{
    int active[ICM_MAX_PLAYERS];
    double total = 0.0;
    if (!input || !result || input->num_players < 1 ||
        input->num_players > ICM_MAX_PLAYERS || input->num_payouts < 1 ||
        input->num_payouts > input->num_players)
        return -1;
    memset(result, 0, sizeof(*result));
    for (int player = 0; player < input->num_players; ++player) {
        if (!(input->stacks[player] > 0.0) || !pe_finite_double(input->stacks[player]))
            return -1;
        total += input->stacks[player];
        if (!pe_finite_double(total))
            return -1;
        active[player] = 1;
        for (int position = 0; position < input->num_payouts; ++position)
            if (!pe_finite_double(input->payouts[player][position])) return -1;
    }
    if (!(total > 0.0)) return -1;
    asymmetric_recurse(0, input, active, total, 1.0, result);
    return 0;
}

/*
 * Warning: This recursive implementation has factorial complexity O(P(N, K))
 * where N is players and K is payouts. It handles typical final tables (N<=9)
 * efficiently, but may become slow for large fields with deep payouts (e.g. 12+).
 * Max depth is limited by num_payouts.
 */
static void recursive_icm(int depth,
                          int num_players,
                          double current_prob,
                          const double *stacks,
                          const double *payouts,
                          int num_payouts,
                          int *active,
                          double total_chips,
                          double *ev_accum)
{
    /* Safety check for recursion depth */
    if (depth >= num_payouts || depth > 20) return;

    for (int i = 0; i < num_players; ++i) {
        if (active[i]) {
            double prob_win = stacks[i] / total_chips;
            double conditional_prob = current_prob * prob_win;

            /* Add EV for this finish position */
            ev_accum[i] += conditional_prob * payouts[depth];

            /* Recurse for next position */
            active[i] = 0;
            if (total_chips > stacks[i]) {
                recursive_icm(depth + 1, num_players, conditional_prob,
                              stacks, payouts, num_payouts, active,
                              total_chips - stacks[i], ev_accum);
            }
            active[i] = 1;
        }
    }
}

int pe_icm_calculate(const icm_input_t *input, icm_result_t *result)
{
    if (!input || !result) return -1;
    if (input->num_players <= 0 || input->num_players > ICM_MAX_PLAYERS ||
        input->num_payouts <= 0 || input->num_payouts > input->num_players)
        return -1;

    double total_chips = 0.0;
    int active[ICM_MAX_PLAYERS];
    for (int i = 0; i < input->num_players; ++i) {
        if (!pe_finite_double(input->stacks[i]) ||
            !(input->stacks[i] > 0.0))
            return -1;
        total_chips += input->stacks[i];
        if (!pe_finite_double(total_chips))
            return -1;
        active[i] = 1;
        result->icm_ev[i] = 0.0;
    }

    for (int i = 0; i < input->num_payouts; ++i)
        if (!pe_finite_double(input->payouts[i]) || input->payouts[i] < 0.0)
            return -1;

    if (total_chips <= 0.0) return -1;

    recursive_icm(0, input->num_players, 1.0, input->stacks, input->payouts, input->num_payouts, active, total_chips, result->icm_ev);

    /* Calculate equity shares */
    double total_payout = 0.0;
    for (int i = 0; i < input->num_payouts; ++i) {
        total_payout += input->payouts[i];
        if (!pe_finite_double(total_payout))
            return -1;
    }

    for (int i = 0; i < input->num_players; ++i) {
        if (total_payout > 0.0)
            result->equity[i] = result->icm_ev[i] / total_payout;
        else
            result->equity[i] = 0.0;
    }

    return 0;
}

int pe_icm_chop(const icm_input_t *input, double *out_payouts)
{
    if (!input || !out_payouts) return -1;
    icm_result_t result;
    if (pe_icm_calculate(input, &result) != 0) return -1;
    for (int i = 0; i < input->num_players; ++i) {
        out_payouts[i] = result.icm_ev[i];
    }
    return 0;
}
