/*
 * eval_opencl.h: OpenCL implementation of GPU-accelerated poker evaluator
 */

#ifndef __EVAL_OPENCL_H__
#define __EVAL_OPENCL_H__

#include <poker_eval/gpu/eval_gpu.h>

/* OpenCL implementation of GPU interface functions */
gpu_eval_context_t* opencl_gpu_eval_init(int device_id, size_t max_batch_size, int backend_type);
void opencl_gpu_eval_cleanup(gpu_eval_context_t* ctx);
int opencl_gpu_eval_batch_boards(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_t* result
);
int opencl_gpu_monte_carlo_equity(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* ranges,
    int n_players,
    int n_simulations,
    float* equities
);
int opencl_gpu_get_device_info(int device_id, char* name, size_t* memory, int* compute_units);
int opencl_gpu_is_available(int backend_type);

/* Multi-game support functions (OpenCL) */
gpu_eval_context_t* opencl_gpu_eval_init_game(
    int device_id,
    size_t max_batch_size,
    int backend_type,
    gpu_game_config_t config
);

int opencl_gpu_eval_batch_boards_hilo(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_hilo_t* result
);

#endif /* __EVAL_OPENCL_H__ */
