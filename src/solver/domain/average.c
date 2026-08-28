/*
 * average.c - Vector strategy averaging (VEC-04)
 */

#include <poker_eval/solver/pe_average.h>

#include "finite_double.h"

#include <math.h>

int pe_average_accumulate_vector(double *weighted, double *normalizer,
                                 const double *strategy,
                                 const double *reach,
                                 uint16_t action_count,
                                 uint16_t combo_count, double weight)
{
    uint16_t action;
    uint16_t combo;

    if (!weighted || !normalizer || !strategy || !reach ||
        action_count == 0u || combo_count == 0u || weight < 0.0 ||
        isnan(weight))
        return -1;

    for (combo = 0; combo < combo_count; ++combo)
        if (reach[combo] < 0.0 || isnan(reach[combo]))
            return -1;

    for (combo = 0; combo < combo_count; ++combo)
    {
        double contribution = reach[combo] * weight;
        normalizer[combo] += contribution;
        for (action = 0; action < action_count; ++action)
        {
            size_t index = (size_t)action * combo_count + combo;
            weighted[index] += strategy[index] * contribution;
        }
    }
    return 0;
}

int pe_average_accumulate_importance_vector(
    double *weighted, double *normalizer, const double *strategy,
    const double *reach, uint16_t action_count, uint16_t combo_count,
    double sampling_probability, double weight)
{
    double corrected_weight;

    if (!pe_finite_double(sampling_probability) || sampling_probability <= 0.0 ||
        sampling_probability > 1.0 || !pe_finite_double(weight) || weight < 0.0)
        return -1;
    corrected_weight = weight / sampling_probability;
    if (!pe_finite_double(corrected_weight))
        return -1;
    return pe_average_accumulate_vector(weighted, normalizer, strategy, reach,
                                        action_count, combo_count,
                                        corrected_weight);
}

int pe_average_accumulate_delayed_linear_vector(
    double *weighted, double *normalizer, const double *strategy,
    const double *reach, uint16_t action_count, uint16_t combo_count,
    uint64_t iteration, uint64_t averaging_delay)
{
    double weight;

    if (iteration == 0u)
        return -1;
    if (iteration <= averaging_delay)
        return 0;

    weight = (double)(iteration - averaging_delay);
    if (!pe_finite_double(weight))
        return -1;
    return pe_average_accumulate_vector(weighted, normalizer, strategy, reach,
                                        action_count, combo_count, weight);
}

int pe_average_finalize_vector(const double *weighted, const double *normalizer,
                               double *out_strategy,
                               uint16_t action_count,
                               uint16_t combo_count)
{
    uint16_t action;
    uint16_t combo;

    if (!weighted || !normalizer || !out_strategy || action_count == 0u ||
        combo_count == 0u)
        return -1;

    for (combo = 0; combo < combo_count; ++combo)
    {
        double denominator = normalizer[combo];
        for (action = 0; action < action_count; ++action)
        {
            size_t index = (size_t)action * combo_count + combo;
            out_strategy[index] = denominator > 0.0
                                    ? weighted[index] / denominator
                                    : 1.0 / (double)action_count;
        }
    }
    return 0;
}
