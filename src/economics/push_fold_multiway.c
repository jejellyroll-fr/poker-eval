#include <poker_eval/economics/push_fold_multiway.h>

#include <math.h>
#include <string.h>

static void regret_strategy(double regret[2], double strategy[2])
{
    double sum = fmax(0.0, regret[0]) + fmax(0.0, regret[1]);
    if (sum <= 0.0) { strategy[0] = 0.5; strategy[1] = 0.5; return; }
    strategy[0] = fmax(0.0, regret[0]) / sum;
    strategy[1] = fmax(0.0, regret[1]) / sum;
}

static double profile_probability(int mask, const double villain_strategy[][2], int villains)
{
    double probability = 1.0;
    for (int i = 0; i < villains; ++i)
        probability *= villain_strategy[i][(mask >> i) & 1];
    return probability;
}

static double other_profile_probability(int mask, int excluded,
                                        const double villain_strategy[][2], int villains)
{
    double probability = 1.0;
    for (int i = 0; i < villains; ++i)
        if (i != excluded) probability *= villain_strategy[i][(mask >> i) & 1];
    return probability;
}

static double pushed_payoff(const pe_push_fold_multiway_input_t *input, int mask)
{
    double called_stacks = 0.0;
    int profiles = 1 << input->num_villains;
    if (mask < 0 || mask >= profiles) return 0.0;
    if (mask == 0) return input->pot_before_push;
    for (int i = 0; i < input->num_villains; ++i)
        if (mask & (1 << i)) called_stacks += input->villain_stacks[i];
    return input->hero_equity_by_call_mask[mask] *
               (input->pot_before_push + called_stacks) -
           (1.0 - input->hero_equity_by_call_mask[mask]) * input->hero_stack;
}

int pe_push_fold_multiway_solve(const pe_push_fold_multiway_input_t *input,
                                pe_push_fold_multiway_result_t *result)
{
    double hero_regret[2] = {0.0, 0.0};
    double villain_regret[PE_PUSH_FOLD_MAX_VILLAINS][2] = {{0.0}};
    double hero_sum[2] = {0.0, 0.0};
    double villain_sum[PE_PUSH_FOLD_MAX_VILLAINS][2] = {{0.0}};
    int iterations, profiles;
    if (!input || !result || input->num_villains < 1 ||
        input->num_villains > PE_PUSH_FOLD_MAX_VILLAINS || input->pot_before_push < 0.0 ||
        input->hero_stack <= 0.0) return -1;
    iterations = input->iterations > 0 ? input->iterations : 100000;
    profiles = 1 << input->num_villains;
    for (int i = 0; i < input->num_villains; ++i)
        if (!(input->villain_stacks[i] > 0.0) || !isfinite(input->villain_stacks[i])) return -1;
    for (int mask = 0; mask < profiles; ++mask)
        if (input->hero_equity_by_call_mask[mask] < 0.0 ||
            input->hero_equity_by_call_mask[mask] > 1.0 ||
            !isfinite(input->hero_equity_by_call_mask[mask])) return -1;
    memset(result, 0, sizeof(*result));
    for (int iter = 0; iter < iterations; ++iter) {
        double hs[2], vs[PE_PUSH_FOLD_MAX_VILLAINS][2];
        double node_value = 0.0;
        regret_strategy(hero_regret, hs);
        for (int i = 0; i < input->num_villains; ++i)
            regret_strategy(villain_regret[i], vs[i]);
        hero_sum[0] += hs[0]; hero_sum[1] += hs[1];
        for (int i = 0; i < input->num_villains; ++i) {
            villain_sum[i][0] += vs[i][0];
            villain_sum[i][1] += vs[i][1];
        }
        for (int mask = 0; mask < profiles; ++mask)
            node_value += hs[1] * profile_probability(mask, vs, input->num_villains) *
                          pushed_payoff(input, mask);
        hero_regret[0] += -node_value;
        for (int mask = 0; mask < profiles; ++mask)
            hero_regret[1] += profile_probability(mask, vs, input->num_villains) *
                              pushed_payoff(input, mask) - node_value;
        for (int villain = 0; villain < input->num_villains; ++villain) {
            for (int action = 0; action < 2; ++action) {
                double hero_action_value = 0.0;
                for (int mask = 0; mask < profiles; ++mask) {
                    if (((mask >> villain) & 1) != action) continue;
                    hero_action_value += hs[1] * other_profile_probability(mask, villain, vs,
                                                                             input->num_villains) *
                                         pushed_payoff(input, mask);
                }
                villain_regret[villain][action] += node_value - hero_action_value;
            }
        }
    }
    result->hero_push_frequency = hero_sum[1] / (double)iterations;
    result->hero_ev = 0.0;
    for (int i = 0; i < input->num_villains; ++i)
        result->villain_call_frequency[i] = villain_sum[i][1] / (double)iterations;
    for (int mask = 0; mask < profiles; ++mask) {
        double probability = 1.0;
        for (int i = 0; i < input->num_villains; ++i)
            probability *= ((mask >> i) & 1) ? result->villain_call_frequency[i] :
                                               1.0 - result->villain_call_frequency[i];
        result->hero_ev += result->hero_push_frequency * probability * pushed_payoff(input, mask);
    }
    {
        double hero_push_value = 0.0, villain_best = INFINITY;
        for (int mask = 0; mask < profiles; ++mask) {
            double probability = 1.0;
            for (int i = 0; i < input->num_villains; ++i)
                probability *= ((mask >> i) & 1) ? result->villain_call_frequency[i] :
                                                   1.0 - result->villain_call_frequency[i];
            hero_push_value += probability * pushed_payoff(input, mask);
            villain_best = fmin(villain_best, result->hero_push_frequency * pushed_payoff(input, mask));
        }
        double hero_best = fmax(0.0, hero_push_value);
        result->exploitability = fmax(0.0, hero_best - result->hero_ev) +
                                 fmax(0.0, result->hero_ev - villain_best);
    }
    result->iterations = iterations;
    return 0;
}
