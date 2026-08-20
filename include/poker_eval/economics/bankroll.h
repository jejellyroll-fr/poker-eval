#ifndef POKER_EVAL_BANKROLL_H
#define POKER_EVAL_BANKROLL_H

#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    double risk_of_ruin;
    double required_bankroll;
    double kelly_fraction;
    double half_kelly_fraction;
    double expected_growth_rate;
} pe_bankroll_result_t;

/* Compute the infinite-horizon Brownian risk-of-ruin model and Kelly values.
 * winrate and stddev must use the same unit (for example BB/100 hands). */
extern POKEREVAL_EXPORT int pe_compute_risk_of_ruin(
    double bankroll,
    double winrate_per_unit,
    double stddev_per_unit,
    double target_ror,
    pe_bankroll_result_t *out_result);

/* Settle one expected backing period. makeup_cap is the outstanding makeup
 * recovered before the configured profit split is applied. Negative expected
 * profit is borne by the investor; standard deviation is validated for API
 * consistency but does not change an expected-value-only settlement. */
extern POKEREVAL_EXPORT int pe_compute_staking_split(
    double winrate,
    double stddev,
    double makeup_cap,
    double profit_split_investor,
    double *out_investor_ev,
    double *out_player_ev);

#ifdef __cplusplus
}
#endif

#endif
