/*
 * gpu_eval_example.c: Example of using GPU-accelerated poker evaluation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/gpu/eval_gpu.h>

/* Function prototypes */
static StdDeck_CardMask random_cards(int n_cards);
static void benchmark_evaluation(int n_boards, int n_players);
static void example_monte_carlo(void);

/* Function to create a random card mask */
static StdDeck_CardMask random_cards(int n_cards) {
    StdDeck_CardMask result, dead;
    StdDeck_CardMask_RESET(result);
    StdDeck_CardMask_RESET(dead);
    
    for (int i = 0; i < n_cards; i++) {
        int card;
        StdDeck_CardMask card_mask;
        
        do {
            card = rand() % 52;
            card_mask = StdDeck_MASK(card);
        } while (StdDeck_CardMask_ANY_SET(dead, card_mask));
        
        StdDeck_CardMask_OR(result, result, card_mask);
        StdDeck_CardMask_OR(dead, dead, card_mask);
    }
    
    return result;
}

/* Benchmark GPU vs CPU evaluation */
static void benchmark_evaluation(int n_boards, int n_players) {
    printf("Benchmarking %d boards with %d players each...\n", n_boards, n_players);
    
    /* Allocate memory for test data */
    StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));
    HandVal* cpu_results = malloc(n_boards * n_players * sizeof(HandVal));
    
    /* Generate random test data */
    printf("Generating test data...\n");
    for (int i = 0; i < n_boards; i++) {
        boards[i] = random_cards(5); /* 5 community cards */
        
        for (int j = 0; j < n_players; j++) {
            hole_cards[i * n_players + j] = random_cards(2); /* 2 hole cards */
        }
    }
    
    /* CPU evaluation */
    printf("CPU evaluation...\n");
    clock_t cpu_start = clock();
    
    for (int i = 0; i < n_boards; i++) {
        for (int j = 0; j < n_players; j++) {
            StdDeck_CardMask combined;
            StdDeck_CardMask_OR(combined, boards[i], hole_cards[i * n_players + j]);
            cpu_results[i * n_players + j] = StdDeck_StdRules_EVAL_N(combined, 7);
        }
    }
    
    clock_t cpu_end = clock();
    double cpu_time = ((double)(cpu_end - cpu_start)) / CLOCKS_PER_SEC;
    printf("CPU time: %.3f seconds (%.0f evals/sec)\n", 
           cpu_time, (n_boards * n_players) / cpu_time);
    
    /* GPU evaluation if available */
    int gpu_backend = -1;
    if (gpu_is_available(0)) { /* Check CUDA */
        gpu_backend = 0;
        printf("\nGPU evaluation (CUDA)...\n");
    } else if (gpu_is_available(1)) { /* Check OpenCL */
        gpu_backend = 1;
        printf("\nGPU evaluation (OpenCL)...\n");
    }
    
    if (gpu_backend >= 0) {
        /* Initialize GPU context */
        gpu_eval_context_t* gpu_ctx = gpu_eval_init(0, n_boards, gpu_backend);
        if (!gpu_ctx) {
            printf("Failed to initialize GPU context\n");
            goto cleanup;
        }
        
        /* Get device info */
        char device_name[256];
        size_t device_memory;
        int compute_units;
        gpu_get_device_info(0, device_name, &device_memory, &compute_units);
        printf("Using GPU: %s (%.1f GB, %d SMs)\n", 
               device_name, (double)device_memory / (1024.0 * 1024.0 * 1024.0), compute_units);
        
        /* Prepare result structure */
        gpu_eval_result_t gpu_result = {0};
        
        /* Time GPU evaluation */
        clock_t gpu_start = clock();
        
        int ret = gpu_eval_batch_boards(gpu_ctx, boards, hole_cards, 
                                        n_boards, n_players, &gpu_result);
        
        clock_t gpu_end = clock();
        
        if (ret == 0) {
            double gpu_time = ((double)(gpu_end - gpu_start)) / CLOCKS_PER_SEC;
            printf("GPU time: %.3f seconds (%.0f evals/sec)\n", 
                   gpu_time, (n_boards * n_players) / gpu_time);
            printf("Speedup: %.1fx\n", cpu_time / gpu_time);
            
            /* Verify results match */
            int mismatches = 0;
            for (int i = 0; i < n_boards * n_players; i++) {
                if (cpu_results[i] != gpu_result.hand_values[i]) {
                    mismatches++;
                }
            }
            
            if (mismatches == 0) {
                printf("Results verified: GPU and CPU evaluations match!\n");
            } else {
                printf("WARNING: %d mismatches between GPU and CPU results\n", mismatches);
            }
            
            free(gpu_result.hand_values);
        } else {
            printf("GPU evaluation failed\n");
        }
        
        gpu_eval_cleanup(gpu_ctx);
    } else {
        printf("\nGPU acceleration not available\n");
    }
    
