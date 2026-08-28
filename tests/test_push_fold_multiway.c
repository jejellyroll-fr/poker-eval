#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/economics/push_fold_multiway.h>

int main(void)
{
    pe_push_fold_multiway_input_t input = {
        .pot_before_push = 1.0,
        .hero_stack = 10.0,
        .villain_stacks = {10.0, 10.0},
        .num_villains = 2,
        .hero_equity_by_call_mask = {0.0, 0.5, 0.4, 0.3},
        .iterations = 20000
    };
    pe_push_fold_multiway_result_t result = {0};
    assert(pe_push_fold_multiway_solve(&input, &result) == 0);
    assert(result.iterations == 20000);
    assert(isfinite(result.hero_ev) && isfinite(result.exploitability));
    assert(result.hero_push_frequency >= 0.0 && result.hero_push_frequency <= 1.0);
    for (int i = 0; i < 2; ++i)
        assert(result.villain_call_frequency[i] >= 0.0 && result.villain_call_frequency[i] <= 1.0);

    /* A caller cannot contribute more than the hero can match. */
    {
        pe_push_fold_multiway_input_t asymmetric = input;
        pe_push_fold_multiway_result_t asymmetric_result = {0};
        asymmetric.hero_stack = 100.0;
        asymmetric.villain_stacks[0] = 10.0;
        asymmetric.num_villains = 1;
        asymmetric.hero_equity_by_call_mask[0] = 0.0;
        asymmetric.hero_equity_by_call_mask[1] = 0.6;
        assert(pe_push_fold_multiway_solve(&asymmetric, &asymmetric_result) == 0);
        assert(asymmetric_result.hero_push_frequency > 0.95);
    }

    puts("Multiway push/fold tests passed");
    return 0;
}
