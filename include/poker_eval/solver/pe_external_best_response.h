/* Empirical best response for sampled/external games (Lane B). */
#ifndef POKER_EVAL_PE_EXTERNAL_BEST_RESPONSE_H
#define POKER_EVAL_PE_EXTERNAL_BEST_RESPONSE_H

#include <poker_eval/solver/pe_external_traversal.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t samples;
    uint16_t max_depth;
    uint64_t seed;
} pe_external_br_config_t;

typedef struct {
    double policy_value;
    double br_value;
    double br_gap;
    size_t policy_samples;
    size_t br_samples;
    int empirical;
} pe_external_br_result_t;

pe_external_br_config_t pe_external_br_config_default(void);

/*
 * Estimate a unilateral one-step-deviation BR against the callbacks'
 * behavioral strategy. Chance and opponent actions are sampled. At a BR
 * decision all legal actions are rolled out and the highest sample is used;
 * this is intentionally an empirical estimate, never an exact Nash claim.
 */
int pe_external_best_response_sampled(const pe_external_game_t *game,
                                      uint8_t br_player,
                                      const pe_external_br_config_t *config,
                                      pe_external_br_result_t *out);

#ifdef __cplusplus
}
#endif

#endif
