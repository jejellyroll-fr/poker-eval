/* cfr_parallel.c - safe batched parallel execution for legacy CFR */

#include <poker_eval/engine/solvers/cfr/cfr_parallel.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef PE_LEGACY_CFR_OPENMP
#include <omp.h>
#endif

typedef struct {
    cfr_game_t game;
    cfr_storage_t *storage;
    int created;
    int ready;
    int error;
} cfr_parallel_worker_t;

static int worker_run(cfr_parallel_worker_t *worker,
                      int worker_id,
                      const cfr_parallel_game_factory_fn factory,
                      cfr_parallel_game_destroy_fn destroy,
                      void *factory_user_data,
                      const cfr_config_t *base_config,
                      int iterations,
                      int seed_stride)
{
    cfr_config_t local_config;
    int factory_result;

    memset(&worker->game, 0, sizeof(worker->game));
    worker->storage = NULL;
    worker->created = 0;
    worker->ready = 0;
    worker->error = 0;
    factory_result = factory(worker_id, &worker->game, factory_user_data);
    if (factory_result != 0)
    {
        worker->error = 1;
        return -1;
    }
    worker->created = 1;
    worker->storage = cfr_storage_create();
    if (!worker->storage)
    {
        worker->error = 1;
        return -1;
    }

    local_config = *base_config;
    local_config.max_iterations = iterations;
    local_config.num_threads = 1;
    local_config.checkpoint_interval = 0;
    local_config.checkpoint_path = NULL;
    local_config.resume_path = NULL;
    local_config.checkpoint_final = 0;
    local_config.monitor_fn = NULL;
    local_config.monitor_user = NULL;
    local_config.monitor_period = 0;
    local_config.metrics_fn = NULL;
    local_config.metrics_user = NULL;
    local_config.metrics_buffer = NULL;
    local_config.stop_flag = NULL;
    local_config.exploitability_interval = 0;
    local_config.convergence_threshold = 0.0;
    local_config.progress_interval = 0;
    local_config.trace_iterations = 0;
    /* Workers must retain the complete average strategy so the merge does not
     * silently substitute a regret-matched fallback for a street filtered out
     * in the caller's final storage. The destination masks are applied below. */
    local_config.keep_avg_strategy_mask = 0;
    local_config.keep_ev_mask = 0;
    local_config.seed = base_config->seed + worker_id * seed_stride;

    /* A worker's game is private, so cfr_solve may recurse normally.  The
     * local storage is also independent and is merged only after all workers
     * have stopped mutating it. */
    if (cfr_solve(&worker->game, worker->storage, &local_config, NULL) < 0.0)
    {
        worker->error = 1;
        return -1;
    }
    worker->ready = 1;
    return 0;
}

int cfr_solve_parallel_batch(const cfr_parallel_game_factory_fn factory,
                             cfr_parallel_game_destroy_fn destroy,
                             void *factory_user_data,
                             cfr_storage_t *storage,
                             const cfr_config_t *config,
                             const cfr_parallel_config_t *parallel,
                             double *out_exploitability)
{
    cfr_parallel_worker_t *workers = NULL;
    int worker_count;
    int iterations;
    int seed_stride;
    int base_iterations;
    int remainder;
    int i;
    int failed = 0;
    int measure_worker = -1;

    if (out_exploitability)
        *out_exploitability = -1.0;
    if (!factory || !storage || !config)
    {
        errno = EINVAL;
        return -1;
    }

    worker_count = parallel ? parallel->worker_count : 0;
    if (worker_count <= 0)
        worker_count = config->num_threads;
    if (worker_count <= 0)
        worker_count = 1;
    iterations = parallel ? parallel->max_iterations : 0;
    if (iterations <= 0)
        iterations = config->max_iterations;
    if (iterations <= 0)
    {
        errno = EINVAL;
        return -1;
    }
    seed_stride = parallel ? parallel->seed_stride : 0;
    if (seed_stride == 0)
        seed_stride = 1;

    cfr_storage_set_strategy_mode_for(storage, config->enable_ecfr,
                                      config->ecfr_lambda);
    cfr_storage_set_memory_masks(storage, config->keep_avg_strategy_mask,
                                 config->keep_ev_mask);
    cfr_storage_set_num_threads(storage, 1);

    workers = (cfr_parallel_worker_t *)calloc((size_t)worker_count,
                                               sizeof(*workers));
    if (!workers)
        return -1;

    base_iterations = iterations / worker_count;
    remainder = iterations % worker_count;

#ifdef PE_LEGACY_CFR_OPENMP
#pragma omp parallel for schedule(static) num_threads(worker_count)
#endif
    for (i = 0; i < worker_count; ++i)
    {
        int worker_iterations = base_iterations + (i < remainder ? 1 : 0);
        if (worker_iterations == 0)
            continue;
        (void)worker_run(&workers[i], i, factory, destroy, factory_user_data,
                         config, worker_iterations, seed_stride);
    }

    for (i = 0; i < worker_count; ++i)
        if (workers[i].error)
            failed = 1;

    /* The merge is intentionally serial and worker-id ordered.  This makes
     * floating-point accumulation reproducible even when the workers finish
     * in a different order. */
    if (!failed)
    {
        for (i = 0; i < worker_count; ++i)
        {
            if (!workers[i].ready ||
                cfr_storage_merge_scaled(storage, workers[i].storage,
                                         1.0, 1.0) != 0)
            {
                failed = 1;
                break;
            }
            if (measure_worker < 0 && workers[i].ready)
                measure_worker = i;
        }
    }

    if (!failed && out_exploitability && measure_worker >= 0)
    {
        /* Any ready worker is a valid independent view of the game topology.
         * The merged storage, not its local storage, is the policy measured. */
        *out_exploitability = cfr_exploitability(&workers[measure_worker].game,
                                                  storage, NULL);
    }

    for (i = 0; i < worker_count; ++i)
    {
        if (workers[i].storage)
            cfr_storage_destroy(workers[i].storage);
        if (workers[i].created)
        {
            if (destroy)
                destroy(&workers[i].game, i, factory_user_data);
        }
    }
    free(workers);
    if (failed)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}
