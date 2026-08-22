/*
 * cfr_average_ops.c - Averaging operators (EXT-06)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The weight a node's current strategy carries into the average. Three
 * implementations, and the selector that maps a v2 configuration onto them.
 *
 * All three start from the acting player's reach and apply flow focusing the
 * same way, because that part was never algorithm-dependent: it is the
 * traversal's own weighting, and duplicating its condition in each operator is
 * what keeps the three interchangeable.
 */

#include "cfr_algo_ops.h"

#include <math.h>

static double base_weight(double reach, double flow_weight, int use_flow_focus)
{
    return use_flow_focus ? (reach * flow_weight) : reach;
}

double cfr_average_weight_uniform(const cfr_algo_ops_t *ops, int iter,
                                  double reach, double flow_weight,
                                  int use_flow_focus)
{
    (void)ops;
    (void)iter;
    return base_weight(reach, flow_weight, use_flow_focus);
}

double cfr_average_weight_linear(const cfr_algo_ops_t *ops, int iter,
                                 double reach, double flow_weight,
                                 int use_flow_focus)
{
    (void)ops;
    return base_weight(reach, flow_weight, use_flow_focus) * (double)(iter + 1);
}

double cfr_average_weight_legacy_dcfr(const cfr_algo_ops_t *ops, int iter,
                                      double reach, double flow_weight,
                                      int use_flow_focus)
{
    const cfr_config_t *config = ops->config;
    double t = (double)(iter + 1);
    double w = base_weight(reach, flow_weight, use_flow_focus);

    if (config->enable_linear_avg)
        w *= t;
    if (config->enable_dcfr)
    {
        /* beta and gamma are both applied to the average here. That is not
           canonical DCFR — beta discounts negative regrets there — but it is
           what v2 did, and ALG-03 is where the semantics are corrected. */
        if (fabs(config->dcfr_beta) > 1e-9)
            w *= pow(t, config->dcfr_beta);
        if (fabs(config->dcfr_gamma) > 1e-9)
            w *= pow(t, config->dcfr_gamma);
    }
    return w;
}

cfr_algo_ops_t cfr_algo_ops_from_config(const cfr_config_t *config)
{
    cfr_algo_ops_t ops;

    ops.config = config;

    ops.regret_discount = config->enable_dcfr ? cfr_regret_discount_legacy_dcfr
                                              : cfr_regret_discount_vanilla;

    if (config->enable_dcfr)
        ops.average_weight = cfr_average_weight_legacy_dcfr;
    else if (config->enable_linear_avg)
        ops.average_weight = cfr_average_weight_linear;
    else
        ops.average_weight = cfr_average_weight_uniform;

    return ops;
}
