/*
 * compute_opencl.c - OpenCL terminal-evaluation compute adapter (GPU-04)
 *
 * This adapter mirrors compute_cuda.c while remaining independent of OpenCL
 * headers unless the existing OpenCL batched-evaluator target is configured.
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/gpu/pe_regret_opencl.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(PE_COMPUTE_OPENCL_AVAILABLE)
#include <poker_eval/gpu/eval_batched_gpu.h>
#endif

typedef struct
{
#if defined(PE_COMPUTE_OPENCL_AVAILABLE)
    gpu_eval_context_t *context;
#endif
    pe_compute_config_t config;
    size_t max_batch_size;
    pe_regret_opencl_context_t *regret_context;
} pe_compute_opencl_t;

typedef struct
{
    pe_infoset_id_t id;
    uint16_t actions;
    uint16_t combos;
    size_t length;
    double *regrets;
    double *averages;
} pe_opencl_update_group_t;

static uint64_t compute_opencl_capabilities(void *self)
{
    (void)self;
    return pe_gpu_terminal_eval_gate_is_open() ? PE_CAP_GPU_TERMINAL_EVAL : 0u;
}

static int compute_opencl_create(void **self, const pe_compute_config_t *cfg)
{
    if (self == NULL || cfg == NULL || cfg->terminal_batch_size == 0u)
        return -1;
    *self = NULL;

#if !defined(PE_COMPUTE_OPENCL_AVAILABLE)
    return -1;
#else
    pe_compute_opencl_t *backend =
        (pe_compute_opencl_t *)calloc(1u, sizeof(*backend));
    gpu_eval_config_t gpu_cfg;

    if (backend == NULL)
        return -1;
    backend->config = *cfg;
    gpu_cfg = gpu_eval_default_config();
    gpu_cfg.preferred_backend = GPU_BACKEND_OPENCL;
    gpu_cfg.max_batch_size = cfg->terminal_batch_size;
    backend->context = gpu_eval_init(&gpu_cfg);
    if (backend->context == NULL) {
        free(backend);
        return -1;
    }
    backend->max_batch_size = cfg->terminal_batch_size;
#if defined(PE_COMPUTE_OPENCL_AVAILABLE)
    backend->regret_context = pe_regret_opencl_create();
#endif
    *self = backend;
    return 0;
#endif
}

static void compute_opencl_destroy(void *self)
{
    pe_compute_opencl_t *backend = (pe_compute_opencl_t *)self;

#if defined(PE_COMPUTE_OPENCL_AVAILABLE)
    if (backend != NULL)
        pe_regret_opencl_destroy(backend->regret_context);
    if (backend != NULL)
        gpu_eval_free(backend->context);
#endif
    free(backend);
}

static int compute_opencl_strategy_batch(void *self,
                                         const pe_infoset_batch_t *in,
                                         pe_strategy_batch_t *out)
{
    pe_compute_opencl_t *backend = (pe_compute_opencl_t *)self;
#if defined(PE_COMPUTE_OPENCL_AVAILABLE)
    return backend ? pe_regret_opencl_strategy_batch(backend->regret_context,
                                                     in, out) : -1;
#else
    (void)backend;
    (void)in;
    (void)out;
    return -1;
#endif
}

static int compute_opencl_apply_update_batch(void *self,
                                             const pe_update_batch_t *batch)
{
    pe_compute_opencl_t *backend = (pe_compute_opencl_t *)self;
    const pe_storage_ops_t *storage;
    pe_opencl_update_group_t *groups = NULL;
    pe_infoset_id_t *infosets = NULL;
    uint32_t *offsets = NULL;
    uint16_t *action_counts = NULL;
    uint16_t *combo_counts = NULL;
    float *regrets = NULL;
    float *averages = NULL;
    uint32_t *slots = NULL;
    float *regret_deltas = NULL;
    float *average_deltas = NULL;
    pe_regret_opencl_update_batch_t gpu_batch;
    pe_infoset_layout_t layout;
    size_t group_count = 0u;
    size_t total_slots = 0u;
    size_t gpu_count = 0u;
    size_t i;
    int result = -1;

    if (backend == NULL || batch == NULL)
        return -1;
    if (batch->count == 0u)
        return 0;
#if !defined(PE_COMPUTE_OPENCL_AVAILABLE)
    (void)batch;
    return -1;
#else
    if (batch->items == NULL)
        return -1;
    storage = backend->config.storage;
    if (backend->regret_context == NULL || backend->config.storage_self == NULL ||
        storage == NULL || storage->shape == NULL || storage->values == NULL ||
        !pe_storage_serves(storage, PE_VALUES_REGRET) ||
        !pe_storage_serves(storage, PE_VALUES_AVERAGE))
        return -1;

    if (batch->count > SIZE_MAX / sizeof(*groups) ||
        batch->count > SIZE_MAX / sizeof(*slots) ||
        batch->count > SIZE_MAX / sizeof(*regret_deltas))
        return -1;
    groups = (pe_opencl_update_group_t *)calloc(batch->count, sizeof(*groups));
    infosets = (pe_infoset_id_t *)calloc(batch->count, sizeof(*infosets));
    offsets = (uint32_t *)calloc(batch->count + 1u, sizeof(*offsets));
    action_counts = (uint16_t *)calloc(batch->count, sizeof(*action_counts));
    combo_counts = (uint16_t *)calloc(batch->count, sizeof(*combo_counts));
    slots = (uint32_t *)calloc(batch->count, sizeof(*slots));
    regret_deltas = (float *)calloc(batch->count, sizeof(*regret_deltas));
    average_deltas = (float *)calloc(batch->count, sizeof(*average_deltas));
    if (groups == NULL || infosets == NULL || offsets == NULL ||
        action_counts == NULL || combo_counts == NULL || slots == NULL ||
        regret_deltas == NULL || average_deltas == NULL)
        goto cleanup;

    for (i = 0u; i < batch->count; ++i)
    {
        const pe_update_t *update = &batch->items[i];
        size_t group;

        if (!isfinite(update->delta) || !isfinite(update->average_delta))
            goto cleanup;
        for (group = 0u; group < group_count; ++group)
            if (groups[group].id == update->infoset)
                break;
        if (group != group_count)
            continue;

        if (storage->shape(backend->config.storage_self, update->infoset,
                           &groups[group_count].actions,
                           &groups[group_count].combos, NULL) != 0 ||
            groups[group_count].actions == 0u ||
            groups[group_count].combos == 0u ||
            update->infoset == PE_INFOSET_ID_INVALID)
            goto cleanup;
        groups[group_count].id = update->infoset;
        groups[group_count].length =
            (size_t)groups[group_count].actions *
            (size_t)groups[group_count].combos;
        if (groups[group_count].length > SIZE_MAX - total_slots ||
            total_slots + groups[group_count].length > UINT32_MAX)
            goto cleanup;
        total_slots += groups[group_count].length;
        ++group_count;
    }

    if (group_count == 0u || total_slots == 0u ||
        total_slots > SIZE_MAX / sizeof(*regrets))
        goto cleanup;
    for (i = 0u; i < group_count; ++i)
    {
        infosets[i] = groups[i].id;
        action_counts[i] = groups[i].actions;
        combo_counts[i] = groups[i].combos;
        offsets[i + 1u] = offsets[i] + (uint32_t)groups[i].length;
        groups[i].regrets = storage->values(backend->config.storage_self,
                                            groups[i].id, PE_VALUES_REGRET,
                                            &groups[i].length);
        groups[i].averages = storage->values(backend->config.storage_self,
                                             groups[i].id, PE_VALUES_AVERAGE,
                                             NULL);
        if (groups[i].regrets == NULL || groups[i].averages == NULL)
            goto cleanup;
    }

    regrets = (float *)calloc(total_slots, sizeof(*regrets));
    averages = (float *)calloc(total_slots, sizeof(*averages));
    if (regrets == NULL || averages == NULL)
        goto cleanup;
    for (i = 0u; i < group_count; ++i)
    {
        size_t j;
        size_t length = (size_t)action_counts[i] * (size_t)combo_counts[i];
        if (groups[i].length < length)
            goto cleanup;
        for (j = 0u; j < length; ++j)
        {
            regrets[offsets[i] + j] = (float)groups[i].regrets[j];
            averages[offsets[i] + j] = (float)groups[i].averages[j];
            if (!isfinite(regrets[offsets[i] + j]) ||
                !isfinite(averages[offsets[i] + j]))
                goto cleanup;
        }
    }

    layout.count = group_count;
    layout.infosets = infosets;
    layout.offsets = offsets;
    layout.action_counts = action_counts;
    layout.combo_counts = combo_counts;
    for (i = 0u; i < batch->count; ++i)
    {
        uint32_t slot;
        float regret_delta = (float)batch->items[i].delta;
        float average_delta = (float)batch->items[i].average_delta;
        size_t prior;

        if (!isfinite(regret_delta) || !isfinite(average_delta) ||
            pe_infoset_layout_resolve_slot(&layout, &batch->items[i], &slot) !=
                0)
            goto cleanup;
        for (prior = 0u; prior < gpu_count; ++prior)
            if (slots[prior] == slot)
                break;
        if (prior != gpu_count)
        {
            regret_deltas[prior] += regret_delta;
            average_deltas[prior] += average_delta;
            if (!isfinite(regret_deltas[prior]) ||
                !isfinite(average_deltas[prior]))
                goto cleanup;
        }
        else
        {
            slots[gpu_count] = slot;
            regret_deltas[gpu_count] = regret_delta;
            average_deltas[gpu_count] = average_delta;
            ++gpu_count;
        }
    }

    gpu_batch.count = gpu_count;
    gpu_batch.slots = slots;
    gpu_batch.regret_deltas = regret_deltas;
    gpu_batch.average_deltas = average_deltas;
    if (pe_regret_opencl_apply_update_slots(backend->regret_context, regrets,
                                            averages, total_slots,
                                            &gpu_batch) != 0)
        goto cleanup;
    for (i = 0u; i < group_count; ++i)
    {
        size_t j;
        size_t length = (size_t)action_counts[i] * (size_t)combo_counts[i];
        for (j = 0u; j < length; ++j)
        {
            if (!isfinite((double)regrets[offsets[i] + j]) ||
                !isfinite((double)averages[offsets[i] + j]))
                goto cleanup;
            groups[i].regrets[j] = (double)regrets[offsets[i] + j];
            groups[i].averages[j] = (double)averages[offsets[i] + j];
        }
    }
    result = 0;

cleanup:
    free(average_deltas);
    free(regret_deltas);
    free(slots);
    free(averages);
    free(regrets);
    free(combo_counts);
    free(action_counts);
    free(offsets);
    free(infosets);
    free(groups);
    return result;
#endif
}

static int compute_opencl_terminal_eval_batch(void *self,
                                              const pe_terminal_batch_t *in,
                                              pe_value_batch_t *out)
{
    pe_compute_opencl_t *backend = (pe_compute_opencl_t *)self;

    if (backend == NULL || in == NULL || out == NULL || in->count == 0u ||
        in->count > backend->max_batch_size || out->values == NULL ||
        out->capacity < in->count)
        return -1;

#if !defined(PE_COMPUTE_OPENCL_AVAILABLE)
    (void)in;
    (void)out;
    return -1;
#else
    if (in->game == game_holdem && in->cards != NULL &&
        gpu_eval_holdem_batch(backend->context, in->cards, in->count,
                              out->values) == 0) {
        out->count = in->count;
        return 0;
    }
    if ((in->game == game_omaha || in->game == game_omaha8) &&
        in->hole != NULL && in->board != NULL &&
        gpu_eval_omaha_batch(backend->context, in->hole, in->board, in->count,
                             out->values) == 0) {
        out->count = in->count;
        return 0;
    }
    if (in->game == game_omaha5 && in->hole != NULL && in->board != NULL &&
        gpu_eval_omaha5_batch(backend->context, in->hole, in->board, in->count,
                              out->values) == 0) {
        out->count = in->count;
        return 0;
    }
    if (in->game == game_omaha6 && in->hole != NULL && in->board != NULL &&
        gpu_eval_omaha6_batch(backend->context, in->hole, in->board, in->count,
                              out->values) == 0) {
        out->count = in->count;
        return 0;
    }
    if (in->game == game_7stud && in->cards != NULL &&
        gpu_eval_stud_batch(backend->context, in->cards, in->count,
                            out->values) == 0) {
        out->count = in->count;
        return 0;
    }
    if (in->game == game_razz && in->cards != NULL &&
        gpu_eval_razz_batch(backend->context, in->cards, in->count,
                            out->values) == 0) {
        out->count = in->count;
        return 0;
    }
    return -1;
#endif
}

static int compute_opencl_vector_showdown(void *self,
                                          const pe_showdown_job_t *job,
                                          pe_value_vec_t *out)
{
    (void)self;
    (void)job;
    (void)out;
    return -1;
}

static int compute_opencl_sync(void *self)
{
    pe_compute_opencl_t *backend = (pe_compute_opencl_t *)self;
#if defined(PE_COMPUTE_OPENCL_AVAILABLE)
    if (!backend)
        return -1;
    return !backend->regret_context ||
                   pe_regret_opencl_sync(backend->regret_context) == 0 ? 0 : -1;
#else
    return backend == NULL ? -1 : 0;
#endif
}

const pe_compute_ops_t *pe_compute_opencl_ops(void)
{
    static const pe_compute_ops_t ops = {
        "opencl",
        compute_opencl_capabilities,
        compute_opencl_create,
        compute_opencl_destroy,
        compute_opencl_strategy_batch,
        compute_opencl_apply_update_batch,
        compute_opencl_terminal_eval_batch,
        compute_opencl_vector_showdown,
        compute_opencl_sync
    };
    return &ops;
}
