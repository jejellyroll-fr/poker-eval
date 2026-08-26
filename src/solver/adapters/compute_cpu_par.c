/*
 * compute_cpu_par.c - Deterministic CPU-parallel compute adapter (PAR-03)
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_regret_dcfr.h>

#include "compute_simd.h"

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
    double regret_value;
    double average_value;
} pe_update_target_t;

typedef struct
{
    const pe_update_group_t *group;
    double *regrets;
    double *average;
    size_t regret_length;
    size_t average_length;
} pe_soa_target_t;

static uint64_t cpu_par_capabilities(void *self)
{
    (void)self;
    return PE_CAP_CPU_PARALLEL | PE_CAP_BATCH_UPDATES | PE_CAP_DETERMINISTIC;
}

static int cpu_par_create(void **self, const pe_compute_config_t *cfg)
{
#ifndef _OPENMP
    (void)self;
    (void)cfg;
    /* CPU_PAR is an execution contract, not a label. Never instantiate a
       silently single-threaded adapter when the OpenMP runtime is absent. */
    return -1;
#else
    pe_cpu_par_t *backend;

    if (!self || !cfg || cfg->cpu_threads < 0 ||
        (cfg->deterministic != 0 && cfg->deterministic != 1) ||
        ((cfg->storage == NULL) != (cfg->storage_self == NULL)))
        return -1;
    backend = (pe_cpu_par_t *)calloc(1u, sizeof(*backend));
    if (!backend)
        return -1;
    backend->config = *cfg;
    backend->threads = cfg->cpu_threads > 0 ? cfg->cpu_threads
                                            : omp_get_max_threads();
    if (backend->threads < 1)
    {
        free(backend);
        return -1;
    }
    *self = backend;
    return 0;
#endif
}

static void cpu_par_destroy(void *self)
{
    free(self);
}

static int cpu_par_update_values(const pe_compute_config_t *config,
                                 const pe_update_batch_t *batch,
                                 const pe_update_t *update,
                                 double old_regret, double old_average,
                                 double *out_regret, double *out_average)
{
    double regret = old_regret;
    double average_delta = update->average_delta;

    if (!config || !batch || !update || !out_regret || !out_average ||
        !isfinite(old_regret) || !isfinite(old_average))
        return -1;
    if (config->regret_mode == PE_REGRET_DCFR) {
        pe_dcfr_params_t params = {
            config->dcfr_alpha, config->dcfr_beta, config->dcfr_gamma
        };
        if (pe_dcfr_discount_regrets(&regret, 1u, batch->iteration,
                                     &params) != 0)
            return -1;
    }
    regret += update->delta;
    if (config->regret_mode == PE_REGRET_PLUS && regret < 0.0)
        regret = 0.0;

    switch (config->averaging_mode) {
    case PE_AVG_LINEAR:
        if (batch->iteration == 0u)
            return -1;
        average_delta *= (double)batch->iteration;
        break;
    case PE_AVG_POWER: {
        double weight;
        if (pe_dcfr_average_weight(batch->iteration, config->dcfr_gamma,
                                   &weight) != 0)
            return -1;
        average_delta *= weight;
        break;
    }
    case PE_AVG_DELAYED_LINEAR:
        if (batch->iteration <= (uint64_t)(config->averaging_delay < 0
                                             ? 0 : config->averaging_delay))
            average_delta = 0.0;
        else
            average_delta *= (double)(batch->iteration -
                                      (uint64_t)config->averaging_delay);
        break;
    case PE_AVG_UNIFORM:
    case PE_AVG_IMPORTANCE:
    case PE_AVG_COUNT:
    default:
        break;
    }
    if (!isfinite(regret) || !isfinite(old_average + average_delta))
        return -1;
    *out_regret = regret;
    *out_average = old_average + average_delta;
    return 0;
}

