/*
 * regret_dcfr.c - Canonical DCFR weighting primitives (ALG-03)
 */

#include <poker_eval/solver/pe_regret_dcfr.h>

#include <math.h>

static int valid_exponent(double exponent)
{
    return isfinite(exponent) && exponent >= 0.0;
}

static double discount_factor(uint64_t iteration, double exponent)
{
    double t = (double)iteration;
    double powered = pow(t, exponent);
    return powered / (powered + 1.0);
}

pe_dcfr_params_t pe_dcfr_params_default(void)
{
    pe_dcfr_params_t params;
    params.alpha = 1.5;
    params.beta = 0.0;
    params.gamma = 2.0;
    return params;
}

int pe_dcfr_discount_regrets(double *regrets, size_t count,
                             uint64_t iteration,
                             const pe_dcfr_params_t *params)
{
    double positive_factor;
    double negative_factor;
    size_t i;

    if (!regrets || count == 0u || !params || iteration == 0u ||
        !valid_exponent(params->alpha) || !valid_exponent(params->beta) ||
        !valid_exponent(params->gamma))
        return -1;

    positive_factor = discount_factor(iteration, params->alpha);
    negative_factor = discount_factor(iteration, params->beta);
    if (!isfinite(positive_factor) || !isfinite(negative_factor))
        return -1;

    for (i = 0; i < count; ++i)
        if (!isfinite(regrets[i]))
            return -1;

    for (i = 0; i < count; ++i)
    {
        double factor = regrets[i] >= 0.0 ? positive_factor : negative_factor;
        regrets[i] *= factor;
        if (!isfinite(regrets[i]))
            return -1;
    }
    return 0;
}

int pe_dcfr_average_weight(uint64_t iteration, double gamma,
                           double *out_weight)
{
    double t;
    double ratio;

    if (!out_weight || iteration == 0u || !valid_exponent(gamma))
        return -1;
    t = (double)iteration;
    ratio = t / (t + 1.0);
    *out_weight = pow(ratio, gamma);
    return isfinite(*out_weight) ? 0 : -1;
}
