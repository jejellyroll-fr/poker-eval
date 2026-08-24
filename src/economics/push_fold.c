#include <poker_eval/economics/push_fold.h>

#include <math.h>
#include <string.h>

static void regret_strategy(const double regret[2], double strategy[2])
{
    double sum = 0.0;
    for (int i = 0; i < 2; ++i) sum += regret[i] > 0.0 ? regret[i] : 0.0;
    if (sum <= 0.0) { strategy[0] = 0.5; strategy[1] = 0.5; return; }
    for (int i = 0; i < 2; ++i)
        strategy[i] = (regret[i] > 0.0 ? regret[i] : 0.0) / sum;
}

int pe_push_fold_solve(const pe_push_fold_input_t *input,
                       pe_push_fold_result_t *result)
{
    double payoff[2][2];
    double hero_regret[2] = {0.0, 0.0};
    double villain_regret[2] = {0.0, 0.0};
    double hero_sum[2] = {0.0, 0.0};
    double villain_sum[2] = {0.0, 0.0};
    int iterations;
    if (!input || !result || input->pot_before_push < 0.0 ||
        input->hero_stack <= 0.0 || input->villain_stack <= 0.0 ||
        input->hero_equity_when_called < 0.0 ||
        input->hero_equity_when_called > 1.0)
        return -1;
    iterations = input->iterations > 0 ? input->iterations : 100000;
    memset(result, 0, sizeof(*result));
    /* Rows: hero fold/push. Columns: villain fold/call. */
    payoff[0][0] = 0.0;
    payoff[0][1] = 0.0;
    payoff[1][0] = input->pot_before_push;
    payoff[1][1] = input->hero_equity_when_called *
                   (input->pot_before_push + input->villain_stack) -
                   (1.0 - input->hero_equity_when_called) * input->hero_stack;
    for (int iter = 0; iter < iterations; ++iter) {
        double hs[2], vs[2], hero_value[2], villain_value[2];
        double node_value = 0.0;
        regret_strategy(hero_regret, hs);
        regret_strategy(villain_regret, vs);
        hero_sum[0] += hs[0]; hero_sum[1] += hs[1];
        villain_sum[0] += vs[0]; villain_sum[1] += vs[1];
        for (int h = 0; h < 2; ++h)
            for (int v = 0; v < 2; ++v)
                node_value += hs[h] * vs[v] * payoff[h][v];
        for (int h = 0; h < 2; ++h) {
            hero_value[h] = payoff[h][0] * vs[0] + payoff[h][1] * vs[1];
            hero_regret[h] += hero_value[h] - node_value;
        }
        for (int v = 0; v < 2; ++v) {
            villain_value[v] = -(hs[0] * payoff[0][v] + hs[1] * payoff[1][v]);
            villain_regret[v] += villain_value[v] + node_value;
        }
    }
    result->hero_push_frequency = hero_sum[1] / (double)iterations;
    result->villain_call_frequency = villain_sum[1] / (double)iterations;
    result->hero_ev = result->hero_push_frequency *
        ((1.0 - result->villain_call_frequency) * payoff[1][0] +
         result->villain_call_frequency * payoff[1][1]);
    {
        double hero_br = fmax(payoff[0][0] * (1.0 - result->villain_call_frequency) + payoff[0][1] * result->villain_call_frequency,
                               payoff[1][0] * (1.0 - result->villain_call_frequency) + payoff[1][1] * result->villain_call_frequency);
        double villain_fold_hero_ev = result->hero_push_frequency * payoff[1][0];
        double villain_call_hero_ev = result->hero_push_frequency * payoff[1][1];
        double villain_br_hero_ev = fmin(villain_fold_hero_ev, villain_call_hero_ev);
        result->exploitability = fmax(0.0, hero_br - result->hero_ev) +
                                 fmax(0.0, result->hero_ev - villain_br_hero_ev);
    }
    result->iterations = iterations;
    return 0;
}
