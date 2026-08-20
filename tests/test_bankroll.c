#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/economics/bankroll.h>

static void test_risk_of_ruin_and_kelly(void)
{
    pe_bankroll_result_t result;
    assert(pe_compute_risk_of_ruin(3000.0, 5.0, 80.0, 0.01, &result) == 0);
    assert(fabs(result.risk_of_ruin - exp(-4.6875)) < 1e-12);
    assert(fabs(result.required_bankroll -
                (80.0 * 80.0 * -log(0.01) / (2.0 * 5.0))) < 1e-9);
    assert(fabs(result.kelly_fraction - 5.0 / (80.0 * 80.0)) < 1e-12);
    assert(fabs(result.half_kelly_fraction -
                0.5 * 5.0 / (80.0 * 80.0)) < 1e-12);
    assert(fabs(result.expected_growth_rate -
                0.5 * 5.0 * 5.0 / (80.0 * 80.0)) < 1e-12);
}

static void test_staking_makeup_settlement(void)
{
    double investor;
    double player;

    assert(pe_compute_staking_split(120.0, 80.0, 100.0, 0.5,
                                   &investor, &player) == 0);
    assert(fabs(investor - 110.0) < 1e-12);
    assert(fabs(player - 10.0) < 1e-12);

    assert(pe_compute_staking_split(50.0, 80.0, 100.0, 0.5,
                                   &investor, &player) == 0);
    assert(fabs(investor - 50.0) < 1e-12);
    assert(fabs(player) < 1e-12);

    assert(pe_compute_staking_split(-20.0, 80.0, 100.0, 0.5,
                                   &investor, &player) == 0);
    assert(fabs(investor + 20.0) < 1e-12);
    assert(fabs(player) < 1e-12);
}

static void test_invalid_inputs(void)
{
    pe_bankroll_result_t result;
    double investor;
    double player;
    assert(pe_compute_risk_of_ruin(3000.0, 0.0, 80.0, 0.01, &result) == -1);
    assert(pe_compute_risk_of_ruin(3000.0, 5.0, 80.0, 1.0, &result) == -1);
    assert(pe_compute_staking_split(10.0, 80.0, 10.0, 1.1,
                                    &investor, &player) == -1);
}

int main(void)
{
    test_risk_of_ruin_and_kelly();
    test_staking_makeup_settlement();
    test_invalid_inputs();
    puts("bankroll tests passed");
    return 0;
}
