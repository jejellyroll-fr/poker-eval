/*
 * bench_gpu_batched.c - GPU batched evaluation benchmark
 *
 * Tests GPU throughput for Hold'em 7-card evaluation.
 * Measures hands/second and compares to target (1-5 billion/min).
 */

#include <poker_eval/gpu/eval_batched_gpu.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Simple RNG for benchmark */
static uint64_t rng_state = 123456789ULL;

static uint64_t xorshift64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* Generate random card (0-51) */
static uint8_t random_card(void) {
    return (uint8_t)(xorshift64() % 52);
}

/* Generate random 7-card hand */
static void generate_random_hand(uint8_t cards[7]) {
    /* Simple generation - may have duplicates, but okay for benchmark */
    for (int i = 0; i < 7; i++) {
        cards[i] = random_card();
    }
}

/* Main benchmark */
int main(int argc, char** argv) {
    printf("=== GPU Batched Evaluation Benchmark ===\n\n");

    /* Parse command line */
    size_t batch_size = 100000; /* Default: 100K hands */
    int num_iterations = 10;    /* Default: 10 iterations */

    if (argc > 1) {
        batch_size = (size_t)atoi(argv[1]);
    }
    if (argc > 2) {
        num_iterations = atoi(argv[2]);
    }

    printf("Configuration:\n");
    printf("  Batch size: %zu hands\n", batch_size);
    printf("  Iterations: %d\n\n", num_iterations);

    /* Check GPU availability */
    if (!gpu_eval_is_available()) {
        fprintf(stderr, "ERROR: GPU evaluation not available\n");
        fprintf(stderr, "  - Make sure CUDA is installed\n");
        fprintf(stderr, "  - Build with -DENABLE_CUDA=ON\n");
        return 1;
    }

    printf("GPU available\n\n");

    /* Initialize GPU context */
    gpu_eval_config_t config = gpu_eval_default_config();
    config.enable_profiling = true;
    config.verbose = true;

    gpu_eval_context_t* gpu_ctx = gpu_eval_init(&config);
    if (!gpu_ctx) {
        fprintf(stderr, "ERROR: Failed to initialize GPU context\n");
        return 1;
    }

    printf("\n");

    /* Get device info */
    gpu_device_info_t info;
    if (gpu_eval_get_device_info(gpu_ctx, &info) == 0) {
        printf("Device Info:\n");
        printf("  Name: %s\n", info.device_name);
        printf("  Global memory: %.2f GB\n",
               info.global_mem_size / (1024.0 * 1024.0 * 1024.0));
        printf("  Max threads/block: %d\n", info.max_threads_per_block);
        printf("  Warp size: %d\n", info.warp_size);
        if (info.backend == GPU_BACKEND_CUDA) {
            printf("  Compute capability: %d.%d\n",
                   info.compute_capability_major,
                   info.compute_capability_minor);
        }
        printf("\n");
    }

    /* Allocate host memory */
    uint8_t* hands = (uint8_t*)malloc(batch_size * 7 * sizeof(uint8_t));
    uint32_t* values = (uint32_t*)malloc(batch_size * sizeof(uint32_t));

    if (!hands || !values) {
        fprintf(stderr, "ERROR: Failed to allocate host memory\n");
        gpu_eval_free(gpu_ctx);
        free(hands);
        free(values);
        return 1;
    }

    /* Generate random hands */
    printf("Generating %zu random hands...\n", batch_size);
    for (size_t i = 0; i < batch_size; i++) {
        generate_random_hand(&hands[i * 7]);
    }
    printf("Done\n\n");

    /* Warmup run */
    printf("Warmup run...\n");
    int result = gpu_eval_holdem_batch(gpu_ctx, hands, batch_size, values);
    if (result != 0) {
        fprintf(stderr, "ERROR: GPU evaluation failed (warmup)\n");
        gpu_eval_free(gpu_ctx);
        free(hands);
        free(values);
        return 1;
    }
    printf("Warmup complete\n\n");

    /* Reset stats */
    gpu_eval_reset_stats(gpu_ctx);

    /* Benchmark loop */
    printf("Running benchmark (%d iterations)...\n", num_iterations);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int iter = 0; iter < num_iterations; iter++) {
        result = gpu_eval_holdem_batch(gpu_ctx, hands, batch_size, values);
        if (result != 0) {
            fprintf(stderr, "ERROR: GPU evaluation failed (iteration %d)\n", iter);
            gpu_eval_free(gpu_ctx);
            free(hands);
            free(values);
            return 1;
        }

        if ((iter + 1) % 5 == 0) {
            printf("  Completed %d/%d iterations\n", iter + 1, num_iterations);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double wall_time_sec = (end.tv_sec - start.tv_sec) +
                           (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Done\n\n");

    /* Get stats */
    uint64_t total_evals;
    double gpu_time_ms, evals_per_sec;
    gpu_eval_get_stats(gpu_ctx, &total_evals, &gpu_time_ms, &evals_per_sec);

    /* Results */
    printf("=== Results ===\n\n");
    printf("Total hands evaluated: %llu\n", (unsigned long long)total_evals);
    printf("Wall time: %.3f seconds\n", wall_time_sec);
    printf("GPU time: %.3f ms\n", gpu_time_ms);
    printf("\n");

    double wall_throughput = (double)total_evals / wall_time_sec;
    printf("Throughput (wall time): %.2f M hands/sec\n", wall_throughput / 1e6);
    printf("Throughput (GPU time):  %.2f M hands/sec\n", evals_per_sec / 1e6);
    printf("\n");

    double hands_per_min = wall_throughput * 60.0;
    printf("Throughput: %.2f billion hands/minute\n", hands_per_min / 1e9);
    printf("\n");

    /* Compare to target */
    double target_min = 1.0e9;  /* 1 billion/min minimum */
    double target_max = 5.0e9;  /* 5 billion/min target */

    printf("Target range: %.1f - %.1f billion hands/minute\n",
           target_min / 1e9, target_max / 1e9);

    if (hands_per_min >= target_min) {
        printf("STATUS: ✓ Target achieved!\n");
        if (hands_per_min >= target_max) {
            printf("        ⭐ Excellent performance!\n");
        }
    } else {
        printf("STATUS: ✗ Below target (%.1f%% of minimum)\n",
               (hands_per_min / target_min) * 100.0);
    }

    printf("\n");

    /* Verify first few results */
    printf("Sample results (first 5 hands):\n");
    for (int i = 0; i < 5 && i < (int)batch_size; i++) {
        printf("  Hand %d: value = 0x%08x\n", i, values[i]);
    }

    /* Cleanup */
    gpu_eval_free(gpu_ctx);
    free(hands);
    free(values);

    printf("\n=== Benchmark Complete ===\n");

    return 0;
}
