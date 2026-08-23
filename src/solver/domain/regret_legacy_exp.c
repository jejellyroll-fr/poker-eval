/*
 * regret_legacy_exp.c - Legacy ECFR exponential policy (ALG-05)
 */

#include <poker_eval/solver/pe_regret_legacy_exp.h>

#include <math.h>
#include <stddef.h>

int pe_regret_match_legacy_exp_vector(const double *regrets, double *strategy,
                                      uint16_t action_count,
                                      uint16_t combo_count,
                                      double lambda)
{
    uint16_t action;
    uint16_t combo;

    if (!regrets || !strategy || action_count == 0u || combo_count == 0u ||
        !isfinite(lambda) || lambda <= 0.0)
        return -1;

    /* Validate first so a malformed span cannot leave a partially written
       strategy. */
    for (action = 0; action < action_count; ++action)
    {
        for (combo = 0; combo < combo_count; ++combo)
        {
            if (!isfinite(regrets[(size_t)action * combo_count + combo]))
                return -1;
        }
    }

    for (combo = 0; combo < combo_count; ++combo)
    {
        double max_positive = 0.0;
        double sum_weights = 0.0;

        for (action = 0; action < action_count; ++action)
        {
            double regret = regrets[(size_t)action * combo_count + combo];
            if (regret > max_positive)
                max_positive = regret;
        }

        if (max_positive > 0.0)
        {
            for (action = 0; action < action_count; ++action)
            {
                size_t index = (size_t)action * combo_count + combo;
                double regret = regrets[index];

                if (regret > 0.0)
                {
                    strategy[index] = exp(lambda * (regret - max_positive));
                    sum_weights += strategy[index];
                }
                else
                {
                    strategy[index] = 0.0;
                }
            }

            if (sum_weights > 0.0 && isfinite(sum_weights))
            {
                for (action = 0; action < action_count; ++action)
                {
                    size_t index = (size_t)action * combo_count + combo;
                    strategy[index] /= sum_weights;
                }
                continue;
            }

            /* A finite, positive lambda and finite regrets should make this
               unreachable, but retain the old uniform fallback if a future
               platform's exp implementation underflows every weight. */
        }

        for (action = 0; action < action_count; ++action)
            strategy[(size_t)action * combo_count + combo] =
                1.0 / (double)action_count;
    }

    return 0;
}
