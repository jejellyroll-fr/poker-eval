/*
 * regret_plus.c - CFR+ regret accumulation and matching (ALG-01)
 */

#include <poker_eval/solver/pe_regret.h>

#include "finite_double.h"

#include <math.h>

int pe_regret_plus_apply_delta(double *regrets, const double *delta,
                               size_t count)
{
    size_t i;

    if (!regrets || !delta || count == 0u)
        return -1;

    /* Validate before mutating so a bad batch cannot leave a partially
       updated regret span. */
    for (i = 0; i < count; ++i)
        if (!pe_finite_double(regrets[i]) || !pe_finite_double(delta[i]))
            return -1;

    for (i = 0; i < count; ++i)
    {
        double updated = regrets[i] + delta[i];
        if (!pe_finite_double(updated))
            return -1;
        regrets[i] = updated > 0.0 ? updated : 0.0;
    }
    return 0;
}

int pe_regret_match_plus_vector(const double *regrets, double *strategy,
                               uint16_t action_count, uint16_t combo_count)
{
    return pe_regret_match_vector(regrets, strategy,
                                  action_count, combo_count);
}
