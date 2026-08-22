/*
 * cfr_traversal.h - Internal interface of the scalar tree walk (EXT-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Not a public header. It exists so the exhaustive scalar traversal can live in
 * its own translation unit while cfr_core.c keeps the solve loop, the metrics
 * and the algorithm selection.
 *
 * The split follows one rule: the traversal walks the tree and produces raw
 * regret deltas; it never decides how they accumulate. Discounting and the
 * averaging weight are asked for through cfr_algo_ops_t, so adding CFR+ or a
 * canonical DCFR later means writing an ops implementation, not another
 * branch inside the recursion.
 *
 * The traversal still stays under src/engine/. Moving it to src/solver/domain/
 * would need the 11 storage and best-response entry points it calls to become
 * ports first (STO-03 for storage, EXT-08 for the locks); until then the move
 * would only invert the library dependency into a cycle, since poker_engine
 * already links poker_solver.
 */

#ifndef POKER_EVAL_CFR_TRAVERSAL_H
#define POKER_EVAL_CFR_TRAVERSAL_H

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/solver/pe_telemetry.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * State of one tree walk (EXT-02).
 *
 * These seven values used to be thread-local statics. That was enough to keep
 * two threads from corrupting each other, and the single-threaded solver shows
 * no observable difference — the change was structural. What it bought is this
 * file: a function reading a file-static cannot move to another translation
 * unit. The parallel backend will want one context per worker.
 */
typedef struct
{
    int current_iter;
    int recursion_depth;
    int max_depth;
    /* Reset before each iteration's traversal. The traversal and the
       best-response walks it triggers share it, so a runaway tree is reported
       once per iteration rather than once per walk. */
    int depth_exceeded;
    long node_count;
    int use_flow_focus;
    double flow_pow;
    /* Where this walk's messages go. Resolved once by cfr_solve so no call
       site has to test for NULL. */
    const pe_telemetry_ops_t *telemetry;
} cfr_walk_ctx_t;

void cfr_walk_ctx_init(cfr_walk_ctx_t *walk);

/*
 * How raw deltas turn into accumulated regret and into the average strategy.
 *
 * This is the seam that keeps algorithm selection out of the recursion. The
 * traversal asks for two numbers and applies them; which formula produced them
 * is none of its business, which is why cfr_traversal_full_scalar.c mentions
 * neither DCFR nor linear averaging.
 */
typedef struct cfr_algo_ops_t
{
    /*
     * Factor applied to the already-accumulated regret before this node's
     * delta is added: regret = regret * discount + delta. 1.0 accumulates
     * plainly.
     */
    double (*regret_discount)(const struct cfr_algo_ops_t *ops, int iter);

    /*
     * Weight of this node's contribution to the average strategy.
     *
     * `reach` is the acting player's reach probability, and `flow_weight` the
     * flow-focusing factor already computed by the traversal — passed in
     * rather than recomputed so the two cannot drift apart.
     */
    double (*average_weight)(const struct cfr_algo_ops_t *ops, int iter,
                             double reach, double flow_weight, int use_flow_focus);

    /* Read by the implementations, never by the traversal. */
    const cfr_config_t *config;
} cfr_algo_ops_t;

/* Helpers shared with cfr_core.c. External linkage only so the traversal can
   reach them from its own translation unit; they are not public API. */
uint64_t cfr_traversal_storage_key(cfr_game_t *game, uint64_t state_key);
int cfr_traversal_storage_street(cfr_game_t *game, uint64_t state_key);
void cfr_traversal_terminal_utilities(cfr_game_t *game, uint64_t state_key,
                                      int num_players, double *out_util,
                                      void *user_data);

double cfr_best_response_recursive(cfr_game_t *game, cfr_storage_t *storage,
                                   int br_player, int current_player,
                                   uint64_t state_key, void *user_data, int depth,
                                   int *depth_exceeded,
                                   const pe_telemetry_ops_t *telemetry);

double cfr_best_response_recursive_multiway(cfr_game_t *game, cfr_storage_t *storage,
                                            int br_player, uint64_t state_key,
                                            void *user_data, int depth,
                                            int *depth_exceeded,
                                            const pe_telemetry_ops_t *telemetry);

/* The exhaustive scalar walk: every action, every chance outcome. */
void cfr_traverse_recursive(cfr_game_t *game,
                            cfr_storage_t *storage,
                            const cfr_config_t *config,
                            const cfr_algo_ops_t *algo,
                            uint64_t state_key,
                            double *reach,
                            int num_players,
                            int iter,
                            double *out_util,
                            void *user_data,
                            double *scratch,
                            int depth_limit,
                            cfr_walk_ctx_t *walk);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_TRAVERSAL_H */
