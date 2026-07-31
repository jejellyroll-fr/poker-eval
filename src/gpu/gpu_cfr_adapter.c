/*
 * gpu_cfr_adapter.c - Game tree traversal adapter for GPU-CFR
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Connects GPU-CFR kernels with CPU-based game tree traversal.
 * Computes regret deltas via Monte Carlo sampling on CPU, then uploads to GPU.
 */

#include <poker_eval/gpu/gpu_cfr.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifdef ENABLE_CUDA
extern int gpu_cfr_cuda_download_state(gpu_cfr_context_t * ctx, cfr_matrix_storage_t * storage);
extern int gpu_cfr_cuda_solve(gpu_cfr_context_t * ctx, int iterations);
extern int gpu_cfr_cuda_upload_deltas(gpu_cfr_context_t *ctx, const float *deltas, int size);
#endif

/* Adapter context for game tree traversal */
struct gpu_cfr_adapter_t
{
    cfr_game_t *game;
    cfr_matrix_storage_t *matrix;

    /* Mapping from infoset key to matrix row index */
    struct
    {
        uint64_t *keys;
        int *indices;
        int count;
        int capacity;
    } key_map;

    /* Temporary buffers for regret deltas */
    float *regret_deltas; /* [num_infosets * max_actions] */
    int *visit_counts;    /* [num_infosets] */

    /* Sampling parameters */
    int mc_samples;
    unsigned int rng_seed;
};

/* ===== Key mapping utilities ===== */

static int find_or_create_index(gpu_cfr_adapter_t *adapter, uint64_t key)
{
    /* Linear search (could use hash table for large games) */
    for (int i = 0; i < adapter->key_map.count; i++)
    {
        if (adapter->key_map.keys[i] == key)
        {
            return adapter->key_map.indices[i];
        }
    }

    /* Not found - add new mapping */
    if (adapter->key_map.count >= adapter->key_map.capacity)
    {
        fprintf(stderr, "Key map overflow: %d infosets\n", adapter->key_map.count);
        return -1;
    }

    int new_index = adapter->key_map.count;
    adapter->key_map.keys[new_index] = key;
    adapter->key_map.indices[new_index] = new_index;
    adapter->key_map.count++;

    return new_index;
}

/* ===== Monte Carlo CFR traversal (CPU) ===== */

/* Recursive CFR traversal - computes counterfactual values */
static double cfr_traverse(
    gpu_cfr_adapter_t *adapter,
    uint64_t state_key,
    int player,
    double reach_prob_p0,
    double reach_prob_p1)
{
    cfr_game_t *game = adapter->game;

    /* Terminal node */
    if (game->is_terminal(game, state_key, game->game_data))
    {
        return game->get_utility(game, state_key, player, game->game_data);
    }

    /* Get available actions */
    int actions[16];
    int num_actions = game->get_actions(game, state_key, actions, 16, game->game_data);
    if (num_actions <= 0 || num_actions > 8)
        return 0.0;

    uint64_t infoset_key = state_key; // Assuming state_key is the infoset key
    int matrix_idx = find_or_create_index(adapter, infoset_key);

    if (matrix_idx < 0)
        return 0.0;

    /* Get current strategy from matrix */
    int base_idx = matrix_idx * adapter->matrix->max_actions;
    float *strategy = &adapter->matrix->curr_strategy[base_idx];

    /* Compute strategy from regrets (regret matching) */
    float sum_pos = 0.0f;
    for (int a = 0; a < num_actions; a++)
    {
        float r = adapter->matrix->regrets[base_idx + a];
        if (r > 0.0f)
            sum_pos += r;
    }

    if (sum_pos > 1e-9f)
    {
        for (int a = 0; a < num_actions; a++)
        {
            float r = adapter->matrix->regrets[base_idx + a];
            strategy[a] = (r > 0.0f) ? (r / sum_pos) : 0.0f;
        }
    }
    else
    {
        /* Uniform strategy */
        float uniform = 1.0f / (float)num_actions;
        for (int a = 0; a < num_actions; a++)
        {
            strategy[a] = uniform;
        }
    }

    /* Compute counterfactual values for each action */
    float action_values[8] = {0};
    float node_value = 0.0f;

    for (int i = 0; i < num_actions; ++i)
    {
        int action = actions[i];
        uint64_t next_state_key = game->apply_action(game, state_key, action, game->game_data);

        double new_reach_p0 = reach_prob_p0;
        double new_reach_p1 = reach_prob_p1;

        if (player == 0)
        {
            new_reach_p0 *= strategy[i];
        }
        else
        {
            new_reach_p1 *= strategy[i];
        }

        action_values[i] = (float)cfr_traverse(adapter, next_state_key, 1 - player,
                                               new_reach_p0, new_reach_p1);

        node_value += strategy[i] * action_values[i];
    }

    /* Update regrets (only for acting player) */
    double counterfactual_reach = (player == 0) ? reach_prob_p1 : reach_prob_p0;

    for (int i = 0; i < num_actions; i++)
    {
        float regret = (float)((action_values[i] - node_value) * counterfactual_reach);
        adapter->regret_deltas[base_idx + i] += regret;
    }

    adapter->visit_counts[matrix_idx]++;

    return node_value;
}

/* ===== Public API ===== */

