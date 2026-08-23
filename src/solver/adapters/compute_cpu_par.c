/*
 * compute_cpu_par.c - Deterministic CPU-parallel compute adapter (PAR-03)
 */

#include <poker_eval/solver/pe_compute.h>

#include <math.h>
#include <stdlib.h>

typedef struct
{
    pe_compute_config_t config;
    int threads;
} pe_cpu_par_t;

static uint64_t cpu_par_capabilities(void *self)
{
    (void)self;
    return PE_CAP_CPU_PARALLEL | PE_CAP_BATCH_UPDATES | PE_CAP_DETERMINISTIC;
}

static int cpu_par_create(void **self, const pe_compute_config_t *cfg)
{
    pe_cpu_par_t *backend;

    if (!self || !cfg || cfg->cpu_threads < 0 ||
        (cfg->deterministic != 0 && cfg->deterministic != 1))
        return -1;
    backend = (pe_cpu_par_t *)calloc(1u, sizeof(*backend));
    if (!backend)
        return -1;
    backend->config = *cfg;
    /* OpenMP is optional in this build. The adapter remains a valid
       deterministic backend with one worker until an OpenMP runtime is
       available; the public contract does not silently claim a GPU path. */
    backend->threads = cfg->cpu_threads > 0 ? cfg->cpu_threads : 1;
    *self = backend;
    return 0;
}

static void cpu_par_destroy(void *self)
{
    free(self);
}

static int cpu_par_strategy_batch(void *self, const pe_infoset_batch_t *in,
                                  pe_strategy_batch_t *out)
{
    (void)self;
    (void)in;
    (void)out;
    return -1;
}

static int cpu_par_apply_update_batch(void *self,
                                      const pe_update_batch_t *batch)
{
    const pe_cpu_par_t *backend = (const pe_cpu_par_t *)self;
    size_t i;

    if (!backend || !batch || (batch->count != 0u && !batch->items))
        return -1;
    for (i = 0u; i < batch->count; ++i)
    {
        if (!isfinite(batch->items[i].delta) ||
            !isfinite(batch->items[i].average_delta))
            return -1;
    }
    return 0;
}

static int cpu_par_terminal_eval_batch(void *self,
                                       const pe_terminal_batch_t *in,
                                       pe_value_batch_t *out)
{
    (void)self;
    (void)in;
    (void)out;
    return -1;
}

static int cpu_par_vector_showdown(void *self, const pe_showdown_job_t *job,
                                  pe_value_vec_t *out)
{
    (void)self;
    (void)job;
    (void)out;
    return -1;
}

static int cpu_par_sync(void *self)
{
    return self ? 0 : -1;
}

const pe_compute_ops_t *pe_compute_cpu_par_ops(void)
{
    static const pe_compute_ops_t ops = {
        "cpu_par",
        cpu_par_capabilities,
        cpu_par_create,
        cpu_par_destroy,
        cpu_par_strategy_batch,
        cpu_par_apply_update_batch,
        cpu_par_terminal_eval_batch,
        cpu_par_vector_showdown,
        cpu_par_sync
    };
    return &ops;
}