cleanup:
    free(boards);
    free(hole_cards);
    free(cpu_results);
}

/* Example of Monte Carlo equity calculation */
static void example_monte_carlo(void) {
    printf("\n=== Monte Carlo Equity Calculation ===\n");
    
    int gpu_backend = -1;
    if (gpu_is_available(0)) {
        gpu_backend = 0;
    } else if (gpu_is_available(1)) {
        gpu_backend = 1;
    }
    
    if (gpu_backend < 0) {
        printf("GPU acceleration not available for Monte Carlo simulation\n");
        return;
    }
    
    /* Initialize GPU context */
    gpu_eval_context_t* gpu_ctx = gpu_eval_init(0, 100000, gpu_backend);
    if (!gpu_ctx) {
        printf("Failed to initialize GPU context for Monte Carlo\n");
        return;
    }
    
    /* Test different simulation sizes */
    int simulation_counts[] = {10000, 100000, 1000000};
    int n_tests = sizeof(simulation_counts) / sizeof(simulation_counts[0]);
    
    for (int t = 0; t < n_tests; t++) {
        int n_simulations = simulation_counts[t];
        int n_players = 2;
        float equities[2];
        
        printf("\nRunning %d Monte Carlo simulations with %d players...\n", 
               n_simulations, n_players);
        
        clock_t start = clock();
        
        int ret = gpu_monte_carlo_equity(gpu_ctx, NULL, n_players, n_simulations, equities);
        
        clock_t end = clock();
        double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
        
        if (ret == 0) {
            printf("Results:\n");
            for (int p = 0; p < n_players; p++) {
                printf("  Player %d equity: %.3f (%.1f%%)\n", 
                       p + 1, equities[p], equities[p] * 100.0);
            }
            printf("Time: %.3f seconds (%.0f sims/sec)\n", 
                   time_taken, n_simulations / time_taken);
            
            /* Estimate speedup vs CPU */
            double cpu_time_estimate = n_simulations * 0.000001; /* Rough estimate */
            printf("Estimated speedup vs CPU: %.1fx\n", cpu_time_estimate / time_taken);
        } else {
            printf("Monte Carlo simulation failed\n");
        }
    }
    
    gpu_eval_cleanup(gpu_ctx);
}

int main(int argc, char* argv[]) {
    /* Seed random number generator */
    srand((unsigned int)time(NULL));
    
    printf("GPU-Accelerated Poker Evaluator Example\n");
    printf("=======================================\n\n");
    
    /* Run benchmarks with different sizes */
    benchmark_evaluation(1000, 2);     /* 1K boards, 2 players */
    printf("\n");
    benchmark_evaluation(10000, 2);    /* 10K boards, 2 players */
    printf("\n");
    benchmark_evaluation(100000, 2);   /* 100K boards, 2 players */
    printf("\n");
    benchmark_evaluation(10000, 6);    /* 10K boards, 6 players */
    
    /* Monte Carlo example */
    example_monte_carlo();
    
    return 0;
}
