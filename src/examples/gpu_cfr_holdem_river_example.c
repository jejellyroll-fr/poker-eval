/*
 * gpu_cfr_holdem_river_example.c - GPU-CFR on Hold'em river scenario
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Demonstrates GPU-accelerated CFR solving on a simple Hold'em river spot.
 */

#include <poker_eval/gpu/gpu_cfr.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Forward declarations for Hold'em river adapter */
extern cfr_game_t *holdem_river_game_create(const char *board, const char *hero_range);
extern void holdem_river_game_free(cfr_game_t *game);

/* Simple example: K-high board, HU river */
int main(int argc, char **argv)
{
    printf("=======================================================\n");
    printf("   GPU-CFR Hold'em River Example\n");
    printf("   Board: Kh9s4c2d7h (K-high, no flushes)\n");
    printf("=======================================================\n\n");

    /* Configuration */
    int num_iterations = 1000;
    int mc_samples_per_iter = 100;
    int max_actions = 8; /* fold, call, bet sizes */

    /* Create Hold'em river game */
    cfr_game_t *game = holdem_river_game_create("Kh9s4c2d7h", "AsAc-22");

    if (!game)
    {
        fprintf(stderr, "Failed to create Hold'em river game\n");
        fprintf(stderr, "Note: holdem_river_adapter may not be available in this build\n");
        fprintf(stderr, "Continuing with synthetic benchmark instead...\n\n");

        /* Fallback: synthetic benchmark (no game tree traversal) */
        goto synthetic_benchmark;
    }

    /* Count infosets in game */
    cfr_storage_t *temp_storage = cfr_storage_create();
    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 100;
    cfg.dcfr_alpha = 1.5;
    cfg.dcfr_beta = 0.0;
    cfg.dcfr_gamma = 2.0;

    printf("Pre-computing infoset count (running 100 CPU iterations)...\n");
    double warmup_exploit = 0.0;
    cfr_solve(game, temp_storage, &cfg, &warmup_exploit);

    size_t num_infosets = cfr_storage_count_infosets(temp_storage);
    printf("  Found %zu infosets in game tree\n\n", num_infosets);

    /* Convert CPU storage to GPU matrix format */
    printf("Converting CPU storage to GPU matrix format...\n");
    cfr_matrix_storage_t *matrix = cfr_convert_to_matrix(temp_storage, max_actions);

    if (!matrix)
    {
        fprintf(stderr, "Failed to convert storage to matrix\n");
        cfr_storage_destroy(temp_storage);
        holdem_river_game_free(game);
        return 1;
    }

    cfr_matrix_print_summary(matrix);
    printf("\n");

    /* Initialize GPU-CFR */
    printf("Initializing GPU-CFR...\n");

    gpu_cfr_config_t gpu_cfg = gpu_cfr_default_config();
    if (num_infosets > INT_MAX) {
        fprintf(stderr, "Too many infosets for GPU config\n");
        goto cleanup;
    }
    gpu_cfg.num_infosets = (int)num_infosets;
    gpu_cfg.max_actions = max_actions;
    gpu_cfg.max_iterations = num_iterations;
    gpu_cfg.regret_discount = 1.0;
    gpu_cfg.strategy_weight = 1.0;
    gpu_cfg.device_id = -1; /* Auto-select */
    gpu_cfg.enable_profiling = true;
    gpu_cfg.verbose = true;

#ifdef ENABLE_CUDA
    gpu_cfr_context_t *ctx = NULL;
    extern int gpu_cfr_cuda_init(gpu_cfr_context_t * *out_ctx, const gpu_cfr_config_t *config);

    if (gpu_cfr_cuda_init(&ctx, &gpu_cfg) != 0)
    {
        fprintf(stderr, "Failed to initialize GPU-CFR (CUDA not available?)\n");
        goto cleanup;
    }
#else
    gpu_cfr_context_t *ctx = gpu_cfr_init(&gpu_cfg);
    if (!ctx)
    {
        fprintf(stderr, "Failed to initialize GPU-CFR\n");
        goto cleanup;
    }
#endif

    /* Load initial state */
#ifdef ENABLE_CUDA
    extern int gpu_cfr_cuda_load_state(gpu_cfr_context_t * ctx,
                                       const cfr_matrix_storage_t *storage);
    gpu_cfr_cuda_load_state(ctx, matrix);
#else
    gpu_cfr_load_state(ctx, matrix);
#endif

    /* Solve with GPU-CFR + game tree traversal */
    printf("\nSolving with GPU-CFR (integrated with game tree)...\n");

    extern int gpu_cfr_solve_with_adapter(
        gpu_cfr_context_t * ctx,
        cfr_game_t * game,
        int iterations,
        int mc_samples_per_iter);

    if (gpu_cfr_solve_with_adapter(ctx, game, num_iterations, mc_samples_per_iter) != 0)
    {
        fprintf(stderr, "GPU-CFR solve failed\n");
        goto cleanup_ctx;
    }

    /* Get statistics */
    gpu_cfr_stats_t stats;

#ifdef ENABLE_CUDA
    extern int gpu_cfr_cuda_get_stats(const gpu_cfr_context_t *ctx, gpu_cfr_stats_t *stats);
    gpu_cfr_cuda_get_stats(ctx, &stats);
#else
    gpu_cfr_get_stats(ctx, &stats);
#endif

    printf("\n=======================================================\n");
    printf("   GPU-CFR Results\n");
    printf("=======================================================\n");
    printf("Iterations completed: %d\n", stats.iterations_completed);
    printf("Total time: %.2f ms\n", stats.total_time_ms);
    printf("Average time per iteration: %.3f ms\n", stats.avg_iteration_time_ms);

    if (stats.strategy_compute_time_ms > 0)
    {
        printf("\nKernel breakdown:\n");
        printf("  Strategy compute: %.2f ms\n", stats.strategy_compute_time_ms);
        printf("  Regret update: %.2f ms\n", stats.regret_update_time_ms);
    }

    /* Download final strategy */
#ifdef ENABLE_CUDA
    extern int gpu_cfr_cuda_download_state(gpu_cfr_context_t * ctx,
                                           cfr_matrix_storage_t * storage);
    gpu_cfr_cuda_download_state(ctx, matrix);
#else
    gpu_cfr_download_state(ctx, matrix);
#endif

    /* Convert back to CPU storage and evaluate */
    cfr_storage_t *final_storage = cfr_storage_create();
    cfr_convert_from_matrix(matrix, final_storage);

    printf("\nFinal strategy infoset count: %zu\n", cfr_storage_count_infosets(final_storage));

    /* Compute exploitability */
    double exploit = cfr_exploitability_proxy(game, final_storage, NULL);
    printf("Exploitability proxy: %.6f\n", exploit);

    cfr_storage_destroy(final_storage);

cleanup_ctx:
#ifdef ENABLE_CUDA
    extern void gpu_cfr_cuda_free(gpu_cfr_context_t * ctx);
    gpu_cfr_cuda_free(ctx);
#else
    gpu_cfr_free(ctx);
#endif

cleanup:
    cfr_matrix_storage_free(matrix);
    cfr_storage_destroy(temp_storage);
    holdem_river_game_free(game);

    printf("\n=======================================================\n");
    printf("   Example Complete\n");
    printf("=======================================================\n");

    return 0;

synthetic_benchmark:
    /* Synthetic benchmark (no game tree) - tests GPU kernels only */
    printf("=======================================================\n");
    printf("   Running Synthetic GPU-CFR Benchmark\n");
    printf("   (No game tree traversal)\n");
    printf("=======================================================\n\n");

    num_infosets = 10000;

    gpu_cfr_config_t synthetic_cfg = gpu_cfr_default_config();
    if (num_infosets > INT_MAX) {
        fprintf(stderr, "Too many infosets for synthetic config\n");
        return 1;
    }
    synthetic_cfg.num_infosets = (int)num_infosets;
    synthetic_cfg.max_actions = 8;
    synthetic_cfg.max_iterations = num_iterations;
    synthetic_cfg.enable_profiling = true;
    synthetic_cfg.verbose = true;

#ifdef ENABLE_CUDA
    gpu_cfr_context_t *synthetic_ctx = NULL;
    if (gpu_cfr_cuda_init(&synthetic_ctx, &synthetic_cfg) != 0)
    {
        fprintf(stderr, "Failed to initialize GPU-CFR\n");
        return 1;
    }

    extern int gpu_cfr_cuda_solve(gpu_cfr_context_t * ctx, int iterations);
    gpu_cfr_cuda_solve(synthetic_ctx, num_iterations);

    gpu_cfr_stats_t synthetic_stats;
    extern int gpu_cfr_cuda_get_stats(const gpu_cfr_context_t *ctx, gpu_cfr_stats_t *stats);
    gpu_cfr_cuda_get_stats(synthetic_ctx, &synthetic_stats);

    printf("\nSynthetic Benchmark Results:\n");
    printf("  Iterations: %d\n", synthetic_stats.iterations_completed);
    printf("  Total time: %.2f ms\n", synthetic_stats.total_time_ms);
    printf("  Throughput: %.1f M infoset-updates/sec\n",
           (synthetic_stats.iterations_completed * num_infosets) /
               (synthetic_stats.total_time_ms / 1000.0) / 1e6);

    extern void gpu_cfr_cuda_free(gpu_cfr_context_t * ctx);
    gpu_cfr_cuda_free(synthetic_ctx);
#else
    printf("GPU-CFR requires CUDA support (ENABLE_CUDA not defined)\n");
    return 1;
#endif

    return 0;
}
