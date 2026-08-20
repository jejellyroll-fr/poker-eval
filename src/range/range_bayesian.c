#include <poker_eval/range.h>

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

pe_status_t pe_range_bayesian_update(pe_range_t *range,
                                     int observed_action,
                                     pe_action_likelihood_fn likelihood_fn,
                                     void *user_data)
{
    if (!range || !range->combos || range->count == 0 || !likelihood_fn)
        return PE_STATUS_INVALID_ARG;

    double *likelihoods = (double *)malloc(range->count * sizeof(*likelihoods));
    if (!likelihoods)
        return PE_STATUS_OUT_OF_MEMORY;
    double *posterior = (double *)malloc(range->count * sizeof(*posterior));
    if (!posterior)
    {
        free(likelihoods);
        return PE_STATUS_OUT_OF_MEMORY;
    }

    long double prior_total = 0.0L;
    long double evidence = 0.0L;
    for (size_t i = 0; i < range->count; ++i)
    {
        const double weight = range->combos[i].weight;
        if (!(weight >= 0.0 && weight <= DBL_MAX))
        {
            free(posterior);
            free(likelihoods);
            return PE_STATUS_ERROR;
        }

        const double likelihood = likelihood_fn(i,
                                                range->combos[i].hand,
                                                observed_action,
                                                user_data);
        if (!(likelihood >= 0.0 && likelihood <= 1.0))
        {
            free(posterior);
            free(likelihoods);
            return PE_STATUS_INVALID_ARG;
        }

        likelihoods[i] = likelihood;
        prior_total += (long double)weight;
        evidence += (long double)weight * (long double)likelihood;
    }

    if (!(prior_total > 0.0L && prior_total <= (long double)DBL_MAX) ||
        !(evidence > 0.0L))
    {
        free(posterior);
        free(likelihoods);
        return PE_STATUS_ERROR;
    }

    /* Divide before multiplying by the prior mass so tiny likelihoods cannot
     * overflow an intermediate normalization scale. Validate every result
     * before changing the caller-owned range. */
    for (size_t i = 0; i < range->count; ++i)
    {
        const long double value =
            ((long double)range->combos[i].weight *
             (long double)likelihoods[i] / evidence) * prior_total;
        if (!(value >= 0.0L && value <= (long double)DBL_MAX))
        {
            free(posterior);
            free(likelihoods);
            return PE_STATUS_ERROR;
        }
        posterior[i] = (double)value;
    }

    for (size_t i = 0; i < range->count; ++i)
        range->combos[i].weight = posterior[i];
    range->total_weight = (double)prior_total;
    free(posterior);
    free(likelihoods);
    return PE_STATUS_OK;
}
