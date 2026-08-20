#include <poker_eval/range.h>

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

pe_status_t pe_range_bayesian_update(pe_range_t *range,
                                     int observed_action,
                                     pe_action_likelihood_fn likelihood_fn,
                                     void *user_data)
{
    if (!range || !range->combos || range->count == 0 || !likelihood_fn ||
        range->count > UINT16_MAX)
        return PE_STATUS_INVALID_ARG;

    double *likelihoods = (double *)malloc(range->count * sizeof(*likelihoods));
    if (!likelihoods)
        return PE_STATUS_OUT_OF_MEMORY;

    double prior_total = 0.0;
    double evidence = 0.0;
    for (size_t i = 0; i < range->count; ++i)
    {
        const double weight = range->combos[i].weight;
        if (!isfinite(weight) || weight < 0.0)
        {
            free(likelihoods);
            return PE_STATUS_ERROR;
        }

        const double likelihood = likelihood_fn((uint16_t)i,
                                                range->combos[i].hand,
                                                observed_action,
                                                user_data);
        if (!isfinite(likelihood) || likelihood < 0.0 || likelihood > 1.0)
        {
            free(likelihoods);
            return PE_STATUS_INVALID_ARG;
        }

        likelihoods[i] = likelihood;
        prior_total += weight;
        evidence += weight * likelihood;
    }

    if (!isfinite(prior_total) || prior_total <= 0.0 ||
        !isfinite(evidence) || evidence <= 0.0)
    {
        free(likelihoods);
        return PE_STATUS_ERROR;
    }

    const double scale = prior_total / evidence;
    for (size_t i = 0; i < range->count; ++i)
    {
        range->combos[i].weight *= likelihoods[i] * scale;
    }
    range->total_weight = prior_total;
    free(likelihoods);
    return PE_STATUS_OK;
}
