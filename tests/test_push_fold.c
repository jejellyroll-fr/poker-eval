#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <poker_eval/economics/push_fold.h>

int main(void)
{
    pe_push_fold_input_t input = {
        .pot_before_push = 1.5,
        .hero_stack = 10.0,
        .villain_stack = 10.0,
        .hero_equity_when_called = 0.5,
        .iterations = 20000
    };
    pe_push_fold_result_t result = {0};
    assert(pe_push_fold_solve(&input, &result) == 0);
    assert(result.iterations == 20000);
    assert(isfinite(result.hero_ev));
    assert(isfinite(result.exploitability));
    assert(result.hero_push_frequency >= 0.0 && result.hero_push_frequency <= 1.0);
    assert(result.villain_call_frequency >= 0.0 && result.villain_call_frequency <= 1.0);
    assert(result.exploitability < 0.02);

    /* A shove is capped by the shorter stack.  With a short villain and
     * positive called EV, pushing must remain strictly better than folding. */
    {
        pe_push_fold_input_t asymmetric = input;
        pe_push_fold_result_t asymmetric_result = {0};
        asymmetric.hero_stack = 100.0;
        asymmetric.villain_stack = 10.0;
        asymmetric.hero_equity_when_called = 0.6;
        assert(pe_push_fold_solve(&asymmetric, &asymmetric_result) == 0);
        assert(asymmetric_result.hero_push_frequency > 0.95);
    }

    {
        pe_push_fold_input_t invalid = input;
        invalid.hero_stack = NAN;
        assert(pe_push_fold_solve(&invalid, &result) == -1);
        invalid = input;
        invalid.villain_stack = INFINITY;
        assert(pe_push_fold_solve(&invalid, &result) == -1);
    }

    puts("Push/fold tests passed");
    return 0;
}
