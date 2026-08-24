#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/economics/pko.h>

int main(void)
{
    pe_pko_input_t input = {0};
    pe_pko_result_t result = {0};
    input.icm.num_players = 2;
    input.icm.num_payouts = 2;
    input.icm.stacks[0] = 100.0;
    input.icm.stacks[1] = 100.0;
    input.icm.payouts[0][0] = 70.0;
    input.icm.payouts[0][1] = 30.0;
    input.icm.payouts[1][0] = 70.0;
    input.icm.payouts[1][1] = 30.0;
    input.bounties[1] = 25.0;
    input.elimination_probability[0][1] = 0.5;
    input.bounty_multiplier = 1.0;
    assert(pe_pko_calculate(&input, &result) == 0);
    assert(fabs(result.icm_ev[0] - 50.0) < 1e-9);
    assert(fabs(result.bounty_ev[0] - 12.5) < 1e-9);
    assert(fabs(result.total_ev[0] - 62.5) < 1e-9);
    assert(isfinite(result.total_ev[1]));
    puts("PKO overlay tests passed");
    return 0;
}
