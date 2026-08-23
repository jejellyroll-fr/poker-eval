/*
 * evaluator_gpu.c - GPU evaluator port placeholder (GPU-02)
 *
 * GPU-03 and GPU-04 provide the concrete CUDA/OpenCL implementations. Keeping
 * this adapter present now makes the selection boundary explicit while the
 * no-GPU build remains fully linkable.
 */

#include <poker_eval/solver/pe_evaluator.h>

static uint64_t evaluator_gpu_capabilities(void *self)
{
    (void)self;
    return 0u;
}

static int evaluator_gpu_create(void **self)
{
    if (self != NULL)
        *self = NULL;
    return -1;
}

static void evaluator_gpu_destroy(void *self)
{
    (void)self;
}

static pe_evaluator_status_t evaluator_gpu_evaluate(
    void *self, const pe_evaluator_request_t *request,
    pe_evaluator_result_t *result)
{
    (void)self;
    (void)request;
    (void)result;
    return PE_EVALUATOR_ERR_UNSUPPORTED;
}

static int evaluator_gpu_sync(void *self)
{
    return self == NULL ? -1 : 0;
}

const pe_evaluator_ops_t *pe_evaluator_gpu_ops(void)
{
    static const pe_evaluator_ops_t ops = {
        "gpu",
        evaluator_gpu_capabilities,
        evaluator_gpu_create,
        evaluator_gpu_destroy,
        evaluator_gpu_evaluate,
        evaluator_gpu_sync
    };
    return &ops;
}
