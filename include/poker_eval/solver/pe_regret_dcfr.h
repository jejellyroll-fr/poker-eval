/*
 * pe_regret_dcfr.h - Canonical DCFR weighting primitives (ALG-03)
 */

#ifndef POKER_EVAL_PE_REGRET_DCFR_H
#define POKER_EVAL_PE_REGRET_DCFR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double alpha;
    double beta;
    double gamma;
} pe_dcfr_params_t;

/**
 * How DCFR's exponents change across iterations (issue #284).
 *
 * FIXED keeps the caller's alpha/beta/gamma for the whole solve, which is the
 * canonical Brown-Sandholm DCFR control. HS15 and HS30 are the training-free
 * Hyperparameter Schedules of Zhang et al., "Faster Game Solving via
 * Hyperparameter Schedules" (AAAI 2026), Equation 4, with an initial gamma of
 * 15 and 30 respectively:
 *
 *   alpha(t) = 1 + 3 * min(t, n) / n
 *   beta(t)  = -1 - 2 * min(t, n) / n
 *   gamma(t) = G - 5 * min(t, n) / n      with G = 15 or 30
 *
 * where t is the iteration and n the schedule horizon. Past the horizon the
 * parameters stay at their final values instead of extrapolating. The paper's
 * experiments use n = 1000; HS30 is its single-schedule recommendation.
 */
typedef enum {
    PE_DCFR_SCHEDULE_FIXED = 0,
    PE_DCFR_SCHEDULE_HS15,
    PE_DCFR_SCHEDULE_HS30,
    PE_DCFR_SCHEDULE_COUNT
} pe_dcfr_schedule_t;

/** Canonical Brown–Sandholm parameters: alpha=1.5, beta=0, gamma=2. */
pe_dcfr_params_t pe_dcfr_params_default(void);

/**
 * Resolve the exponents for one iteration.
 *
 * FIXED copies `fixed` unchanged. HS schedules require a positive horizon and
 * a non-zero iteration; `fixed` may be NULL when the schedule is not FIXED.
 *
 * @return 0 on success, -1 for invalid arguments or schedules.
 */
int pe_dcfr_params_resolve(const pe_dcfr_params_t *fixed,
                           pe_dcfr_schedule_t schedule, uint64_t horizon,
                           uint64_t iteration, pe_dcfr_params_t *out);

/** Stable lowercase name ("fixed", "hs15", "hs30"); never NULL. */
const char *pe_dcfr_schedule_name(pe_dcfr_schedule_t schedule);

/** Parse a schedule name. @return 0 on success, -1 when unknown. */
int pe_dcfr_schedule_parse(const char *text, pe_dcfr_schedule_t *out);

/**
 * Discount one cumulative regret span at iteration `iteration`.
 *
 * Positive regrets receive t^alpha/(t^alpha+1); negative regrets receive
 * t^beta/(t^beta+1). The operation is applied once to the whole span.
 *
 * @return 0 on success, -1 for invalid arguments, parameters, iteration or
 *         non-finite data.
 */
int pe_dcfr_discount_regrets(double *regrets, size_t count,
                             uint64_t iteration,
                             const pe_dcfr_params_t *params);

/** Return the canonical DCFR averaging weight (t/(t+1))^gamma. */
int pe_dcfr_average_weight(uint64_t iteration, double gamma,
                           double *out_weight);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_REGRET_DCFR_H */
