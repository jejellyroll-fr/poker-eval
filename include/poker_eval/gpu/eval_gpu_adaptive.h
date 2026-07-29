/*
 * eval_gpu_adaptive.h: Adaptive GPU/CPU evaluation interface
 */

#ifndef __EVAL_GPU_ADAPTIVE_H__
#define __EVAL_GPU_ADAPTIVE_H__

#include "eval_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Adaptive evaluation thresholds */
#define GPU_BATCH_THRESHOLD_MIN     1000    /* Minimum batch size for GPU */
#define GPU_MONTE_CARLO_THRESHOLD   10000   /* Minimum simulations for GPU */

/* Adaptive batch evaluation - automatically chooses CPU or GPU */
int adaptive_eval_batch_boards(
    gpu_eval_context_t* gpu_ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_t* result
);

/* Adaptive Monte Carlo - automatically chooses CPU or GPU */
int adaptive_monte_carlo_equity(
    gpu_eval_context_t* gpu_ctx,
    StdDeck_CardMask* ranges,
    int n_players,
    int n_simulations,
    float* equities
);

/* Get recommended batch size for optimal performance */
int get_optimal_batch_size(gpu_eval_context_t* ctx, int n_players);

/* Performance profiling structure */
typedef struct {
    double cpu_time_per_eval;
    double gpu_time_per_eval;
    double gpu_setup_overhead;
    int optimal_threshold;
} gpu_performance_profile_t;

/* Profile GPU vs CPU performance */
int profile_gpu_performance(gpu_eval_context_t* ctx, gpu_performance_profile_t* profile);

#ifdef __cplusplus
}
#endif

#endif /* __EVAL_GPU_ADAPTIVE_H__ */
