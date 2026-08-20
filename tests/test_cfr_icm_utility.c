#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/engine/solvers/cfr/icm_utility.h>

int main(void)
{
    const double payouts[] = {65.0, 35.0};
    pe_cfr_icm_context_t context;
    assert(pe_cfr_icm_context_init(&context, payouts, 2) == 0);

    const int32_t stacks[] = {1000, 1000};
    assert(fabs(pe_cfr_icm_utility(stacks, 2, 0, &context) - 50.0) < 1e-9);
    assert(fabs(pe_cfr_icm_utility(stacks, 2, 1, &context) - 50.0) < 1e-9);

    const int32_t asymmetric[] = {1500, 500};
    double strong = pe_cfr_icm_utility(asymmetric, 2, 0, &context);
    double short_stack = pe_cfr_icm_utility(asymmetric, 2, 1, &context);
    assert(strong > short_stack);
    assert(fabs(strong + short_stack - 100.0) < 1e-9);

    assert(pe_cfr_icm_context_init(&context, payouts, 0) != 0);
    assert(pe_cfr_icm_utility(NULL, 2, 0, &context) == 0.0);
    puts("CFR ICM utility tests passed");
    return 0;
}