static void cpu_par_strategy_one(const pe_compute_config_t *config,
                                 const pe_infoset_batch_t *in,
                                 pe_strategy_batch_t *out, size_t infoset)
{
    uint32_t begin = in->offsets[infoset];
    uint16_t actions = in->action_counts[infoset];
    float positive = 0.0f;
    uint16_t action;

    if (config->policy_mode == PE_POLICY_EXPONENTIAL)
    {
        float maximum = -INFINITY;
        double total = 0.0;

        for (action = 0u; action < actions; ++action)
        {
            float regret = in->regrets[begin + action];
            if (regret > maximum)
                maximum = regret;
        }
        for (action = 0u; action < actions; ++action)
            total += exp(config->exponential_lambda *
                         ((double)in->regrets[begin + action] -
                          (double)maximum));
        for (action = 0u; action < actions; ++action)
        {
            double weight = exp(config->exponential_lambda *
                                ((double)in->regrets[begin + action] -
                                 (double)maximum));
            out->strategies[begin + action] = (float)(weight / total);
        }
        for (uint32_t slot = begin + actions;
             slot < in->offsets[infoset + 1u]; ++slot)
            out->strategies[slot] = 0.0f;
        return;
    }

    positive = pe_compute_simd_positive_sum(in->regrets + begin, actions);
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
    if (backend->config.policy_mode == PE_POLICY_EXPONENTIAL &&
        (!isfinite(backend->config.exponential_lambda) ||
         backend->config.exponential_lambda <= 0.0))
        return -1;

    /* Validate all metadata before entering the parallel region so an invalid
       batch cannot leave a partially written output. */
    for (infoset = 0u; infoset < in->count; ++infoset)
    {
        uint32_t begin = in->offsets[infoset];
        uint32_t end = in->offsets[infoset + 1u];
        float positive = 0.0f;
        uint16_t action;
        if (end < begin || in->action_counts[infoset] == 0u ||
            (uint32_t)in->action_counts[infoset] > end - begin)
            return -1;
        for (action = 0u; action < in->action_counts[infoset]; ++action)
        {
            if (!isfinite(in->regrets[begin + action]))
                return -1;
            if (in->regrets[begin + action] > 0.0f)
                positive += in->regrets[begin + action];
        }
        if (!isfinite(positive))
            return -1;
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(backend->threads)
#endif
    for (infoset = 0u; infoset < in->count; ++infoset)
        cpu_par_strategy_one(&backend->config, in, out, infoset);
    out->count = in->count;
    return 0;
}

static int cpu_par_apply_update_batch(void *self,
                                      const pe_update_batch_t *batch)
{
    const pe_cpu_par_t *backend = (const pe_cpu_par_t *)self;
    pe_update_target_t *targets = NULL;
    size_t i;

    if (!backend || !batch || (batch->count != 0u && !batch->items) ||
        (batch->soa.group_count != 0u &&
         (batch->soa.groups == NULL || batch->soa.deltas == NULL ||
          batch->soa.average_deltas == NULL)))
        return -1;

    if (batch->soa.group_count != 0u)
    {
        pe_soa_target_t *groups;
        size_t group_index;
        int failed = 0;

        if (backend->config.storage == NULL ||
            backend->config.storage_self == NULL ||
            !backend->config.storage->shape ||
            !backend->config.storage->values ||
            !pe_storage_serves(backend->config.storage, PE_VALUES_REGRET) ||
            !pe_storage_serves(backend->config.storage, PE_VALUES_AVERAGE) ||
            batch->soa.group_count > SIZE_MAX / sizeof(*groups))
            return -1;
        groups = (pe_soa_target_t *)calloc(batch->soa.group_count,
                                           sizeof(*groups));
        if (groups == NULL)
            return -1;

        /* Resolve each infoset once. The expensive update math is performed
           by the parallel loop below, not by a serial validation pass. */
        for (group_index = 0u; group_index < batch->soa.group_count;
             ++group_index)
        {
            const pe_update_group_t *group = &batch->soa.groups[group_index];
            uint16_t actions;
            uint16_t combos;
            size_t values;
            if (group->actions == 0u || group->combos == 0u ||
                group->offset > batch->soa.value_count ||
                (size_t)group->actions > SIZE_MAX / (size_t)group->combos)
            {
                failed = 1;
                break;
            }
            values = (size_t)group->actions * (size_t)group->combos;
            if (values > batch->soa.value_count - group->offset ||
                backend->config.storage->shape(
                    backend->config.storage_self, group->infoset,
                    &actions, &combos, NULL) != 0 ||
                actions != group->actions || combos != group->combos)
            {
                failed = 1;
                break;
            }
            groups[group_index].group = group;
            groups[group_index].regrets = backend->config.storage->values(
                backend->config.storage_self, group->infoset,
                PE_VALUES_REGRET, &groups[group_index].regret_length);
            groups[group_index].average = backend->config.storage->values(
                backend->config.storage_self, group->infoset,
                PE_VALUES_AVERAGE, &groups[group_index].average_length);
            if (groups[group_index].regrets == NULL ||
                groups[group_index].average == NULL ||
                groups[group_index].regret_length < values ||
                groups[group_index].average_length < values)
            {
                failed = 1;
                break;
            }
        }
        if (failed)
        {
            free(groups);
            return -1;
        }

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(backend->threads)
#endif
        for (group_index = 0u; group_index < batch->soa.group_count;
             ++group_index)
        {
            const pe_update_group_t *group = groups[group_index].group;
            size_t value_index;
            for (value_index = 0u;
                 value_index < (size_t)group->actions * group->combos;
                 ++value_index)
            {
                pe_update_t update = {
                    group->infoset,
                    (uint16_t)(value_index / group->combos),
                    (uint16_t)(value_index % group->combos),
                    batch->soa.deltas[group->offset + value_index],
                    batch->soa.average_deltas[group->offset + value_index]
                };
                size_t slot = pe_storage_slot_at(
                    group->combos, update.action, update.combo);
                double new_regret;
                double new_average;
                if (slot >= groups[group_index].regret_length ||
                    slot >= groups[group_index].average_length ||
                    cpu_par_update_values(
                        &backend->config, batch, &update,
                        groups[group_index].regrets[slot],
                        groups[group_index].average[slot],
                        &new_regret, &new_average) != 0)
                {
#ifdef _OPENMP
#pragma omp atomic write
#endif
                    failed = 1;
                    continue;
                }
                groups[group_index].regrets[slot] = new_regret;
                groups[group_index].average[slot] = new_average;
            }
        }
        free(groups);
        return failed ? -1 : 0;
    }
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
            if (!regrets || !average || slot >= regret_len || slot >= average_len)
            {
                free(targets);
                return -1;
            }
            if (cpu_par_update_values(&backend->config, batch, update,
                                      regrets[slot], average[slot],
                                      &targets[i].regret_value,
                                      &targets[i].average_value) != 0)
            {
                free(targets);
                return -1;
            }
            targets[i].regrets = regrets;
            targets[i].average = average;
            targets[i].slot = slot;
        }
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(backend->threads)
#endif
    for (i = 0u; i < batch->count; ++i)
    {
        if (targets != NULL)
        {
            targets[i].regrets[targets[i].slot] = targets[i].regret_value;
            targets[i].average[targets[i].slot] = targets[i].average_value;
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
