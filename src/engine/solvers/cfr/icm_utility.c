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

    icm_input_t input;
    memset(&input, 0, sizeof(input));
    input.num_players = num_players;
    input.num_payouts = context->num_payouts;
    for (int i = 0; i < num_players; ++i)
        input.stacks[i] = final_stacks[i] > 0 ? (double)final_stacks[i] : 0.0;
    for (int i = 0; i < context->num_payouts; ++i)
        input.payouts[i] = context->payouts[i];

    icm_result_t result;
    if (pe_icm_calculate(&input, &result) != 0)
        return 0.0;
    return result.icm_ev[player_id];
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
