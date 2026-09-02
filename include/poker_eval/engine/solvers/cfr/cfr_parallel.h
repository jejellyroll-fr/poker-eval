/* cfr_parallel.h - safe batched parallel execution for the legacy CFR API */

#ifndef POKER_EVAL_CFR_PARALLEL_H
#define POKER_EVAL_CFR_PARALLEL_H

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build an independent game instance for one worker.  The returned game must
 * own or reference worker-private state; callbacks must never mutate shared
 * game_data or initial_state. The factory and destroy callbacks must be safe
 * to invoke concurrently. worker_id is stable in [0, worker_count). */
typedef int (*cfr_parallel_game_factory_fn)(int worker_id,
                                            cfr_game_t *out_game,
                                            void *user_data);

/* Release a game produced by cfr_parallel_game_factory_fn.  It may be NULL
 * when the factory returns a trivially copyable, non-owning game. */
typedef void (*cfr_parallel_game_destroy_fn)(cfr_game_t *game,
                                             int worker_id,
                                             void *user_data);

typedef struct {
    int worker_count;       /* <= 0 uses config.num_threads, then one worker */
    int max_iterations;     /* <= 0 uses config.max_iterations */
    int seed_stride;        /* 0 uses 1; worker seed = seed + id * stride */
} cfr_parallel_config_t;

/* Run independent CFR batches concurrently and merge them into `storage`.
 *
 * This is a safe parallel legacy lane: each worker owns a complete game and
 * storage, so no recursive traversal touches a shared hash table.  It is
 * deterministic at merge time, but it is deliberately documented as batched
 * CFR rather than bit-for-bit sequential CFR: every worker starts its batch
 * from an empty local storage.  Use cfr_solve() when sequential continuation
 * semantics are required.
 *
 * Returns 0 on success, -1 on invalid arguments, factory failure or merge
 * allocation failure.  `out_exploitability` is computed on worker zero's game
 * after the merged policy is installed; pass NULL to skip that expensive walk.
 */
int cfr_solve_parallel_batch(const cfr_parallel_game_factory_fn factory,
                             cfr_parallel_game_destroy_fn destroy,
                             void *factory_user_data,
                             cfr_storage_t *storage,
                             const cfr_config_t *config,
                             const cfr_parallel_config_t *parallel,
                             double *out_exploitability);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_PARALLEL_H */
