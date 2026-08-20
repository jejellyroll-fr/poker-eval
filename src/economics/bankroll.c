#include <poker_eval/economics/bankroll.h>

#include <float.h>
#include <math.h>

static int pe_bankroll_finite(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

static int pe_bankroll_result_finite(long double value)
{
    return value >= -(long double)DBL_MAX && value <= (long double)DBL_MAX;
}

int pe_compute_risk_of_ruin(double bankroll,
                            double winrate_per_unit,
                            double stddev_per_unit,
                            double target_ror,
                            pe_bankroll_result_t *out_result)
{
    if (!out_result || !pe_bankroll_finite(bankroll) || bankroll <= 0.0 ||
        !pe_bankroll_finite(winrate_per_unit) || winrate_per_unit <= 0.0 ||
        !pe_bankroll_finite(stddev_per_unit) || stddev_per_unit <= 0.0 ||
        !pe_bankroll_finite(target_ror) || target_ror <= 0.0 ||
        target_ror >= 1.0)
        return -1;

    const long double mu = (long double)winrate_per_unit;
    const long double variance =
        (long double)stddev_per_unit * (long double)stddev_per_unit;
    const long double bankroll_ld = (long double)bankroll;
    const long double ror_exponent = -2.0L * mu * bankroll_ld / variance;
    const long double required =
        -variance * logl((long double)target_ror) / (2.0L * mu);
    const long double kelly = mu / variance;
    const long double growth = 0.5L * mu * mu / variance;

    if (!pe_bankroll_result_finite(required) ||
        !pe_bankroll_result_finite(kelly) ||
        !pe_bankroll_result_finite(growth))
        return -1;

    out_result->risk_of_ruin = (double)expl(ror_exponent);
    out_result->required_bankroll = (double)required;
    out_result->kelly_fraction = (double)kelly;
    out_result->half_kelly_fraction = (double)(0.5L * kelly);
    out_result->expected_growth_rate = (double)growth;
    return 0;
}

int pe_compute_staking_split(double winrate,
                             double stddev,
                             double makeup_cap,
                             double profit_split_investor,
                             double *out_investor_ev,
                             double *out_player_ev)
{
    if (!out_investor_ev || !out_player_ev || !pe_bankroll_finite(winrate) ||
        !pe_bankroll_finite(stddev) || stddev < 0.0 ||
        !pe_bankroll_finite(makeup_cap) || makeup_cap < 0.0 ||
        !pe_bankroll_finite(profit_split_investor) ||
        profit_split_investor < 0.0 || profit_split_investor > 1.0)
        return -1;

    const double recovery = fmin(fmax(winrate, 0.0), makeup_cap);
    const double distributable = fmax(winrate - makeup_cap, 0.0);
    *out_investor_ev = recovery + distributable * profit_split_investor;
    *out_player_ev = distributable * (1.0 - profit_split_investor);

    /* A losing period is charged to the investor and does not create player
     * profit or reduce a pre-existing makeup balance. */
    if (winrate < 0.0)
        *out_investor_ev = winrate;
    return 0;
}
