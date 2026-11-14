/*
 * regret_dcfr.c - Canonical DCFR weighting primitives (ALG-03)
 */

#include <poker_eval/solver/pe_regret_dcfr.h>

#include "finite_double.h"

#include <math.h>
#include <string.h>

/* DCFR's beta may be negative under the HS schedules, where early negative
   regrets are discounted more aggressively; only the averaging exponent and
   gamma itself must stay non-negative. */
static int valid_regret_exponent(double exponent)
{
    return pe_finite_double(exponent);
}

static int valid_exponent(double exponent)
{
    return pe_finite_double(exponent) && exponent >= 0.0;
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

int pe_dcfr_params_resolve(const pe_dcfr_params_t *fixed,
                           pe_dcfr_schedule_t schedule, uint64_t horizon,
                           uint64_t iteration, pe_dcfr_params_t *out)
{
    double progress;
    double initial_gamma;

    if (!out || schedule < PE_DCFR_SCHEDULE_FIXED ||
        schedule >= PE_DCFR_SCHEDULE_COUNT || iteration == 0u)
        return -1;
    if (schedule == PE_DCFR_SCHEDULE_FIXED)
    {
        if (!fixed || !valid_exponent(fixed->alpha) ||
            !valid_regret_exponent(fixed->beta) ||
            !valid_exponent(fixed->gamma))
            return -1;
        *out = *fixed;
        return 0;
    }
    if (horizon == 0u)
        return -1;

    /* The schedules are defined over t in [1, n]; clamping keeps the final
       parameters instead of extrapolating past the horizon. */
    progress = iteration < horizon ? (double)iteration / (double)horizon : 1.0;
    initial_gamma = schedule == PE_DCFR_SCHEDULE_HS15 ? 15.0 : 30.0;
    out->alpha = 1.0 + 3.0 * progress;
    out->beta = -1.0 - 2.0 * progress;
    out->gamma = initial_gamma - 5.0 * progress;
    return pe_finite_double(out->alpha) && pe_finite_double(out->beta) &&
                   pe_finite_double(out->gamma)
               ? 0
               : -1;
}

const char *pe_dcfr_schedule_name(pe_dcfr_schedule_t schedule)
{
    switch (schedule)
    {
    case PE_DCFR_SCHEDULE_FIXED: return "fixed";
    case PE_DCFR_SCHEDULE_HS15: return "hs15";
    case PE_DCFR_SCHEDULE_HS30: return "hs30";
    case PE_DCFR_SCHEDULE_COUNT:
    default: return "unknown";
    }
}

int pe_dcfr_schedule_parse(const char *text, pe_dcfr_schedule_t *out)
{
    if (!text || !out)
        return -1;
    if (strcmp(text, "fixed") == 0)
        *out = PE_DCFR_SCHEDULE_FIXED;
    else if (strcmp(text, "hs15") == 0)
        *out = PE_DCFR_SCHEDULE_HS15;
    else if (strcmp(text, "hs30") == 0)
        *out = PE_DCFR_SCHEDULE_HS30;
    else
        return -1;
    return 0;
}

int pe_dcfr_discount_regrets(double *regrets, size_t count,
                             uint64_t iteration,
                             const pe_dcfr_params_t *params)
{
    double positive_factor;
    double negative_factor;
    size_t i;

    if (!regrets || count == 0u || !params || iteration == 0u ||
        !valid_exponent(params->alpha) ||
        !valid_regret_exponent(params->beta) ||
        !valid_exponent(params->gamma))
        return -1;

    positive_factor = discount_factor(iteration, params->alpha);
    negative_factor = discount_factor(iteration, params->beta);
    if (!pe_finite_double(positive_factor) || !pe_finite_double(negative_factor))
        return -1;

    for (i = 0; i < count; ++i)
        if (!pe_finite_double(regrets[i]))
            return -1;

    for (i = 0; i < count; ++i)
    {
        double factor = regrets[i] >= 0.0 ? positive_factor : negative_factor;
        regrets[i] *= factor;
        if (!pe_finite_double(regrets[i]))
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
    return pe_finite_double(*out_weight) ? 0 : -1;
}
