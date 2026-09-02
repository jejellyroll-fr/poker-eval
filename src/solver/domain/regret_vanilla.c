/*
 * regret_vanilla.c - Vector regret matching (VEC-03)
 */

#include <poker_eval/solver/pe_regret.h>

int pe_regret_match_vector(const double *regrets, double *strategy,
                           uint16_t action_count, uint16_t combo_count)
{
    uint16_t action;
    uint16_t combo;

    if (!regrets || !strategy || action_count == 0u || combo_count == 0u)
        return -1;

    for (combo = 0; combo < combo_count; ++combo)
    {
        double positive_sum = 0.0;

        for (action = 0; action < action_count; ++action)
        {
            double regret = regrets[(size_t)action * combo_count + combo];
            if (regret > 0.0)
                positive_sum += regret;
        }

        for (action = 0; action < action_count; ++action)
        {
            size_t index = (size_t)action * combo_count + combo;
            double positive = regrets[index] > 0.0 ? regrets[index] : 0.0;
            strategy[index] = positive_sum > 0.0
                                ? positive / positive_sum
                                : 1.0 / (double)action_count;
        }
    }
    return 0;
}
