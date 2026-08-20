/* icm_utility.h - ICM payoff adapter for the generic CFR utility hook */
#ifndef POKER_EVAL_CFR_ICM_UTILITY_H
#define POKER_EVAL_CFR_ICM_UTILITY_H

#include <poker_eval/economics/icm.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_cfr_icm_context_s {
    double payouts[ICM_MAX_PLAYERS];
    int num_payouts;
    int cache_valid;
    int cache_num_players;
    int32_t cache_stacks[ICM_MAX_PLAYERS];
    double cache_values[ICM_MAX_PLAYERS];
} pe_cfr_icm_context_t;

/* Initialize a context from a tournament payout ladder. */
int pe_cfr_icm_context_init(pe_cfr_icm_context_t *context,
                            const double *payouts,
                            int num_payouts);

/* Utility callback suitable for pe_cfr_utility_config_t.utility_fn. */
double pe_cfr_icm_utility(const int32_t *final_stacks,
                          int num_players,
                          int player_id,
                          void *user_data);

/* Install the adapter on a game; context must outlive the solve. */
int pe_cfr_set_icm_utility(cfr_game_t *game, pe_cfr_icm_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_ICM_UTILITY_H */
