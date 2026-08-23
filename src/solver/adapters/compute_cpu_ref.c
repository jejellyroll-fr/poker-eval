/*
 * compute_cpu_ref.c - Stable one-thread F64 compute oracle (GPU-01)
 */

#include <poker_eval/solver/pe_compute.h>

#include <math.h>
#include <stdlib.h>

typedef struct
{
    pe_compute_config_t config;
} pe_cpu_ref_t;

static uint64_t cpu_ref_capabilities(void *self)
{
    (void)self;
    return PE_CAP_DETERMINISTIC;
}

static int cpu_ref_create(void **self, const pe_compute_config_t *cfg)
{
    pe_cpu_ref_t *backend;

    if (self == NULL || cfg == NULL || cfg->cpu_threads < 0 ||
        cfg->cpu_threads > 1 || cfg->deterministic != 1 ||
        ((cfg->storage == NULL) != (cfg->storage_self == NULL)))
        return -1;
    backend = (pe_cpu_ref_t *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return -1;
    backend->config = *cfg;
    *self = backend;
    return 0;
}

static void cpu_ref_destroy(void *self)
{
    free(self);
}

static int cpu_ref_strategy_batch(void *self, const pe_infoset_batch_t *in,
                                  pe_strategy_batch_t *out)
{
    (void)self;
    (void)in;
    (void)out;
    return -1;
}

static int cpu_ref_apply_update_batch(void *self,
                                      const pe_update_batch_t *batch)
{
    const pe_cpu_ref_t *backend = (const pe_cpu_ref_t *)self;
    size_t index;

    if (backend == NULL || batch == NULL ||
        (batch->count != 0u && batch->items == NULL))
        return -1;
    for (index = 0u; index < batch->count; ++index) {
        const pe_update_t *update = &batch->items[index];
        if (!isfinite(update->delta) || !isfinite(update->average_delta))
            return -1;
        if (backend->config.storage != NULL) {
            uint16_t actions;
            uint16_t combos;
            size_t regret_length;
            size_t average_length;
            size_t slot;
            double *regrets;
            double *average;

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
                PE_VALUES_REGRET, &regret_length);
            average = backend->config.storage->values(
                backend->config.storage_self, update->infoset,
                PE_VALUES_AVERAGE, &average_length);
            if (regrets == NULL || average == NULL || slot >= regret_length ||
                slot >= average_length ||
                !isfinite(regrets[slot] + update->delta) ||
                !isfinite(average[slot] + update->average_delta))
                return -1;
            regrets[slot] += update->delta;
            average[slot] += update->average_delta;
        }
    }
    return 0;
}

static int cpu_ref_terminal_eval_batch(void *self,
                                       const pe_terminal_batch_t *in,
                                       pe_value_batch_t *out)
{
    (void)self;
    (void)in;
    (void)out;
    return -1;
}

static int cpu_ref_vector_showdown(void *self, const pe_showdown_job_t *job,
                                   pe_value_vec_t *out)
{
    (void)self;
    (void)job;
    (void)out;
    return -1;
}

static int cpu_ref_sync(void *self)
{
    return self == NULL ? -1 : 0;
}

const pe_compute_ops_t *pe_compute_cpu_ref_ops(void)
{
    static const pe_compute_ops_t ops = {
        "cpu_ref",
        cpu_ref_capabilities,
        cpu_ref_create,
        cpu_ref_destroy,
        cpu_ref_strategy_batch,
        cpu_ref_apply_update_batch,
        cpu_ref_terminal_eval_batch,
        cpu_ref_vector_showdown,
        cpu_ref_sync
    };
    return &ops;
}
