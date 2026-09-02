/*
 * cfr_algo_ops.h - Regret and averaging operators (EXT-05, EXT-06)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The two numbers the traversal asks for, and the implementations that produce
 * them. Splitting them out is what lets cfr_traversal_full_scalar.c stay free
 * of algorithm selection: the recursion applies a discount and a weight, and
 * never learns which formula it came from.
 *
 * Internal to the engine. The public pe_regret_ops_t / pe_average_ops_t of
 * architecture v3 §4.4 operate on the dense-ID storage and the update batches,
 * neither of which exists yet (STO-01/02, PAR-01); declaring them here would
 * mean inventing their types twice. This is the same interface reduced to what
 * the current storage can actually express.
 */

#ifndef POKER_EVAL_CFR_ALGO_OPS_H
#define POKER_EVAL_CFR_ALGO_OPS_H

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How raw deltas turn into accumulated regret and into the average strategy.
 *
 * This is the seam that keeps algorithm selection out of the recursion.
 */
typedef struct cfr_algo_ops_t
{
    /*
     * Factor applied to the already-accumulated regret before this node's
     * delta is added: regret = regret * discount + delta. 1.0 accumulates
     * plainly.
     */
    double (*regret_discount)(const struct cfr_algo_ops_t *ops, int iter);

    /*
     * Weight of this node's contribution to the average strategy.
     *
     * `reach` is the acting player's reach probability and `flow_weight` the
     * flow-focusing factor the traversal already computed — passed in rather
     * than recomputed so the two cannot drift apart.
     */
    double (*average_weight)(const struct cfr_algo_ops_t *ops, int iter,
                             double reach, double flow_weight, int use_flow_focus);

    /* Read by the implementations, never by the traversal. */
    const cfr_config_t *config;
} cfr_algo_ops_t;

/* ------------------------------------------------------------------ *
 * Regret accumulation (EXT-05)
 * ------------------------------------------------------------------ */

/** Vanilla CFR: no discounting, the accumulation is a plain sum. */
double cfr_regret_discount_vanilla(const cfr_algo_ops_t *ops, int iter);

/**
 * The discount the v2 solver applied when enable_dcfr was set.
 *
 * Preserved verbatim, including its defect: the factor is applied once per
 * *visit* of an infoset rather than once per iteration, so a poker infoset
 * reached N times in one iteration sees d^N. EXT-07 is where that is fixed;
 * changing it here would have made EXT-05 impossible to verify.
 */
double cfr_regret_discount_legacy_dcfr(const cfr_algo_ops_t *ops, int iter);

/* ------------------------------------------------------------------ *
 * Averaging (EXT-06)
 * ------------------------------------------------------------------ */

/** Uniform: the contribution is the acting player's reach. */
double cfr_average_weight_uniform(const cfr_algo_ops_t *ops, int iter,
                                  double reach, double flow_weight,
                                  int use_flow_focus);

/** Linear: the reach weighted by the iteration index. */
double cfr_average_weight_linear(const cfr_algo_ops_t *ops, int iter,
                                 double reach, double flow_weight,
                                 int use_flow_focus);

/** The v2 weight, with its beta and gamma exponents. */
double cfr_average_weight_legacy_dcfr(const cfr_algo_ops_t *ops, int iter,
                                      double reach, double flow_weight,
                                      int use_flow_focus);

/* ------------------------------------------------------------------ *
 * Selection
 * ------------------------------------------------------------------ */

/**
 * The operators a legacy configuration resolves to.
 *
 * Picks the narrowest implementation that reproduces what the v2 booleans
 * asked for: no discounting at all rather than a discount that happens to be
 * 1.0, uniform averaging rather than a legacy weight with every exponent
 * disabled. The numbers are identical either way — the point is that a plan
 * printed later names the operator that actually runs.
 */
cfr_algo_ops_t cfr_algo_ops_from_config(const cfr_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_ALGO_OPS_H */
