/*
 * compute_cuda.c - CUDA terminal-evaluation compute adapter (GPU-03)
 *
 * The public port is always compiled. The GPU implementation is compiled and
 * linked only when the existing CUDA batched-evaluator target is present, so a
 * normal CPU/OpenCL-only build remains independent of CUDA headers and libs.
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include "compute_gpu_updates.h"

#include <stdlib.h>

#if defined(PE_COMPUTE_CUDA_AVAILABLE)
#include <poker_eval/gpu/eval_batched_gpu.h>
#include <poker_eval/gpu/pe_regret_cuda.h>
#endif

typedef struct
{
#if defined(PE_COMPUTE_CUDA_AVAILABLE)
    gpu_eval_context_t *context;
    pe_regret_cuda_context_t *regret_context;
#endif
    pe_compute_config_t config;
    size_t max_batch_size;
} pe_compute_cuda_t;

static uint64_t compute_cuda_capabilities(void *self)
{
    uint64_t caps = 0u;
    (void)self;
    if (pe_gpu_terminal_eval_gate_is_open())
        caps |= PE_CAP_GPU_TERMINAL_EVAL;
#if defined(PE_COMPUTE_CUDA_AVAILABLE)
    if (pe_gpu_regret_update_gate_is_open())
        caps |= PE_CAP_GPU_REGRET_UPDATE;
#endif
    return caps;
}

static int compute_cuda_create(void **self, const pe_compute_config_t *cfg)
{
    if (self == NULL || cfg == NULL || cfg->terminal_batch_size == 0u)
        return -1;
    *self = NULL;

#if !defined(PE_COMPUTE_CUDA_AVAILABLE)
    return -1;
#else
    pe_compute_cuda_t *backend;

    backend = (pe_compute_cuda_t *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return -1;
    backend->config = *cfg;
    {
        gpu_eval_config_t gpu_cfg = gpu_eval_default_config();
        gpu_cfg.preferred_backend = GPU_BACKEND_CUDA;
        gpu_cfg.max_batch_size = cfg->terminal_batch_size;
        backend->context = gpu_eval_init_batched(&gpu_cfg);
    }
    if (backend->context == NULL) {
        free(backend);
        return -1;
    }
    backend->regret_context = pe_regret_cuda_create();
    if (backend->regret_context == NULL) {
        gpu_eval_free(backend->context);
        free(backend);
        return -1;
    }
    backend->max_batch_size = cfg->terminal_batch_size;
    *self = backend;
    return 0;
#endif
}

static void compute_cuda_destroy(void *self)
{
    pe_compute_cuda_t *backend = (pe_compute_cuda_t *)self;

#if defined(PE_COMPUTE_CUDA_AVAILABLE)
    if (backend != NULL)
        pe_regret_cuda_destroy(backend->regret_context);
    if (backend != NULL)
        gpu_eval_free(backend->context);
#endif
    free(backend);
}

static int compute_cuda_strategy_batch(void *self, const pe_infoset_batch_t *in,
                                       pe_strategy_batch_t *out)
{
#if defined(PE_COMPUTE_CUDA_AVAILABLE)
    pe_compute_cuda_t *backend = (pe_compute_cuda_t *)self;
    return backend ? pe_regret_cuda_strategy_batch(backend->regret_context, in,
                                                   out) : -1;
#else
    (void)self;
    (void)in;
    (void)out;
#endif
    return -1;
}

static int compute_cuda_apply_update_batch(void *self,
                                           const pe_update_batch_t *batch)
{
#if defined(PE_COMPUTE_CUDA_AVAILABLE)
    pe_compute_cuda_t *backend = (pe_compute_cuda_t *)self;
    pe_gpu_update_pack_t pack;
    pe_regret_cuda_update_batch_t gpu_batch;

    if (backend == NULL || batch == NULL)
        return -1;
    if (batch->count == 0u && batch->soa.group_count == 0u)
        return 0;
    if (backend->regret_context == NULL ||
        pe_gpu_update_pack_build(&backend->config, batch, &pack) != 0)
        return -1;
    gpu_batch.count = pack.count;
    gpu_batch.slots = pack.slots;
    gpu_batch.regret_deltas = pack.regret_deltas;
    gpu_batch.average_deltas = pack.average_deltas;
    if (pe_regret_cuda_apply_update_slots(
            backend->regret_context, pack.regrets, pack.averages,
            pack.total_slots, &gpu_batch) != 0 ||
        pe_gpu_update_pack_commit(&pack) != 0)
    {
        pe_gpu_update_pack_destroy(&pack);
        return -1;
    }
    pe_gpu_update_pack_destroy(&pack);
    return 0;
#else
    (void)self;
    (void)batch;
#endif
    return -1;
}

static int compute_cuda_terminal_eval_batch(void *self,
                                            const pe_terminal_batch_t *in,
                                            pe_value_batch_t *out)
{
    pe_compute_cuda_t *backend = (pe_compute_cuda_t *)self;

    if (backend == NULL || in == NULL || out == NULL || in->count == 0u ||
        in->count > backend->max_batch_size || out->values == NULL ||
        out->capacity < in->count)
        return -1;

#if !defined(PE_COMPUTE_CUDA_AVAILABLE)
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

static int compute_cuda_vector_showdown(void *self,
                                        const pe_showdown_job_t *job,
                                        pe_value_vec_t *out)
{
    (void)self;
    (void)job;
    (void)out;
    return -1;
}

static int compute_cuda_sync(void *self)
{
    return self == NULL ? -1 : 0;
}

const pe_compute_ops_t *pe_compute_cuda_ops(void)
{
    static const pe_compute_ops_t ops = {
        "cuda",
        compute_cuda_capabilities,
        compute_cuda_create,
        compute_cuda_destroy,
        compute_cuda_strategy_batch,
        compute_cuda_apply_update_batch,
        compute_cuda_terminal_eval_batch,
        compute_cuda_vector_showdown,
        compute_cuda_sync
    };
    return &ops;
}
