/* cfr_work_adapter.h - concrete WorkUnit to legacy CFR bridge (DIST-02/03) */

#ifndef POKER_EVAL_CFR_WORK_ADAPTER_H
#define POKER_EVAL_CFR_WORK_ADAPTER_H

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_work_reducer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pe_cfr_work_game_build_fn)(
    const pe_work_unit_t *unit,
    pe_compute_kind_t backend,
    cfr_game_t *out_game,
    void *user_data);

typedef void (*pe_cfr_work_game_destroy_fn)(cfr_game_t *game,
                                            void *user_data);

typedef struct pe_cfr_work_executor_config_t
{
    cfr_config_t cfr;
    pe_cfr_work_game_build_fn build_game;
    pe_cfr_work_game_destroy_fn destroy_game;
    void *user_data;
} pe_cfr_work_executor_config_t;

/* Execute a real cfr_solve() for the iteration interval in one WorkUnit.
 * The returned delta is allocated by this function and marked for release by
 * pe_work_result_release(). */
int pe_cfr_work_execute(const pe_work_unit_t *unit,
                        pe_compute_kind_t backend,
                        pe_work_result_t *out_result,
                        void *user_data);

/* Apply accepted worker deltas in the reducer's deterministic order. */
int pe_cfr_work_reducer_apply(pe_work_reducer_t *reducer,
                              cfr_storage_t *destination,
                              size_t *out_applied);

/* A built-in adapter for fixed-deal Hold'em river WorkUnits. `ranges` must
 * contain two big-endian uint64 masks (h0, h1); `boards` contains one mask. */
typedef struct pe_cfr_holdem_river_work_context_t
{
    const EvalContext *context;
    /* Optional real backend instance used by cfr_solve terminal calls. */
    const pe_compute_ops_t *compute_ops;
    void *compute_self;
} pe_cfr_holdem_river_work_context_t;

int pe_cfr_holdem_river_build_game(const pe_work_unit_t *unit,
                                   pe_compute_kind_t backend,
                                   cfr_game_t *out_game,
                                   void *user_data);
void pe_cfr_holdem_river_destroy_game(cfr_game_t *game, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_WORK_ADAPTER_H */
