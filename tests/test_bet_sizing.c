#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/engine/solvers/cfr/bet_sizing.h>

static double polarized_value(double fraction, double amount, void *ctx)
{
    (void)amount;
    (void)ctx;
    return fraction;
}

static double condensed_value(double fraction, double amount, void *ctx)
{
    (void)amount;
    (void)ctx;
    return 1.0 - fraction;
}

static double indifferent_value(double fraction, double amount, void *ctx)
{
    (void)fraction;
    (void)amount;
    return *(const double *)ctx;
}

static double amount_value(double fraction, double amount, void *ctx)
{
    (void)fraction;
    (void)ctx;
    return amount;
}

static void test_polarized_and_condensed_ranges(void)
{
    const double candidates[] = {0.25, 0.5, 1.0};
    pe_bet_sizing_result_t result;

    assert(pe_optimal_bet_size(100.0, 60.0, candidates, 3,
                               polarized_value, NULL, &result) == 0);
    assert(result.optimal_index == 2);
    assert(result.sizes[2].bet_amount == 60.0);
    assert(result.sizes[2].frequency == 1.0);

    assert(pe_optimal_bet_size(100.0, 60.0, candidates, 3,
                               condensed_value, NULL, &result) == 0);
    assert(result.optimal_index == 0);
    assert(result.sizes[0].frequency == 1.0);
    assert(result.sizes[1].frequency == 0.0);
}

static void test_indifferent_candidates_are_mixed(void)
{
    const double candidates[] = {0.25, 0.5, 1.0};
    double ev = -2.0;
    pe_bet_sizing_result_t result;

    assert(pe_optimal_bet_size(20.0, 100.0, candidates, 3,
                               indifferent_value, (void *)&ev, &result) == 0);
    assert(result.optimal_index == 0);
    assert(fabs(result.max_ev - ev) < 1e-12);
    assert(fabs(result.sizes[0].frequency - 1.0 / 3.0) < 1e-12);
    assert(fabs(result.sizes[1].frequency - 1.0 / 3.0) < 1e-12);
    assert(fabs(result.sizes[2].frequency - 1.0 / 3.0) < 1e-12);
}

static void test_invalid_inputs(void)
{
    const double invalid[] = {0.5, 0.0};
    pe_bet_sizing_result_t result;
    assert(pe_optimal_bet_size(100.0, 100.0, invalid, 2,
                               polarized_value, NULL, &result) == -1);
    assert(pe_optimal_bet_size(100.0, 100.0, invalid, 0,
                               polarized_value, NULL, &result) == -1);
}

static void test_capped_amounts_are_consolidated(void)
{
    const double candidates[] = {0.5, 1.0, 2.0};
    pe_bet_sizing_result_t result;

    assert(pe_optimal_bet_size(100.0, 25.0, candidates, 3,
                               amount_value, NULL, &result) == 0);
    assert(result.num_sizes == 1);
    assert(result.optimal_index == 0);
    assert(result.sizes[0].bet_amount == 25.0);
    assert(result.sizes[0].frequency == 1.0);
}

int main(void)
{
    test_polarized_and_condensed_ranges();
    test_indifferent_candidates_are_mixed();
    test_invalid_inputs();
    test_capped_amounts_are_consolidated();
    puts("bet sizing tests passed");
    return 0;
}
