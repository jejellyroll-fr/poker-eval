/*
 * compute_hip.c - HIP/ROCm compute adapter (GPU-02)
 *
 * The public port is always compiled. The device implementation is linked only
 * when a ROCm toolchain was found, so a build without one is unaffected.
 *
 * The regret kernels are literally the CUDA ones: both translation units
 * include common/pe_regret_kernels.inc and differ only in how the runtime is
 * spelled. Terminal evaluation has no HIP path yet -- the batched evaluator is
 * CUDA/OpenCL only -- so this adapter advertises the regret capability alone
 * and refuses terminal batches rather than pretending to serve them.
 */

#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include "compute_gpu_updates.h"

#include <stdlib.h>

#if defined(PE_COMPUTE_HIP_AVAILABLE)
#include <poker_eval/gpu/pe_regret_hip.h>
#endif

typedef struct
{
#if defined(PE_COMPUTE_HIP_AVAILABLE)
    pe_regret_hip_context_t *regret_context;
#endif
    pe_compute_config_t config;
    size_t max_batch_size;
} pe_compute_hip_t;

static uint64_t compute_hip_capabilities(void *self)
{
    uint64_t caps = 0u;
    (void)self;
#if defined(PE_COMPUTE_HIP_AVAILABLE)
    /* No HIP terminal evaluator exists yet, so PE_CAP_GPU_TERMINAL_EVAL is
       deliberately absent: the resolver must not route terminal batches here
       and then discover the refusal at run time. */
    if (pe_gpu_regret_update_gate_is_open())
        caps |= PE_CAP_GPU_REGRET_UPDATE;
#endif
    return caps;
}

static int compute_hip_create(void **self, const pe_compute_config_t *cfg)
{
    if (self == NULL || cfg == NULL)
        return -1;
    *self = NULL;

#if !defined(PE_COMPUTE_HIP_AVAILABLE)
    return -1;
#else
    pe_compute_hip_t *backend;

    backend = (pe_compute_hip_t *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return -1;
    backend->config = *cfg;
    backend->regret_context = pe_regret_hip_create();
    if (backend->regret_context == NULL) {
        free(backend);
        return -1;
    }
    backend->max_batch_size = cfg->terminal_batch_size;
    *self = backend;
    return 0;
#endif
}

static void compute_hip_destroy(void *self)
{
    pe_compute_hip_t *backend = (pe_compute_hip_t *)self;

#if defined(PE_COMPUTE_HIP_AVAILABLE)
    if (backend != NULL)
        pe_regret_hip_destroy(backend->regret_context);
#endif
    free(backend);
}

static int compute_hip_strategy_batch(void *self, const pe_infoset_batch_t *in,
                                       pe_strategy_batch_t *out)
{
#if defined(PE_COMPUTE_HIP_AVAILABLE)
    pe_compute_hip_t *backend = (pe_compute_hip_t *)self;
    return backend ? pe_regret_hip_strategy_batch(backend->regret_context, in,
                                                   out) : -1;
#else
    (void)self;
    (void)in;
    (void)out;
#endif
    return -1;
}

static int compute_hip_apply_update_batch(void *self,
                                           const pe_update_batch_t *batch)
{
#if defined(PE_COMPUTE_HIP_AVAILABLE)
    pe_compute_hip_t *backend = (pe_compute_hip_t *)self;
    pe_gpu_update_pack_t pack;
    pe_regret_hip_update_batch_t gpu_batch;

    if (backend == NULL || batch == NULL)
        return -1;
    if (batch->count == 0u)
        return 0;
    if (backend->regret_context == NULL ||
        pe_gpu_update_pack_build(&backend->config, batch, &pack) != 0)
        return -1;
    gpu_batch.count = pack.count;
    gpu_batch.slots = pack.slots;
    gpu_batch.regret_deltas = pack.regret_deltas;
    gpu_batch.average_deltas = pack.average_deltas;
    if (pe_regret_hip_apply_update_slots(
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

static int compute_hip_terminal_eval_batch(void *self,
                                           const pe_terminal_batch_t *in,
                                           pe_value_batch_t *out)
{
    /* Refused rather than silently served on the host: the capability bit
       above says this backend cannot do it, and the two must agree. */
    (void)self;
    (void)in;
    (void)out;
    return -1;
}

static int compute_hip_vector_showdown(void *self,
                                        const pe_showdown_job_t *job,
                                        pe_value_vec_t *out)
{
    (void)self;
    (void)job;
    (void)out;
    return -1;
}

static int compute_hip_sync(void *self)
{
    return self == NULL ? -1 : 0;
}

const pe_compute_ops_t *pe_compute_hip_ops(void)
{
    static const pe_compute_ops_t ops = {
        "hip",
        compute_hip_capabilities,
        compute_hip_create,
        compute_hip_destroy,
        compute_hip_strategy_batch,
        compute_hip_apply_update_batch,
        compute_hip_terminal_eval_batch,
        compute_hip_vector_showdown,
        compute_hip_sync
    };
    return &ops;
}
