/*
 * compute_opencl.c - OpenCL terminal-evaluation compute adapter (GPU-04)
 *
 * This adapter mirrors compute_cuda.c while remaining independent of OpenCL
 * headers unless the existing OpenCL batched-evaluator target is configured.
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/gpu/pe_regret_opencl.h>
#include "compute_gpu_updates.h"

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
    pe_gpu_update_pack_t pack;
    pe_regret_opencl_update_batch_t gpu_batch;

    if (backend == NULL || batch == NULL)
        return -1;
    if (batch->count == 0u)
        return 0;
#if !defined(PE_COMPUTE_OPENCL_AVAILABLE)
    (void)batch;
    return -1;
#else
    if (backend->regret_context == NULL ||
        pe_gpu_update_pack_build(&backend->config, batch, &pack) != 0)
        return -1;
    gpu_batch.count = pack.count;
    gpu_batch.slots = pack.slots;
    gpu_batch.regret_deltas = pack.regret_deltas;
    gpu_batch.average_deltas = pack.average_deltas;
    if (pe_regret_opencl_apply_update_slots(
            backend->regret_context, pack.regrets, pack.averages,
            pack.total_slots, &gpu_batch) != 0 ||
        pe_gpu_update_pack_commit(&pack) != 0)
    {
        pe_gpu_update_pack_destroy(&pack);
        return -1;
    }
    pe_gpu_update_pack_destroy(&pack);
    return 0;
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
