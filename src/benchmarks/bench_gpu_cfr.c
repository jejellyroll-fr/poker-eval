/*
 * bench_gpu_cfr.c - GPU vs CPU regret-matching primitive benchmark
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Compares the legacy GPU matrix regret-matching/average-strategy primitive
 * against CPU storage updates of the same shape. Both sides operate on
 * synthetic regret deltas without a game tree, so this measures update-kernel
 * throughput only; it does not benchmark a full CFR solve and makes no
 * speedup claim. Real solving goes through pe_solver_run() and its compute
 * ports (see docs/gpu/guides/GPU_ACCELERATION_GUIDE.md).
 */

#include <poker_eval/gpu/gpu_cfr.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

/* Timing utility */
static double get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* ===== CPU-CFR Benchmark (Simplified) ===== */

static double bench_cpu_cfr(int num_infosets, int max_actions, int iterations)
{
    printf("\n--- CPU-CFR Benchmark ---\n");
    printf("Infosets: %d, Max Actions: %d, Iterations: %d\n",
           num_infosets, max_actions, iterations);

    /* Create simple CFR storage */
    cfr_storage_t *storage = cfr_storage_create();
    if (!storage)
    {
        printf("Failed to create CPU storage\n");
        return -1.0;
    }

    /* Simulate CFR updates (simplified, no game tree) */
    double start = get_time_ms();

    for (int iter = 0; iter < iterations; iter++)
    {
        for (int i = 0; i < num_infosets; i++)
        {
            uint64_t infoset_key = (uint64_t)i;
            int n_actions = (i % max_actions) + 1;

            /* Get current strategy */
            double strategy[8];
            cfr_storage_get_strategy(storage, infoset_key, n_actions, strategy);

            /* Simulate regret deltas (placeholder) */
            double deltas[8] = {0};
            for (int a = 0; a < n_actions; a++)
            {
                deltas[a] = ((double)rand() / RAND_MAX) - 0.5;
            }

            /* Update regrets */
            cfr_storage_update_regret(storage, infoset_key, n_actions, deltas, 1.0);

            /* Update average strategy */
            cfr_storage_update_avg(storage, infoset_key, n_actions, strategy, 1.0);
        }

        if ((iter % 100 == 0) && iter > 0)
        {
            printf("  CPU Iteration %d/%d\n", iter, iterations);
        }
    }

    double elapsed = get_time_ms() - start;

    printf("CPU-CFR completed in %.2f ms\n", elapsed);
    printf("  Time per iteration: %.3f ms\n", elapsed / iterations);
    printf("  Throughput: %.0f infosets/sec\n",
           (num_infosets * iterations) / (elapsed / 1000.0));

    cfr_storage_destroy(storage);

    return elapsed;
}

/* ===== GPU-CFR Benchmark ===== */

static double bench_gpu_cfr(int num_infosets, int max_actions, int iterations)
{
    printf("\n--- GPU-CFR Benchmark ---\n");
    printf("Infosets: %d, Max Actions: %d, Iterations: %d\n",
           num_infosets, max_actions, iterations);

    /* Configure GPU-CFR */
    gpu_cfr_config_t cfg = gpu_cfr_default_config();
    cfg.num_infosets = num_infosets;
    cfg.max_actions = max_actions;
    cfg.max_iterations = iterations;
    cfg.regret_discount = 1.0;
    cfg.strategy_weight = 1.0;
    cfg.enable_profiling = true;
    cfg.verbose = false; /* Reduce verbosity for benchmark */

    /* Initialize GPU-CFR */
    gpu_cfr_context_t *ctx = NULL;

#ifdef ENABLE_CUDA
    if (gpu_cfr_cuda_init(&ctx, &cfg) != 0)
    {
        printf("Failed to initialize GPU-CFR (CUDA)\n");
        return -1.0;
    }
#else
    ctx = gpu_cfr_init(&cfg);
    if (!ctx)
    {
        printf("Failed to initialize GPU-CFR\n");
        return -1.0;
    }
#endif

    /* Run CFR iterations on GPU */
    double start = get_time_ms();

#ifdef ENABLE_CUDA
    int result = gpu_cfr_cuda_solve(ctx, iterations);
#else
    int result = gpu_cfr_solve(ctx, iterations);
#endif

    double elapsed = get_time_ms() - start;

    if (result != 0)
    {
        printf("GPU-CFR solve failed\n");
        gpu_cfr_free(ctx);
        return -1.0;
    }

    /* Get statistics */
    gpu_cfr_stats_t stats;
    gpu_cfr_get_stats(ctx, &stats);

    printf("GPU-CFR completed in %.2f ms\n", elapsed);
    printf("  Time per iteration: %.3f ms\n", elapsed / iterations);
    printf("  Throughput: %.0f infosets/sec\n",
           (num_infosets * iterations) / (elapsed / 1000.0));

    if (cfg.enable_profiling)
    {
        printf("  Kernel breakdown:\n");
        printf("    Strategy compute: %.2f ms\n", stats.strategy_compute_time_ms);
        printf("    Regret update: %.2f ms\n", stats.regret_update_time_ms);
    }

#ifdef ENABLE_CUDA
    gpu_cfr_cuda_free(ctx);
#else
    gpu_cfr_free(ctx);
#endif

    return elapsed;
}

/* ===== Main Benchmark Suite ===== */

int main(int argc, char **argv)
{
    printf("=======================================================\n");
    printf("   GPU-CFR vs CPU-CFR Performance Benchmark\n");
    printf("   Legacy matrix primitive benchmark (no speedup claim)\n");
    printf("=======================================================\n");

    /* Test configurations */
    struct
    {
        int num_infosets;
        int max_actions;
        int iterations;
    } configs[] = {
        {1000, 8, 100},    /* Small: 1K infosets */
        {10000, 8, 100},   /* Medium: 10K infosets */
        {100000, 8, 100},  /* Large: 100K infosets */
        {100000, 8, 1000}, /* Large with many iterations */
    };

    int num_configs = sizeof(configs) / sizeof(configs[0]);

    printf("\nBenchmark Configuration    CPU Time    Matrix Time    Ratio\n");
    printf("-----------------------------------------------------------\n");

    for (int i = 0; i < num_configs; i++)
    {
        int num_infosets = configs[i].num_infosets;
        int max_actions = configs[i].max_actions;
        int iterations = configs[i].iterations;

        /* Run CPU benchmark */
        double cpu_time = bench_cpu_cfr(num_infosets, max_actions, iterations);

        /* Run GPU benchmark */
        double gpu_time = bench_gpu_cfr(num_infosets, max_actions, iterations);

        /* Calculate speedup */
        if (cpu_time > 0 && gpu_time > 0)
        {
            double speedup = cpu_time / gpu_time;

            printf("%-26s  %-10.1f  %-12.1f  %-10.1fx\n",
                   "Config", cpu_time, gpu_time, speedup);
            printf("  %dK infosets, %d iter\n",
                   num_infosets / 1000, iterations);
        }
        else
        {
            printf("%-26s  FAILED\n", "Config");
        }

        printf("\n");
    }

    printf("=======================================================\n");
    printf("   Benchmark Complete\n");
    printf("=======================================================\n");

    return 0;
}
