#include <poker_eval/economics/push_fold_multiway.h>

#include <math.h>

#include "../solver/domain/finite_double.h"
#include <string.h>

static void regret_strategy(const double *regret, double *strategy,
                            int actions)
{
    double sum = 0.0;
    int action;
    for (action = 0; action < actions; ++action)
        sum += fmax(0.0, regret[action]);
    if (sum <= 0.0)
    {
        for (action = 0; action < actions; ++action)
            strategy[action] = 1.0 / (double)actions;
        return;
    }
    for (action = 0; action < actions; ++action)
        strategy[action] = fmax(0.0, regret[action]) / sum;
}

static double pushed_payoff(const pe_push_fold_multiway_input_t *input, int mask)
{
    double called_stacks = 0.0;
    double effective_risk = 0.0;
    int profiles = 1 << input->num_villains;
    if (mask < 0 || mask >= profiles) return 0.0;
    if (mask == 0) return input->pot_before_push;
    for (int i = 0; i < input->num_villains; ++i)
        if (mask & (1 << i))
        {
            double matched = fmin(input->hero_stack, input->villain_stacks[i]);
            called_stacks += matched;
            effective_risk = fmax(effective_risk, matched);
        }
    return input->hero_equity_by_call_mask[mask] *
               (input->pot_before_push + called_stacks) -
           (1.0 - input->hero_equity_by_call_mask[mask]) * effective_risk;
}

int pe_push_fold_multiway_solve(const pe_push_fold_multiway_input_t *input,
                                pe_push_fold_multiway_result_t *result)
{
    double hero_regret[2] = {0.0, 0.0};
    double coalition_regret[PE_PUSH_FOLD_MAX_PROFILES] = {0.0};
    double hero_sum[2] = {0.0, 0.0};
    double coalition_sum[PE_PUSH_FOLD_MAX_PROFILES] = {0.0};
    int iterations, profiles;
    if (!input || !result || !pe_finite_double(input->pot_before_push) ||
        !pe_finite_double(input->hero_stack) || input->num_villains < 1 ||
        input->num_villains > PE_PUSH_FOLD_MAX_VILLAINS || input->pot_before_push < 0.0 ||
        input->hero_stack <= 0.0) return -1;
    iterations = input->iterations > 0 ? input->iterations : 100000;
    profiles = 1 << input->num_villains;
    for (int i = 0; i < input->num_villains; ++i)
        if (!(input->villain_stacks[i] > 0.0) || !pe_finite_double(input->villain_stacks[i])) return -1;
    for (int mask = 0; mask < profiles; ++mask)
        if (input->hero_equity_by_call_mask[mask] < 0.0 ||
            input->hero_equity_by_call_mask[mask] > 1.0 ||
            !pe_finite_double(input->hero_equity_by_call_mask[mask])) return -1;
    memset(result, 0, sizeof(*result));
    for (int iter = 0; iter < iterations; ++iter) {
        double hs[2];
        double coalition_strategy[PE_PUSH_FOLD_MAX_PROFILES];
        double node_value = 0.0;
        double push_value = 0.0;
        regret_strategy(hero_regret, hs, 2);
        regret_strategy(coalition_regret, coalition_strategy, profiles);
        hero_sum[0] += hs[0]; hero_sum[1] += hs[1];
        for (int mask = 0; mask < profiles; ++mask)
        {
            double probability = coalition_strategy[mask];
            double payoff = pushed_payoff(input, mask);
            push_value += probability * payoff;
            coalition_sum[mask] += probability;
        }
        node_value = hs[1] * push_value;
        hero_regret[0] += -node_value;
        hero_regret[1] += push_value - node_value;
        for (int mask = 0; mask < profiles; ++mask)
            coalition_regret[mask] +=
                node_value - hs[1] * pushed_payoff(input, mask);
    }
    result->hero_push_frequency = hero_sum[1] / (double)iterations;
    result->hero_ev = 0.0;
    for (int i = 0; i < input->num_villains; ++i)
    {
        double call_frequency = 0.0;
        for (int mask = 0; mask < profiles; ++mask)
            if (mask & (1 << i))
                call_frequency += coalition_sum[mask];
        result->villain_call_frequency[i] = call_frequency / (double)iterations;
    }
    {
        double average_push_value = 0.0;
        for (int mask = 0; mask < profiles; ++mask)
            average_push_value += coalition_sum[mask] / (double)iterations *
                                  pushed_payoff(input, mask);
        result->hero_ev = result->hero_push_frequency * average_push_value;
    }
    {
        double hero_push_value = 0.0, villain_best = INFINITY;
        for (int mask = 0; mask < profiles; ++mask) {
            double probability = coalition_sum[mask] / (double)iterations;
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
