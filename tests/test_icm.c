#include <poker_eval/economics/icm.h>
#include <stdio.h>
#include <assert.h>
#include <float.h>
#include <math.h>

static void test_icm_basic(void)
{
    icm_input_t input = {
        .stacks = {1000.0, 1000.0},
        .num_players = 2,
        .payouts = {70.0, 30.0},
        .num_payouts = 2
    };

    icm_result_t result = {0};
    int err = pe_icm_calculate(&input, &result);
    assert(err == 0);

    /* Equal stacks = Equal EV = 50.0 */
    assert(fabs(result.icm_ev[0] - 50.0) < 1e-6);
    assert(fabs(result.icm_ev[1] - 50.0) < 1e-6);

    printf("✓ Basic ICM passed\n");
    (void)err;
}

static void test_icm_3way(void)
{
    /* Typical Bubble Scenario: 50/30/20 payouts, stacks 5000, 3000, 2000 */
    icm_input_t input = {
        .stacks = {5000.0, 3000.0, 2000.0},
        .num_players = 3,
        .payouts = {50.0, 30.0, 20.0},
        .num_payouts = 3
    };

    icm_result_t result = {0};
    int err = pe_icm_calculate(&input, &result);
    assert(err == 0);

    /* Check sum equals total payout */
    double sum = result.icm_ev[0] + result.icm_ev[1] + result.icm_ev[2];
    assert(fabs(sum - 100.0) < 1e-6);

    /* Large stack has most EV */
    assert(result.icm_ev[0] > result.icm_ev[1]);
    assert(result.icm_ev[1] > result.icm_ev[2]);

    printf("✓ 3-way ICM passed\n");
    (void)err;
    (void)sum;
}

static void test_icm_rejects_non_finite_totals(void)
{
    icm_input_t input = {
        .stacks = {DBL_MAX, DBL_MAX},
        .num_players = 2,
        .payouts = {70.0, 30.0},
        .num_payouts = 2
    };
    icm_result_t result = {0};

    assert(pe_icm_calculate(&input, &result) != 0);
    input.stacks[0] = NAN;
    input.stacks[1] = 100.0;
    assert(pe_icm_calculate(&input, &result) != 0);
    input.stacks[0] = 100.0;
    input.payouts[0] = NAN;
    assert(pe_icm_calculate(&input, &result) != 0);
}

int main(void)
{
    test_icm_basic();
    test_icm_3way();
    test_icm_rejects_non_finite_totals();
    printf("All ICM tests passed!\n");
    return 0;
}
