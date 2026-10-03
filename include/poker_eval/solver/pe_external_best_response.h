/* Empirical best response for sampled/external games (Lane B). */
#ifndef POKER_EVAL_PE_EXTERNAL_BEST_RESPONSE_H
#define POKER_EVAL_PE_EXTERNAL_BEST_RESPONSE_H

#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_br_sampling.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_work_priority.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes shared by the sampled, exact and dispatching entry points.
   They are negative so success stays the only zero. */
#define PE_BR_ERR_INVALID            (-1) /* bad arguments or game callbacks */
#define PE_BR_ERR_BUDGET             (-2) /* exact traversal left its budget */
#define PE_BR_ERR_CHANCE_NOT_ENUMERABLE (-3) /* exact BR, no chance_outcome_count */
#define PE_BR_ERR_TRAVERSAL          (-4) /* depth exceeded or bad policy */
#define PE_BR_ERR_INFOSET_DEPTH      (-5) /* one infoset spans tree depths */
#define PE_BR_OK                     0

typedef struct {
    /* Sampled mode only: trajectory count, depth cap and RNG seed. */
    uint32_t samples;
    uint16_t max_depth;
    uint64_t seed;
    /* Issue #233: which measurement pe_external_best_response() performs.
       AUTO applies conservative default budgets when the limits below are 0. */
    pe_br_mode_t mode;
    /* Exact-mode resource guard: refuse the traversal once more than this
       many states would be visited (0 = unlimited). */
    uint64_t max_br_nodes;
    /* Exact-mode resource guard: refuse once this much wall time (ms) is
       spent (0 = unlimited). */
    uint64_t max_br_time_ms;
    /* Issue #257, sampled mode: confidence-guided evaluation of the BR
       decision. Issue #274: the default configuration enables it at
       PE_BR_SAMPLING_DEFAULT_MAX_SAMPLES, because the historical one rollout
       per action is biased upward by the maximum of noisy draws and no
       trajectory count removes that bias; max_samples == 0 is the explicit
       opt-out. Otherwise each action is sampled until the best one is
       resolved (see pe_br_sampling.h). */
    pe_br_sampling_config_t sampling;
    /* Issue #271, sampled mode: how the confidence-guided evaluation's own
       budget is spread over the decisions. The default (FIFO) changes
       nothing - every decision keeps the sampling config's max_samples.
       Under another policy a decision whose action ordering is less settled
       is allowed to sample further, up to that same max_samples: the cap is
       interpolated over the decision's priority bucket, which is taken from
       the previous measurement of the same infoset. Requires sampling to be
       on; a FIFO policy or sampling off leaves the historical path exactly
       as it was. A game without an infoset_key callback cannot be
       prioritised: with the policy in effect the call returns -1. See
       pe_work_priority.h. */
    pe_work_priority_config_t priority;
} pe_external_br_config_t;

typedef struct {
    double policy_value;
    double br_value;
    double br_gap;
    size_t policy_samples;
    size_t br_samples;
    int empirical;
    /* Issue #233: the mode actually used. AUTO resolves to EXACT or SAMPLED
       here, so a caller can never mistake a sampled estimate for an exact
       result. `guarantee` follows the mode: PE_GUARANTEE_NASH for an exact
       two-player (zero-sum) measurement, PE_GUARANTEE_NO_REGRET_ONLY for an
       exact multiway one, PE_GUARANTEE_EMPIRICAL whenever sampling ran. */
    pe_br_mode_t mode;
    pe_guarantee_t guarantee;
    /* Exact mode only: states visited by the deterministic traversal. */
    size_t nodes_visited;
    /* Issue #257, sampled mode: decisions, draws, early stops, max-budget
       hits, the draws histogram, the final gaps, and terminal evaluations
       (counted with or without the adaptive evaluation). */
    pe_br_sampling_stats_t sampling;
    /* Issue #271, sampled mode: the priority layer's own statistics over the
       infosets this measurement tracked, so a baseline run and a prioritised
       one can be compared field by field. All zero under the default FIFO
       policy. It describes the workload, not the allocation: the allocation
       is per decision, from that decision's bucket, as the traversal runs. */
    pe_work_priority_stats_t priority;
} pe_external_br_result_t;

pe_external_br_config_t pe_external_br_config_default(void);

/*
 * Estimate a unilateral one-step-deviation BR against the callbacks'
 * behavioral strategy. Chance and opponent actions are sampled. At a BR
 * decision all legal actions are rolled out and the highest sample is used;
 * this is intentionally an empirical estimate, never an exact Nash claim.
 *
 * Issue #257: with config->sampling enabled, a BR decision samples each
 * action until the best is resolved or the cap is reached, and uses the
 * chosen action's sample mean. Only this measurement changes; nothing here
 * touches CFR updates.
 */
int pe_external_best_response_sampled(const pe_external_game_t *game,
                                      uint8_t br_player,
                                      const pe_external_br_config_t *config,
                                      pe_external_br_result_t *out);

/*
 * Issue #233: deterministic best response for tractable heads-up games.
 *
 * Every chance node is enumerated via game->chance_outcome_count (required
 * whenever the tree contains a chance node),
 * every opponent decision is taken under the full behavioral strategy and the
 * BR player maximises over all legal actions. The traversal is exhaustive, so
 * the reported br_gap is a ground-truth value, not an estimate.
 * Each BR-player infoset must occur at one tree depth; otherwise the exact
 * entry point returns PE_BR_ERR_INFOSET_DEPTH. AUTO falls back to sampling
 * for that unsupported shape.
 *
 * Resource guards: the traversal aborts with PE_BR_ERR_BUDGET once
 * config->max_br_nodes states have been visited or config->max_br_time_ms of
 * wall time has elapsed, and with PE_BR_ERR_CHANCE_NOT_ENUMERABLE when the
 * adapter provides no chance_outcome_count. There is no silent fallback to
 * sampling: a caller that asks for EXACT either gets an exact number or an
 * explicit error.
 */
int pe_external_best_response_exact(const pe_external_game_t *game,
                                    uint8_t br_player,
                                    const pe_external_br_config_t *config,
                                    pe_external_br_result_t *out);

/*
 * Dispatch by config->mode. EXACT and SAMPLED forward to the matching
 * implementation verbatim. AUTO runs the exact traversal under the configured
 * (or default) budget and falls back to the sampled estimator when that
 * refuses the game; the result then reports mode == PE_BR_SAMPLED and
 * guarantee == PE_GUARANTEE_EMPIRICAL, never an exact claim.
 */
int pe_external_best_response(const pe_external_game_t *game,
                              uint8_t br_player,
                              const pe_external_br_config_t *config,
                              pe_external_br_result_t *out);

/** Stable name of a BR mode, for logs and UI ("exact", "sampled", "auto"). */
const char *pe_br_mode_name(pe_br_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_EXTERNAL_BEST_RESPONSE_H */
