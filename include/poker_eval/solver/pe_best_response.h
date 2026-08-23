/*
 * pe_best_response.h - Vector information-set best response (BR-02)
 */

#ifndef POKER_EVAL_PE_BEST_RESPONSE_H
#define POKER_EVAL_PE_BEST_RESPONSE_H

#include <poker_eval/solver/pe_solver.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t max_iterations;
    double tie_tolerance;
} pe_best_response_vector_config_t;

typedef struct
{
    double value;
    uint32_t iterations;
    uint32_t infosets;
    size_t visited_nodes;
    int converged;
} pe_best_response_vector_result_t;

struct pe_vector_game_t;

/** Defaults: 32 fixed-point passes and exact action comparisons. */
pe_best_response_vector_config_t pe_best_response_vector_config_default(void);

/**
 * Compute a vector information-set-consistent best response.
 *
 * The game adapter's terminal_values callback must return raw utility vectors;
 * this routine applies counterfactual reach for every player other than
 * `br_player`. Exact chance enumeration is supported when the optional chance
 * callbacks on pe_vector_game_t are populated; outcome weights are normalized
 * per chance state and default to uniform.
 */
pe_solver_status_t pe_best_response_vector(
    const struct pe_vector_game_t *game,
    uint8_t br_player,
    const pe_best_response_vector_config_t *config,
    pe_best_response_vector_result_t *out_result);

/**
 * Convert a raw exploitability value expressed in the game's currency to
 * milli-big-blinds per game. `big_blind` must use the same currency unit as
 * `raw_value`; a raw value of 0.001 BB therefore becomes 1.0 mbb/g.
 */
pe_solver_status_t pe_best_response_metrics_from_raw(
    double raw_value, double big_blind, pe_metrics_t *out_metrics);

/**
 * Classify the guarantee that applies to a game's exploitability metric.
 *
 * A two-player zero-sum game may report Nash. Multiway zero-sum games report
 * no-regret only, while any non-zero-sum game reports an empirical measure.
 */
pe_solver_status_t pe_best_response_guarantee_for_game(
    uint8_t num_players, int is_zero_sum, pe_guarantee_t *out_guarantee);

/** Return whether a measured mbb/g value satisfies a configured target. */
pe_solver_status_t pe_best_response_target_reached(
    double measured_mbb, double target_mbb, int *out_reached);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BEST_RESPONSE_H */
