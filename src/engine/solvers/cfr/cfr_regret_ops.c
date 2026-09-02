/*
 * cfr_regret_ops.c - Regret accumulation operators (EXT-05)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * How an accumulated regret is discounted before a new delta lands on it. Two
 * implementations today: none, and the one the v2 solver applied.
 *
 * The legacy one is reproduced verbatim, defect included — see the header. A
 * refactor that also fixes a formula is a refactor nobody can verify.
 */

#include "cfr_algo_ops.h"

#include <math.h>

double cfr_regret_discount_vanilla(const cfr_algo_ops_t *ops, int iter)
{
    (void)ops;
    (void)iter;
    return 1.0;
}

double cfr_regret_discount_legacy_dcfr(const cfr_algo_ops_t *ops, int iter)
{
    const cfr_config_t *config = ops->config;
    double t = (double)(iter + 1);
    double a;

    if (!config->enable_dcfr)
        return 1.0;

    a = pow(t, config->dcfr_alpha);
    return (t > 0.0) ? (a / (a + 1.0)) : 1.0;
}
