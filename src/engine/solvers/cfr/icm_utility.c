/* icm_utility.c - bridge tournament ICM calculations into CFR */
#include <poker_eval/engine/solvers/cfr/icm_utility.h>

#include <string.h>

int pe_cfr_icm_context_init(pe_cfr_icm_context_t *context,
                            const double *payouts,
                            int num_payouts)
{
    if (!context || !payouts || num_payouts <= 0 ||
        num_payouts > ICM_MAX_PLAYERS)
        return -1;
    memset(context, 0, sizeof(*context));
    for (int i = 0; i < num_payouts; ++i)
        context->payouts[i] = payouts[i];
    context->num_payouts = num_payouts;
    return 0;
}

double pe_cfr_icm_utility(const int32_t *final_stacks,
                          int num_players,
                          int player_id,
                          void *user_data)
{
    pe_cfr_icm_context_t *context = (pe_cfr_icm_context_t *)user_data;
    if (!context || !final_stacks || num_players <= 0 ||
        num_players > ICM_MAX_PLAYERS || player_id < 0 ||
        player_id >= num_players || context->num_payouts <= 0)
        return 0.0;

    int same_vector = context->cache_valid &&
                      context->cache_num_players == num_players;
    if (same_vector)
    {
        for (int i = 0; i < num_players; ++i)
        {
            if (context->cache_stacks[i] != final_stacks[i])
            {
                same_vector = 0;
                break;
            }
        }
    }
    if (!same_vector)
    {
        int active_indices[ICM_MAX_PLAYERS];
        int busted_indices[ICM_MAX_PLAYERS];
        int active_count = 0;
        int busted_count = 0;
        for (int i = 0; i < num_players; ++i)
        {
            if (final_stacks[i] > 0)
                active_indices[active_count++] = i;
            else
                busted_indices[busted_count++] = i;
        }
        memset(context->cache_values, 0, sizeof(context->cache_values));
        if (active_count > 0 && active_count <= context->num_payouts)
        {
            icm_input_t input;
            memset(&input, 0, sizeof(input));
            input.num_players = active_count;
            input.num_payouts = active_count;
            for (int i = 0; i < active_count; ++i)
            {
                int original = active_indices[i];
                input.stacks[i] = (double)final_stacks[original];
            }
            for (int i = 0; i < active_count; ++i)
                input.payouts[i] = context->payouts[i];

            icm_result_t result;
            if (pe_icm_calculate(&input, &result) != 0)
                return 0.0;
            for (int i = 0; i < active_count; ++i)
                context->cache_values[active_indices[i]] = result.icm_ev[i];
        }
        for (int i = 0; i < busted_count; ++i)
        {
            int payout_index = active_count + i;
            if (payout_index < context->num_payouts)
                context->cache_values[busted_indices[i]] =
                    context->payouts[payout_index];
        }
        for (int i = 0; i < num_players; ++i)
            context->cache_stacks[i] = final_stacks[i];
        context->cache_num_players = num_players;
        context->cache_valid = 1;
    }
    return context->cache_values[player_id];
}

int pe_cfr_set_icm_utility(cfr_game_t *game, pe_cfr_icm_context_t *context)
{
    if (!game || !context || context->num_payouts <= 0)
        return -1;
    pe_cfr_utility_config_t config;
    config.utility_fn = pe_cfr_icm_utility;
    config.user_data = context;
    config.is_non_linear = 1;
    return pe_cfr_set_utility_function(game, config);
}
