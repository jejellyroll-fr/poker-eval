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

/** Canonical Brown–Sandholm parameters: alpha=1.5, beta=0, gamma=2. */
pe_dcfr_params_t pe_dcfr_params_default(void);

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
