/*
 * gpu_combinatorics_demo.c - GPU Combinatorics Demonstration
 *
 * Demonstrates:
 * - Parallel prefix-sum (scan)
 * - Generating all C(52,5) poker hands on GPU
 * - C(7,5) board combinations for equity calculation
 * - Stream compaction for filtering
 *
 * Performance comparison vs CPU sequential generation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ENABLE_CUDA
#include <cuda_runtime.h>
#include "gpu_combinatorics.h"

/* ===== Helper Functions ===== */

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void print_combination(const int* comb, int k) {
    printf("[");
    for (int i = 0; i < k; i++) {
        printf("%d", comb[i]);
        if (i < k - 1) printf(",");
    }
    printf("]");
}

static void print_card(uint8_t card) {
    const char* ranks = "23456789TJQKA";
    const char* suits = "cdhs";
    printf("%c%c", ranks[card % 13], suits[card / 13]);
}

/* ===== Demonstration 1: Prefix Sum ===== */

static void demo_prefix_sum(void) {
    printf("\n===== Demo 1: Parallel Prefix Sum =====\n");

    const uint32_t n = 8;
    uint32_t h_input[8] = {3, 1, 7, 0, 4, 1, 6, 3};
    uint32_t h_output[8];

    printf("Input:  ");
    for (uint32_t i = 0; i < n; i++) printf("%u ", h_input[i]);
    printf("\n");

    // Allocate device memory
    uint32_t *d_input, *d_output;
    cudaMalloc(&d_input, n * sizeof(uint32_t));
    cudaMalloc(&d_output, n * sizeof(uint32_t));

    // Copy to device
    cudaMemcpy(d_input, h_input, n * sizeof(uint32_t), cudaMemcpyHostToDevice);

    // Run prefix sum
    double start = get_time_ms();
    gpu_prefix_sum_exclusive(d_input, d_output, n, 0);
    cudaDeviceSynchronize();
    double elapsed = get_time_ms() - start;

    // Copy result back
    cudaMemcpy(h_output, d_output, n * sizeof(uint32_t), cudaMemcpyDeviceToHost);

    printf("Output: ");
    for (uint32_t i = 0; i < n; i++) printf("%u ", h_output[i]);
    printf("\n");

    printf("Time: %.3f ms\n", elapsed);

    // Verify correctness
    uint32_t expected[8] = {0, 3, 4, 11, 11, 15, 16, 22};
    int correct = 1;
    for (uint32_t i = 0; i < n; i++) {
        if (h_output[i] != expected[i]) {
            correct = 0;
            break;
        }
    }
    printf("Verification: %s\n", correct ? "PASS" : "FAIL");

    cudaFree(d_input);
    cudaFree(d_output);
}

/* ===== Demonstration 2: Generate C(5,3) Combinations ===== */

static void demo_small_combinations(void) {
    printf("\n===== Demo 2: Generate C(5,3) = 10 Combinations =====\n");

    const int n = 5, k = 3;
    const uint64_t num_comb = 10;

    // Allocate device memory
    int* d_combinations;
    cudaMalloc(&d_combinations, num_comb * k * sizeof(int));

    // Generate combinations
    double start = get_time_ms();
    gpu_generate_all_combinations(n, k, d_combinations, 0);
    cudaDeviceSynchronize();
    double elapsed = get_time_ms() - start;

    // Copy result back
    int* h_combinations = (int*)malloc(num_comb * k * sizeof(int));
    cudaMemcpy(h_combinations, d_combinations, num_comb * k * sizeof(int),
               cudaMemcpyDeviceToHost);

    // Print all combinations
    for (uint64_t i = 0; i < num_comb; i++) {
        printf("  ");
        print_combination(&h_combinations[i * k], k);
        printf("\n");
    }

    printf("Time: %.3f ms\n", elapsed);
    printf("Throughput: %.0f combinations/sec\n", num_comb / (elapsed / 1000.0));

    free(h_combinations);
    cudaFree(d_combinations);
}

/* ===== Demonstration 3: Generate All C(52,5) Poker Hands ===== */

static void demo_all_poker_hands(void) {
    printf("\n===== Demo 3: Generate All C(52,5) = 2,598,960 Poker Hands =====\n");

    const uint64_t num_hands = 2598960;

    // Allocate device memory (13 MB)
    uint8_t* d_hands;
    cudaMalloc(&d_hands, num_hands * 5 * sizeof(uint8_t));

    // Generate all hands
    printf("Generating 2,598,960 poker hands on GPU...\n");
    double start = get_time_ms();
    gpu_generate_all_poker_hands(d_hands, 0);
    cudaDeviceSynchronize();
    double elapsed = get_time_ms() - start;

    // Copy first 10 hands back for verification
    uint8_t h_hands[50];
    cudaMemcpy(h_hands, d_hands, 50, cudaMemcpyDeviceToHost);

    printf("First 10 hands:\n");
    for (int i = 0; i < 10; i++) {
        printf("  Hand %d: ", i);
        for (int j = 0; j < 5; j++) {
            print_card(h_hands[i * 5 + j]);
            printf(" ");
        }
        printf("\n");
    }

    printf("\nTime: %.3f ms\n", elapsed);
    printf("Throughput: %.0f hands/sec\n", num_hands / (elapsed / 1000.0));
    printf("Speed: %.2f million hands/sec\n", num_hands / (elapsed * 1000.0));

    cudaFree(d_hands);
}

/* ===== Demonstration 4: Generate C(7,5) Board Combinations ===== */