gpu_cfr_adapter_t *gpu_cfr_adapter_create(
    cfr_game_t *game,
    cfr_matrix_storage_t *matrix,
    int mc_samples)
{
    if (!game || !matrix)
        return NULL;

    gpu_cfr_adapter_t *adapter =
        (gpu_cfr_adapter_t *)calloc(1, sizeof(gpu_cfr_adapter_t));
    if (!adapter)
        return NULL;

    adapter->game = game;
    adapter->matrix = matrix;
    adapter->mc_samples = mc_samples;
    adapter->rng_seed = 12345;

    /* Allocate key mapping */
    adapter->key_map.capacity = matrix->num_infosets;
    adapter->key_map.keys = (uint64_t *)calloc(matrix->num_infosets, sizeof(uint64_t));
    adapter->key_map.indices = (int *)calloc(matrix->num_infosets, sizeof(int));
    adapter->key_map.count = 0;

    if (!adapter->key_map.keys || !adapter->key_map.indices)
    {
        gpu_cfr_adapter_free(adapter);
        return NULL;
    }

    /* Allocate regret delta buffers */
    size_t matrix_size = matrix->num_infosets * matrix->max_actions;
    adapter->regret_deltas = (float *)calloc(matrix_size, sizeof(float));
    adapter->visit_counts = (int *)calloc(matrix->num_infosets, sizeof(int));

    if (!adapter->regret_deltas || !adapter->visit_counts)
    {
        gpu_cfr_adapter_free(adapter);
        return NULL;
    }

    return adapter;
}

void gpu_cfr_adapter_free(gpu_cfr_adapter_t *adapter)
{
    if (!adapter)
        return;

    free(adapter->key_map.keys);
    free(adapter->key_map.indices);
    free(adapter->regret_deltas);
    free(adapter->visit_counts);
    free(adapter);
}

int gpu_cfr_adapter_compute_deltas(gpu_cfr_adapter_t *adapter)
{
    if (!adapter)
        return -1;

    /* Clear delta buffers */
    size_t matrix_size = adapter->matrix->num_infosets * adapter->matrix->max_actions;
    memset(adapter->regret_deltas, 0, matrix_size * sizeof(float));
    memset(adapter->visit_counts, 0, adapter->matrix->num_infosets * sizeof(int));

    /* Run Monte Carlo samples */
    for (int sample = 0; sample < adapter->mc_samples; sample++)
    {
        /* Sample for player 0 */
        cfr_traverse(adapter, (uint64_t)(uintptr_t)adapter->game->initial_state, 0, 1.0, 1.0);

        /* Sample for player 1 */
        cfr_traverse(adapter, (uint64_t)(uintptr_t)adapter->game->initial_state, 1, 1.0, 1.0);
    }

    /* Average deltas by visit count */
    for (int i = 0; i < adapter->matrix->num_infosets; i++)
    {
        if (adapter->visit_counts[i] > 0)
        {
            int base_idx = i * adapter->matrix->max_actions;
            float scale = 1.0f / (float)adapter->visit_counts[i];

            for (int a = 0; a < adapter->matrix->max_actions; a++)
            {
                adapter->regret_deltas[base_idx + a] *= scale;
            }
        }
    }

    return 0;
}

const float *gpu_cfr_adapter_get_deltas(const gpu_cfr_adapter_t *adapter)
{
    return adapter ? adapter->regret_deltas : NULL;
}

int gpu_cfr_adapter_get_visited_infosets(const gpu_cfr_adapter_t *adapter)
{
    if (!adapter)
        return 0;

    int visited = 0;
    for (int i = 0; i < adapter->matrix->num_infosets; i++)
    {
        if (adapter->visit_counts[i] > 0)
            visited++;
    }

    return visited;
}

/* ===== Integration with GPU-CFR context ===== */

#ifdef ENABLE_CUDA

#endif

static int gpu_cfr_solve_with_adapter(

    gpu_cfr_context_t *ctx,

    cfr_game_t *game,

    int iterations,

    int mc_samples_per_iter)

{

    if (!ctx || !game)

        return -1;

    /* Download current state from GPU */

    cfr_matrix_storage_t *matrix = cfr_matrix_storage_create(

        /* TODO: get size from ctx config */

        10000, 8);

    if (!matrix)

        return -1;

#ifdef ENABLE_CUDA

    gpu_cfr_cuda_download_state(ctx, matrix);

#else

    gpu_cfr_download_state(ctx, matrix);

#endif

    /* Create adapter */

    gpu_cfr_adapter_t *adapter = gpu_cfr_adapter_create(game, matrix, mc_samples_per_iter);

    if (!adapter)

    {

        cfr_matrix_storage_free(matrix);

        return -1;
    }

    /* Main CFR loop */

    for (int iter = 0; iter < iterations; iter++)

    {

        /* 1. Compute regret deltas via game tree traversal (CPU) */

        gpu_cfr_adapter_compute_deltas(adapter);

        int visited = gpu_cfr_adapter_get_visited_infosets(adapter);

        if (iter % 100 == 0)

        {

            printf("  Iteration %d: %d infosets visited\n", iter, visited);
        }

        /* 2. Upload deltas to GPU and run one iteration */

#ifdef ENABLE_CUDA

        gpu_cfr_cuda_upload_deltas(ctx, adapter->regret_deltas,

                                   matrix->num_infosets * matrix->max_actions);

        gpu_cfr_cuda_solve(ctx, 1);

#else

        /* Fallback to CPU */

        gpu_cfr_solve(ctx, 1);

#endif

        /* 3. Download updated state for next iteration */

#ifdef ENABLE_CUDA

        gpu_cfr_cuda_download_state(ctx, matrix);

#else

        gpu_cfr_download_state(ctx, matrix);

#endif
    }

    /* Cleanup */

    gpu_cfr_adapter_free(adapter);

    cfr_matrix_storage_free(matrix);

    return 0;
}
