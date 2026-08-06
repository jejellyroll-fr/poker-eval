/*
 * gpu_link_check.c - build-time link coverage for the GPU backends
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Every GPU target is a static archive, and ar stores objects without
 * resolving anything, so compiling the backend sources proves nothing about
 * whether their symbols exist: an undefined device symbol only surfaces when
 * something actually links them.
 *
 * This translation unit exists solely so CI can perform that link. Referencing
 * the batched entry points forces eval_batched_gpu.c into the link, and with it
 * every cuda_backend_* / opencl_backend_* implementation it dispatches to.
 * Built only when POKER_EVAL_GPU_LINK_CHECK is ON, and never installed.
 */

#include <poker_eval/gpu/eval_batched_gpu.h>
#include <stddef.h>

typedef int (*seven_card_batch_fn)(gpu_eval_context_t* ctx,
                                   const uint8_t* hands,
                                   size_t batch_size,
                                   uint32_t* out_values);

int main(void) {
    /* Taking the addresses is enough - running the kernels needs real
     * hardware, which the build machine does not have. */
    const seven_card_batch_fn referenced[] = {
        gpu_eval_stud_batch,
        gpu_eval_razz_batch,
    };

    return referenced[0] == NULL;
}