static void demo_board_combinations(void) {
    printf("\n===== Demo 4: Generate C(7,5) Board Combinations =====\n");

    const uint64_t batch_size = 10000;
    const int boards_per_hand = 21;

    // Create random 7-card hands
    uint8_t* h_seven_cards = (uint8_t*)malloc(batch_size * 7);
    srand(42);
    for (uint64_t i = 0; i < batch_size * 7; i++) {
        h_seven_cards[i] = rand() % 52;
    }

    // Allocate device memory
    uint8_t *d_seven_cards, *d_five_boards;
    cudaMalloc(&d_seven_cards, batch_size * 7);
    cudaMalloc(&d_five_boards, batch_size * boards_per_hand * 5);

    // Copy input to device
    cudaMemcpy(d_seven_cards, h_seven_cards, batch_size * 7,
               cudaMemcpyHostToDevice);

    // Generate board combinations
    printf("Generating %lu boards (21 per hand × %lu hands)...\n",
           batch_size * boards_per_hand, batch_size);
    double start = get_time_ms();
    gpu_generate_board_combinations(d_seven_cards, d_five_boards, batch_size, 0);
    cudaDeviceSynchronize();
    double elapsed = get_time_ms() - start;

    // Copy first hand's boards back
    uint8_t h_boards[21 * 5];
    cudaMemcpy(h_boards, d_five_boards, 21 * 5, cudaMemcpyDeviceToHost);

    printf("\nFirst hand's 7 cards: ");
    for (int i = 0; i < 7; i++) {
        print_card(h_seven_cards[i]);
        printf(" ");
    }
    printf("\n");

    printf("First 5 of 21 boards:\n");
    for (int i = 0; i < 5; i++) {
        printf("  Board %d: ", i);
        for (int j = 0; j < 5; j++) {
            print_card(h_boards[i * 5 + j]);
            printf(" ");
        }
        printf("\n");
    }

    printf("\nTime: %.3f ms\n", elapsed);
    printf("Throughput: %.0f boards/sec\n",
           (batch_size * boards_per_hand) / (elapsed / 1000.0));
    printf("Speed: %.2f million boards/sec\n",
           (batch_size * boards_per_hand) / (elapsed * 1000.0));

    free(h_seven_cards);
    cudaFree(d_seven_cards);
    cudaFree(d_five_boards);
}

/* ===== Demonstration 5: CPU vs GPU Comparison ===== */

static void cpu_generate_combinations(int n, int k, int* out, uint64_t num_comb) {
    for (uint64_t idx = 0; idx < num_comb; idx++) {
        gpu_unrank_combination_host(idx, n, k, &out[idx * k]);
    }
}

static void demo_cpu_vs_gpu(void) {
    printf("\n===== Demo 5: CPU vs GPU Performance Comparison =====\n");

    const int n = 20, k = 5;
    const uint64_t num_comb = gpu_binomial_coeff_host(n, k);

    printf("Generating C(%d,%d) = %lu combinations\n", n, k, num_comb);

    // CPU version
    int* h_cpu_combinations = (int*)malloc(num_comb * k * sizeof(int));
    printf("\nCPU (sequential):\n");
    double start = get_time_ms();
    cpu_generate_combinations(n, k, h_cpu_combinations, num_comb);
    double cpu_time = get_time_ms() - start;
    printf("  Time: %.3f ms\n", cpu_time);
    printf("  Throughput: %.0f comb/sec\n", num_comb / (cpu_time / 1000.0));

    // GPU version
    int* d_gpu_combinations;
    cudaMalloc(&d_gpu_combinations, num_comb * k * sizeof(int));
    printf("\nGPU (parallel):\n");
    start = get_time_ms();
    gpu_generate_all_combinations(n, k, d_gpu_combinations, 0);
    cudaDeviceSynchronize();
    double gpu_time = get_time_ms() - start;
    printf("  Time: %.3f ms\n", gpu_time);
    printf("  Throughput: %.0f comb/sec\n", num_comb / (gpu_time / 1000.0));

    // Speedup
    printf("\nSpeedup: %.2fx\n", cpu_time / gpu_time);

    // Verify results match
    int* h_gpu_combinations = (int*)malloc(num_comb * k * sizeof(int));
    cudaMemcpy(h_gpu_combinations, d_gpu_combinations, num_comb * k * sizeof(int),
               cudaMemcpyDeviceToHost);

    int correct = 1;
    for (uint64_t i = 0; i < num_comb * k; i++) {
        if (h_cpu_combinations[i] != h_gpu_combinations[i]) {
            correct = 0;
            break;
        }
    }
    printf("Verification: %s\n", correct ? "PASS (CPU and GPU match)" : "FAIL");

    free(h_cpu_combinations);
    free(h_gpu_combinations);
    cudaFree(d_gpu_combinations);
}

/* ===== Main ===== */

int main(void) {
    printf("GPU Combinatorics Demonstration\n");
    printf("================================\n");

    // Check CUDA availability
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    if (err != cudaSuccess || device_count == 0) {
        fprintf(stderr, "No CUDA devices available\n");
        return 1;
    }

    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);
    printf("Using GPU: %s\n", prop.name);

    // Run demonstrations
    demo_prefix_sum();
    demo_small_combinations();
    demo_all_poker_hands();
    demo_board_combinations();
    demo_cpu_vs_gpu();

    printf("\n===== All Demonstrations Complete =====\n");
    return 0;
}

#else /* !ENABLE_CUDA */

int main(void) {
    fprintf(stderr, "This example requires CUDA support\n");
    fprintf(stderr, "Build with: cmake .. -DBUILD_GPU=ON\n");
    return 1;
}

#endif /* ENABLE_CUDA */
