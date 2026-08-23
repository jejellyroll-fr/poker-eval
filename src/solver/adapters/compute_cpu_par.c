/*
 * compute_cpu_par.c - Deterministic CPU-parallel compute adapter (PAR-03)
 */

#include <poker_eval/solver/pe_compute.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct
{
    pe_compute_config_t config;
    int threads;
} pe_cpu_par_t;

typedef struct
{
    double *regrets;
    double *average;
    size_t slot;
    double regret_delta;
    double average_delta;
} pe_update_target_t;

static uint64_t cpu_par_capabilities(void *self)
{
    (void)self;
    return PE_CAP_CPU_PARALLEL | PE_CAP_BATCH_UPDATES | PE_CAP_DETERMINISTIC;
}

static int cpu_par_create(void **self, const pe_compute_config_t *cfg)
{
    pe_cpu_par_t *backend;

    if (!self || !cfg || cfg->cpu_threads < 0 ||
        (cfg->deterministic != 0 && cfg->deterministic != 1) ||
        ((cfg->storage == NULL) != (cfg->storage_self == NULL)))
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

static void cpu_par_strategy_one(const pe_infoset_batch_t *in,
                                 pe_strategy_batch_t *out, size_t infoset)
{
    uint32_t begin = in->offsets[infoset];
    uint16_t actions = in->action_counts[infoset];
    float positive = 0.0f;
    uint16_t action;

    for (action = 0u; action < actions; ++action)
    {
        float regret = in->regrets[begin + action];
        if (regret > 0.0f)
            positive += regret;
    }
    for (action = 0u; action < actions; ++action)
    {
        float regret = in->regrets[begin + action];
        out->strategies[begin + action] = positive > 0.0f
            ? (regret > 0.0f ? regret / positive : 0.0f)
            : 1.0f / (float)actions;
    }
    for (uint32_t slot = begin + actions;
         slot < in->offsets[infoset + 1u]; ++slot)
        out->strategies[slot] = 0.0f;
}

static int cpu_par_strategy_batch(void *self, const pe_infoset_batch_t *in,
                                  pe_strategy_batch_t *out)
{
    const pe_cpu_par_t *backend = (const pe_cpu_par_t *)self;
    size_t infoset;

    if (!backend || !in || !out || (in->count != 0u &&
        (!in->offsets || !in->action_counts || !in->regrets)) ||
        (out->capacity != 0u && !out->strategies) ||
        out->capacity < (in->count != 0u ? in->offsets[in->count] : 0u))
        return -1;
    if (in->count == 0u)
    {
        out->count = 0u;
        out->offsets = in->offsets;
        return 0;
    }
    if (!out->strategies)
        return -1;
    if (!out->offsets)
        out->offsets = in->offsets;

    /* Validate all metadata before entering the parallel region so an invalid
       batch cannot leave a partially written output. */
    for (infoset = 0u; infoset < in->count; ++infoset)
    {
        uint32_t begin = in->offsets[infoset];
        uint32_t end = in->offsets[infoset + 1u];
        uint16_t action;
        if (end < begin || (uint32_t)in->action_counts[infoset] > end - begin)
            return -1;
        for (action = 0u; action < in->action_counts[infoset]; ++action)
            if (!isfinite(in->regrets[begin + action]))
                return -1;
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(backend->threads)
#endif
    for (infoset = 0u; infoset < in->count; ++infoset)
        cpu_par_strategy_one(in, out, infoset);
    out->count = in->count;
    return 0;
}

static int cpu_par_apply_update_batch(void *self,
                                      const pe_update_batch_t *batch)
{
    const pe_cpu_par_t *backend = (const pe_cpu_par_t *)self;
    pe_update_target_t *targets = NULL;
    size_t i;

    if (!backend || !batch || (batch->count != 0u && !batch->items))
        return -1;
    if (backend->config.storage != NULL && batch->count != 0u)
    {
        if (batch->count > SIZE_MAX / sizeof(*targets))
            return -1;
        targets = (pe_update_target_t *)calloc(batch->count, sizeof(*targets));
        if (!targets)
            return -1;
    }

    for (i = 0u; i < batch->count; ++i)
    {
        if (!isfinite(batch->items[i].delta) ||
            !isfinite(batch->items[i].average_delta))
            return -1;

        if (backend->config.storage != NULL && backend->config.storage_self != NULL)
        {
            uint16_t actions;
            uint16_t combos;
            size_t regret_len;
            size_t average_len;
            const pe_update_t *update = &batch->items[i];
            double *regrets;
            double *average;
            size_t slot;

            if (!backend->config.storage->shape ||
                !backend->config.storage->values ||
                !pe_storage_serves(backend->config.storage, PE_VALUES_REGRET) ||
                !pe_storage_serves(backend->config.storage, PE_VALUES_AVERAGE) ||
                backend->config.storage->shape(backend->config.storage_self,
                                               update->infoset, &actions, &combos,
                                               NULL) != 0 ||
                update->action >= actions || update->combo >= combos)
                return -1;

            slot = pe_storage_slot_at(combos, update->action, update->combo);
            regrets = backend->config.storage->values(
                backend->config.storage_self, update->infoset,
                PE_VALUES_REGRET, &regret_len);
            average = backend->config.storage->values(
                backend->config.storage_self, update->infoset,
                PE_VALUES_AVERAGE, &average_len);
            if (!regrets || !average || slot >= regret_len || slot >= average_len ||
                !isfinite(regrets[slot] + update->delta) ||
                !isfinite(average[slot] + update->average_delta))
            {
                free(targets);
                return -1;
            }
            targets[i].regrets = regrets;
            targets[i].average = average;
            targets[i].slot = slot;
            targets[i].regret_delta = update->delta;
            targets[i].average_delta = update->average_delta;
        }
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(backend->threads)
#endif
    for (i = 0u; i < batch->count; ++i)
    {
        if (targets != NULL)
        {
            targets[i].regrets[targets[i].slot] += targets[i].regret_delta;
            targets[i].average[targets[i].slot] += targets[i].average_delta;
        }
    }
    free(targets);
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
