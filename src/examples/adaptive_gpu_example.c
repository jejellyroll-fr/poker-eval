/*
 * adaptive_gpu_example.c: Example using adaptive GPU/CPU evaluation
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/gpu/eval_gpu.h>
#include <poker_eval/gpu/eval_gpu_adaptive.h>

/* Function prototypes */
static StdDeck_CardMask random_cards(int n_cards);
static void benchmark_adaptive(int n_boards, int n_players);
static void benchmark_adaptive_monte_carlo(int n_simulations, int n_players);
static void run_performance_profile(void);

/* Generate random card mask */
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

/* Benchmark adaptive evaluation */
static void benchmark_adaptive(int n_boards, int n_players) {
    printf("=== Adaptive Benchmark: %d boards, %d players ===\n", n_boards, n_players);
    
    /* Initialize GPU context */
    gpu_eval_context_t* gpu_ctx = NULL;
    if (gpu_is_available(0)) {
        gpu_ctx = gpu_eval_init(0, 100000, 0);
    } else if (gpu_is_available(1)) {
        gpu_ctx = gpu_eval_init(0, 100000, 1);
    }
    
    /* Generate test data */
    StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));
    
    for (int i = 0; i < n_boards; i++) {
        boards[i] = random_cards(5);
        for (int j = 0; j < n_players; j++) {
            hole_cards[i * n_players + j] = random_cards(2);
        }
    }
    
    /* Adaptive evaluation */
    clock_t start = clock();
    gpu_eval_result_t result = {0};
    int ret = adaptive_eval_batch_boards(gpu_ctx, boards, hole_cards, n_boards, n_players, &result);
    clock_t end = clock();
    
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    if (ret == 0) {
        int total_evals = n_boards * n_players;
        printf("Adaptive evaluation: %.4f seconds (%.0f evals/sec)\n", 
               time_taken, total_evals / time_taken);
        
        if (total_evals < GPU_BATCH_THRESHOLD_MIN) {
            printf("Used CPU (batch too small)\n");
        } else {
            printf("Used GPU (batch large enough)\n");
        }
        
        free(result.hand_values);
    } else {
        printf("Adaptive evaluation failed\n");
    }
    
    free(boards);
    free(hole_cards);
    
    if (gpu_ctx) {
        gpu_eval_cleanup(gpu_ctx);
    }
}

/* Benchmark adaptive Monte Carlo */
static void benchmark_adaptive_monte_carlo(int n_simulations, int n_players) {
    printf("\n=== Adaptive Monte Carlo: %d simulations, %d players ===\n", n_simulations, n_players);
    
    /* Initialize GPU context */
    gpu_eval_context_t* gpu_ctx = NULL;
    if (gpu_is_available(0)) {
        gpu_ctx = gpu_eval_init(0, 100000, 0);
    } else if (gpu_is_available(1)) {
        gpu_ctx = gpu_eval_init(0, 100000, 1);
    }
    
    float equities[10];
    
    clock_t start = clock();
    int ret = adaptive_monte_carlo_equity(gpu_ctx, NULL, n_players, n_simulations, equities);
    clock_t end = clock();
    
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    if (ret == 0) {
        printf("Adaptive Monte Carlo: %.4f seconds (%.0f sims/sec)\n", 
               time_taken, n_simulations / time_taken);
        
        if (n_simulations < GPU_MONTE_CARLO_THRESHOLD) {
            printf("Used CPU (simulations too few)\n");
        } else {
            printf("Used GPU (simulations sufficient)\n");
        }
        
        printf("Equities: ");
        for (int p = 0; p < n_players; p++) {
            printf("P%d=%.3f ", p+1, equities[p]);
        }
        printf("\n");
    } else {
        printf("Adaptive Monte Carlo failed\n");
    }
    
    if (gpu_ctx) {
        gpu_eval_cleanup(gpu_ctx);
    }
}

/* Performance profiling */
static void run_performance_profile(void) {
    printf("\n=== Performance Profiling ===\n");
    
    gpu_eval_context_t* gpu_ctx = NULL;
    if (gpu_is_available(0)) {
        gpu_ctx = gpu_eval_init(0, 100000, 0);
        printf("Using CUDA backend\n");
    } else if (gpu_is_available(1)) {
        gpu_ctx = gpu_eval_init(0, 100000, 1);
        printf("Using OpenCL backend\n");
    }
    
    if (gpu_ctx) {
        gpu_performance_profile_t profile;
        if (profile_gpu_performance(gpu_ctx, &profile) == 0) {
            printf("\nRecommendations:\n");
            printf("- Use GPU for batches ≥ %d evaluations\n", profile.optimal_threshold);
            printf("- Use CPU for smaller batches to avoid overhead\n");
            
            int optimal_batch = get_optimal_batch_size(gpu_ctx, 2);
            printf("- Optimal batch size for 2 players: %d\n", optimal_batch);
        }
        
        gpu_eval_cleanup(gpu_ctx);
    } else {
        printf("No GPU available - will use CPU for all operations\n");
    }
}

int main(void) {
    srand((unsigned int)time(NULL));
    
    printf("Adaptive GPU/CPU Poker Evaluation Example\n");
    printf("==========================================\n\n");
    
    /* Test different batch sizes */
    benchmark_adaptive(100, 2);      /* Small batch - should use CPU */
    benchmark_adaptive(2000, 2);     /* Large batch - should use GPU */
    benchmark_adaptive(10000, 6);    /* Very large batch - should use GPU */
    
    /* Test different Monte Carlo sizes */
    benchmark_adaptive_monte_carlo(1000, 2);     /* Small - should use CPU */
    benchmark_adaptive_monte_carlo(50000, 2);    /* Large - should use GPU */
    
    /* Performance profiling */
    run_performance_profile();
    
    return 0;
}
