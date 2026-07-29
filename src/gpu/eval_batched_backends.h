/*
 * eval_batched_backends.h - CUDA backend entry points
 *
 * Declared here rather than as local externs in eval_batched_gpu.c so that
 * eval_batched_cuda.c, which defines them, sees the prototypes too. Without
 * that it trips -Wmissing-prototypes, and nothing checks the definitions
 * against the declarations the dispatcher actually calls.
 *
 * Declared unconditionally: eval_batched_cuda.c provides stub versions when
 * ENABLE_CUDA is off.
 */

#ifndef POKER_EVAL_GPU_EVAL_BATCHED_BACKENDS_H
#define POKER_EVAL_GPU_EVAL_BATCHED_BACKENDS_H

#include <poker_eval/gpu/eval_batched_gpu.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int cuda_backend_init(void** out_context, int device_id, bool verbose);
extern void cuda_backend_free(void* context);
extern int cuda_backend_get_device_info(void* context, gpu_device_info_t* info);
extern int cuda_backend_eval_holdem_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha5_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha6_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_stud_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_razz_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    int cards_per_hand,
    int game_type,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
);
extern int cuda_backend_eval_equity_holdem(
    void* context,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses,
    double* out_time_ms
);
extern void cuda_backend_enable_profiling(void* context, bool enable);
extern int cuda_backend_get_device_count(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GPU_EVAL_BATCHED_BACKENDS_H */
