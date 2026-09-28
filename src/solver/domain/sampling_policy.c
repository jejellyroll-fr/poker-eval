/* sampling_policy.c - street-work answers for sampled traversals (ISS-232). */

#include <poker_eval/solver/pe_sampling_policy.h>
#include <poker_eval/solver/pe_online_stats.h>

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "finite_double.h"

uint16_t pe_sampling_replicates_for(pe_sampling_policy_t policy,
                                    const uint16_t *street_replicates,
                                    int street)
{
    if (street < 0 || street >= PE_SAMPLING_STREET_COUNT)
        return 1u;
    if (policy == PE_SAMPLING_STREET_BALANCED && street_replicates)
    {
        uint16_t requested = street_replicates[street];
        /* Zero means "one", so a zero-initialised config stays valid. */
        return requested ? requested : 1u;
    }
    return 1u;
}

const char *pe_sampling_policy_name(pe_sampling_policy_t policy)
{
    const char *name = "unknown";
    switch (policy)
    {
    case PE_SAMPLING_STANDARD: name = "standard"; break;
    case PE_SAMPLING_STREET_BALANCED: name = "street-balanced"; break;
    case PE_SAMPLING_ADAPTIVE_VARIANCE: name = "adaptive-variance"; break;
    case PE_SAMPLING_COUNT: break;
    default: break;
    }
    return name;
}

int pe_sampling_policy_parse(const char *name, pe_sampling_policy_t *out)
{
    if (!name || !out)
        return -1;
    if (strcmp(name, "standard") == 0)
    {
        *out = PE_SAMPLING_STANDARD;
        return 0;
    }
    if (strcmp(name, "street-balanced") == 0)
    {
        *out = PE_SAMPLING_STREET_BALANCED;
        return 0;
    }
    if (strcmp(name, "adaptive-variance") == 0)
    {
        *out = PE_SAMPLING_ADAPTIVE_VARIANCE;
        return 0;
    }
    return -1;
}

int pe_adaptive_sampling_resolve(const pe_adaptive_sampling_t *in,
                                 pe_adaptive_sampling_t *out, double *out_z)
{
    pe_adaptive_sampling_t r;
    double z;

    if (!out)
        return -1;
    if (in)
        r = *in;
    else
        memset(&r, 0, sizeof(r));

    if (r.min_samples == 0u)
        r.min_samples = PE_ADAPTIVE_DEFAULT_MIN_SAMPLES;
    if (r.min_samples < 2u)
        r.min_samples = 2u; /* one draw cannot measure its own spread */
    if (r.max_samples == 0u)
        r.max_samples = r.min_samples > PE_ADAPTIVE_DEFAULT_MAX_SAMPLES
                            ? r.min_samples
                            : PE_ADAPTIVE_DEFAULT_MAX_SAMPLES;
    if (r.check_interval == 0u)
        r.check_interval = PE_ADAPTIVE_DEFAULT_CHECK_INTERVAL;
    if (r.confidence_level == 0.0)
        r.confidence_level = PE_ADAPTIVE_DEFAULT_CONFIDENCE;
    if (!pe_finite_double(r.absolute_tolerance) || r.absolute_tolerance < 0.0 ||
        !pe_finite_double(r.relative_tolerance) || r.relative_tolerance < 0.0)
        return -1;
    if (r.absolute_tolerance == 0.0 && r.relative_tolerance == 0.0)
        r.relative_tolerance = PE_ADAPTIVE_DEFAULT_RELATIVE_TOLERANCE;
    if (r.min_samples > r.max_samples ||
        r.max_samples > PE_ADAPTIVE_MAX_SAMPLES_CEILING)
        return -1;
    z = pe_confidence_z(r.confidence_level);
    if (!pe_finite_double(z) || z <= 0.0)
        return -1;

    *out = r;
    if (out_z)
        *out_z = z;
    return 0;
}

static int parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!text || !*text || *text == '-')
        return -1;
    errno = 0;
    v = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || v > UINT32_MAX)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_real(const char *text, double *out)
{
    char *end = NULL;
    double v;
    if (!text || !*text)
        return -1;
    errno = 0;
    v = strtod(text, &end);
    if (errno != 0 || !end || *end != '\0' || !pe_finite_double(v))
        return -1;
    *out = v;
    return 0;
}

int pe_adaptive_sampling_parse_option(pe_adaptive_sampling_t *settings,
                                      const char *key, const char *value)
{
    if (!settings || !key)
        return -1;
    if (strcmp(key, "min-samples") == 0)
        return parse_u32(value, &settings->min_samples);
    if (strcmp(key, "max-samples") == 0)
        return parse_u32(value, &settings->max_samples);
    if (strcmp(key, "check-interval") == 0)
        return parse_u32(value, &settings->check_interval);
    if (strcmp(key, "confidence") == 0)
        return parse_real(value, &settings->confidence_level);
    if (strcmp(key, "absolute-tolerance") == 0)
        return parse_real(value, &settings->absolute_tolerance);
    if (strcmp(key, "relative-tolerance") == 0)
        return parse_real(value, &settings->relative_tolerance);
    return -1;
}

uint32_t pe_adaptive_sampling_budget(const pe_adaptive_sampling_t *resolved,
                                     double z, double variance, double mean)
{
    double tolerance, needed;
    if (!resolved)
        return 1u;
    if (!pe_finite_double(variance) || variance <= 0.0)
        return resolved->min_samples;
    tolerance = resolved->relative_tolerance * fabs(mean);
    if (resolved->absolute_tolerance > tolerance)
        tolerance = resolved->absolute_tolerance;
    if (!(tolerance > 0.0))
        return resolved->max_samples;
    needed = ceil(z * z * variance / (tolerance * tolerance));
    if (!pe_finite_double(needed) || needed >= (double)resolved->max_samples)
        return resolved->max_samples;
    if (needed <= (double)resolved->min_samples)
        return resolved->min_samples;
    return (uint32_t)needed;
}
