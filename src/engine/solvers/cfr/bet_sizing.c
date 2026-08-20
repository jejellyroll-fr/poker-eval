#include <poker_eval/engine/solvers/cfr/bet_sizing.h>

#include <math.h>
#include <stddef.h>

static int pe_bet_size_is_tie(double value, double best)
{
    const double scale = fmax(1.0, fmax(fabs(value), fabs(best)));
    return fabs(value - best) <= 1e-9 * scale;
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

        out_result->sizes[i].bet_size_fraction = fraction;
        out_result->sizes[i].bet_amount = amount;
        out_result->sizes[i].expected_value = ev;
        out_result->sizes[i].frequency = 0.0;

        if (out_result->optimal_index < 0 || ev > out_result->max_ev)
        {
            out_result->optimal_index = i;
            out_result->max_ev = ev;
        }
    }

    out_result->num_sizes = num_candidates;

    int tied = 0;
    for (int i = 0; i < num_candidates; ++i)
        if (pe_bet_size_is_tie(out_result->sizes[i].expected_value,
                               out_result->max_ev))
            ++tied;

    if (tied == 0)
        return -1;

    const double frequency = 1.0 / (double)tied;
    for (int i = 0; i < num_candidates; ++i)
        if (pe_bet_size_is_tie(out_result->sizes[i].expected_value,
                               out_result->max_ev))
            out_result->sizes[i].frequency = frequency;

    return 0;
}
