#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/economics/icm.h>

int main(void)
{
    icm_asymmetric_input_t input = {
        .stacks = {100.0, 100.0},
        .num_players = 2,
        .payouts = {{70.0, 30.0}, {60.0, 40.0}},
        .num_payouts = 2
    };
    icm_asymmetric_result_t result = {0};
    assert(pe_icm_calculate_asymmetric(&input, &result) == 0);
    assert(fabs(result.finish_probability[0][0] - 0.5) < 1e-9);
    assert(fabs(result.finish_probability[1][0] - 0.5) < 1e-9);
    assert(fabs(result.ev[0] - 50.0) < 1e-9);
    assert(fabs(result.ev[1] - 50.0) < 1e-9);
    assert(fabs(result.finish_probability[0][0] + result.finish_probability[0][1] - 1.0) < 1e-9);
    puts("Asymmetric ICM tests passed");
    return 0;
}
