#include <poker_eval/engine/solvers/cfr/bet_sizing.h>

#include <math.h>
#include <stddef.h>

static int pe_bet_size_is_tie(double value, double best)
{
    const double scale = fmax(1.0, fmax(fabs(value), fabs(best)));
    return fabs(value - best) <= 1e-9 * scale;
}

static int pe_bet_size_same_amount(double left, double right)
{
    const double scale = fmax(1.0, fmax(fabs(left), fabs(right)));
    return fabs(left - right) <= 1e-12 * scale;
}

int pe_optimal_bet_size(double pot,
                        double effective_stack,
                        const double *candidate_fractions,
                        int num_candidates,
                        pe_bet_size_ev_fn value_fn,
                        void *ctx,
                        pe_bet_sizing_result_t *out_result)
{
    if (!out_result || !candidate_fractions || !value_fn ||
        !isfinite(pot) || pot <= 0.0 || !isfinite(effective_stack) ||
        effective_stack <= 0.0 || num_candidates <= 0 ||
        num_candidates > PE_BET_SIZING_MAX_OPTIONS)
        return -1;

    out_result->num_sizes = 0;
    out_result->optimal_index = -1;
    out_result->max_ev = -INFINITY;

    for (int i = 0; i < num_candidates; ++i)
    {
        const double fraction = candidate_fractions[i];
        if (!isfinite(fraction) || fraction <= 0.0)
            return -1;

        const double amount = fmin(pot * fraction, effective_stack);
        const double ev = value_fn(fraction, amount, ctx);
        if (!isfinite(ev))
            return -1;

        int duplicate = -1;
        for (int j = 0; j < out_result->num_sizes; ++j)
            if (pe_bet_size_same_amount(out_result->sizes[j].bet_amount,
                                         amount))
            {
                duplicate = j;
                break;
            }

        if (duplicate >= 0)
        {
            /* Several fractions can collapse to one all-in action after
             * stack capping. Keep one canonical action; if the callback
             * distinguishes the nominal fractions, retain its best EV. */
            if (ev > out_result->sizes[duplicate].expected_value)
            {
                out_result->sizes[duplicate].bet_size_fraction = fraction;
                out_result->sizes[duplicate].expected_value = ev;
            }
            continue;
        }

        const int index = out_result->num_sizes++;
        out_result->sizes[index].bet_size_fraction = fraction;
        out_result->sizes[index].bet_amount = amount;
        out_result->sizes[index].expected_value = ev;
        out_result->sizes[index].frequency = 0.0;
    }

    int tied = 0;
    for (int i = 0; i < out_result->num_sizes; ++i)
        if (out_result->optimal_index < 0 ||
            out_result->sizes[i].expected_value > out_result->max_ev)
        {
            out_result->optimal_index = i;
            out_result->max_ev = out_result->sizes[i].expected_value;
        }

    for (int i = 0; i < out_result->num_sizes; ++i)
        if (pe_bet_size_is_tie(out_result->sizes[i].expected_value,
                               out_result->max_ev))
            ++tied;

    if (tied == 0)
        return -1;

    const double frequency = 1.0 / (double)tied;
    for (int i = 0; i < out_result->num_sizes; ++i)
        if (pe_bet_size_is_tie(out_result->sizes[i].expected_value,
                               out_result->max_ev))
            out_result->sizes[i].frequency = frequency;

    return 0;
}
