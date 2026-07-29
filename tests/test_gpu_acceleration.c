/*
 * test_gpu_acceleration.c: Comprehensive tests for GPU acceleration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>

#ifdef HAVE_CUDA
#include <poker_eval/gpu/eval_gpu.h>
#endif

/* Test configuration */
#define TEST_SKIP_CODE 77
#define MAX_TEST_BOARDS 10000
#define MAX_PLAYERS 6
#define MONTE_CARLO_SIMS 100000

/* Test result structure */
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
} test_results_t;

/* Global test results */
test_results_t g_results = {0, 0, 0};

/* Test assertion macro */
#define TEST_ASSERT(condition, message) do { \
    g_results.tests_run++; \
    if (condition) { \
        g_results.tests_passed++; \
        printf("✓ %s\n", message); \
    } else { \
        g_results.tests_failed++; \
        printf("✗ %s\n", message); \
    } \
} while(0)

/* Generate random card mask with no duplicates from dead cards */
static StdDeck_CardMask random_cards(int n_cards, StdDeck_CardMask dead_cards) {
    StdDeck_CardMask result;
    StdDeck_CardMask_RESET(result);

    for (int i = 0; i < n_cards; i++) {
        int card;
        StdDeck_CardMask card_mask;

        do {
            card = rand() % 52;
            card_mask = StdDeck_MASK(card);
        } while (StdDeck_CardMask_ANY_SET(dead_cards, card_mask) ||
                 StdDeck_CardMask_ANY_SET(result, card_mask));

        StdDeck_CardMask_OR(result, result, card_mask);
        StdDeck_CardMask_OR(dead_cards, dead_cards, card_mask);
    }

    return result;
}

#ifdef HAVE_CUDA
/* Test GPU availability */
static void test_gpu_availability(void) {
    printf("\n=== Testing GPU Availability ===\n");
    
    int cuda_available = gpu_is_available(0);
    int opencl_available = gpu_is_available(1);
    
    TEST_ASSERT(cuda_available >= 0, "CUDA availability check returns valid result");
    TEST_ASSERT(opencl_available >= 0, "OpenCL availability check returns valid result");
    
    if (cuda_available) {
        printf("CUDA GPU acceleration is available\n");
        
        /* Test device information */
        char device_name[256];
        size_t device_memory;
        int compute_units;
        
        int ret = gpu_get_device_info(0, device_name, &device_memory, &compute_units);
        TEST_ASSERT(ret == 0, "CUDA device information retrieval");
        
        if (ret == 0) {
            printf("CUDA Device: %s\n", device_name);
            printf("Memory: %.1f GB\n", (double)device_memory / (1024.0 * 1024.0 * 1024.0));
            printf("Compute Units: %d\n", compute_units);
        }
    } else {
        printf("CUDA GPU acceleration is not available\n");
    }
    
    if (opencl_available) {
        printf("OpenCL GPU acceleration is available\n");
        
        /* Test device information */
        char device_name[256];
        size_t device_memory;
        int compute_units;
        
        int ret = gpu_get_device_info(0, device_name, &device_memory, &compute_units);
        TEST_ASSERT(ret == 0, "OpenCL device information retrieval");
        
        if (ret == 0) {
            printf("OpenCL Device: %s\n", device_name);
            printf("Memory: %.1f GB\n", (double)device_memory / (1024.0 * 1024.0 * 1024.0));
            printf("Compute Units: %d\n", compute_units);
        }
    } else {
        printf("OpenCL GPU acceleration is not available\n");
    }
}

/* Test GPU context initialization */
static void test_gpu_context(void) {
    printf("\n=== Testing GPU Context ===\n");
    
    int gpu_backend = -1;
    if (gpu_is_available(0)) {
        gpu_backend = 0;
    } else if (gpu_is_available(1)) {
        gpu_backend = 1;
    }
    
    if (gpu_backend < 0) {
        printf("Skipping GPU context tests (no GPU available)\n");
        return;
    }
    
    /* Test context creation */
    gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, gpu_backend);
    TEST_ASSERT(ctx != NULL, "GPU context initialization");
    
    if (ctx) {
        TEST_ASSERT(ctx->device_id == 0, "GPU context device ID");
        TEST_ASSERT(ctx->max_batch_size == 1000, "GPU context batch size");
        TEST_ASSERT(ctx->backend_type == gpu_backend, "GPU context backend type");
        
        /* Test cleanup */
        gpu_eval_cleanup(ctx);
        TEST_ASSERT(1, "GPU context cleanup");
    }
}

/* Test batch evaluation accuracy */
static void test_batch_evaluation_accuracy(void) {
    printf("\n=== Testing Batch Evaluation Accuracy ===\n");
    
    int gpu_backend = -1;
    if (gpu_is_available(0)) {
        gpu_backend = 0;
    } else if (gpu_is_available(1)) {
        gpu_backend = 1;
    }
    
    if (gpu_backend < 0) {
        printf("Skipping batch evaluation tests (no GPU available)\n");
        return;
    }
    
    gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, gpu_backend);
    if (!ctx) {
        printf("Failed to initialize GPU context\n");
        return;
    }
    
    /* Generate test data */
    int n_boards = 100;
    int n_players = 2;
    StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));
    HandVal* cpu_results = malloc(n_boards * n_players * sizeof(HandVal));
    
    /* Generate random hands without card overlap */
    for (int i = 0; i < n_boards; i++) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);

        /* Generate board cards */
        boards[i] = random_cards(5, dead);
        StdDeck_CardMask_OR(dead, dead, boards[i]);

        /* Generate hole cards for each player */
        for (int j = 0; j < n_players; j++) {
            hole_cards[i * n_players + j] = random_cards(2, dead);
            StdDeck_CardMask_OR(dead, dead, hole_cards[i * n_players + j]);
        }
    }
    
    /* CPU evaluation */
    for (int i = 0; i < n_boards; i++) {
        for (int j = 0; j < n_players; j++) {
            StdDeck_CardMask combined;
            StdDeck_CardMask_OR(combined, boards[i], hole_cards[i * n_players + j]);
            cpu_results[i * n_players + j] = StdDeck_StdRules_EVAL_N(combined, 7);
        }
    }
    
    /* GPU evaluation */
    gpu_eval_result_t gpu_result = {0};
    int ret = gpu_eval_batch_boards(ctx, boards, hole_cards, n_boards, n_players, &gpu_result);
    
    TEST_ASSERT(ret == 0, "GPU batch evaluation execution");
    
    if (ret == 0) {
        TEST_ASSERT(gpu_result.batch_size == n_boards * n_players, "GPU result batch size");
        TEST_ASSERT(gpu_result.hand_values != NULL, "GPU result hand values allocated");
        
        /* Compare results */
        int mismatches = 0;
        for (int i = 0; i < n_boards * n_players; i++) {
            if (cpu_results[i] != gpu_result.hand_values[i]) {
                if (mismatches < 5) {  /* Print first 5 mismatches for debugging */
                    printf("Mismatch %d: CPU=%u (0x%08x), GPU=%u (0x%08x)\n",
                           mismatches, cpu_results[i], cpu_results[i],
                           gpu_result.hand_values[i], gpu_result.hand_values[i]);
                }
                mismatches++;
            }
        }

        TEST_ASSERT(mismatches == 0, "GPU vs CPU evaluation accuracy (all results match)");

        if (mismatches > 0) {
            printf("Found %d mismatches out of %d evaluations\n", mismatches, n_boards * n_players);
        }
        
        free(gpu_result.hand_values);
    }
    
    /* Cleanup */
    free(boards);
    free(hole_cards);
    free(cpu_results);
    gpu_eval_cleanup(ctx);
}

/* Test batch evaluation performance */
static void test_batch_evaluation_performance(void) {
    printf("\n=== Testing Batch Evaluation Performance ===\n");
    
    int gpu_backend = -1;
    if (gpu_is_available(0)) {
        gpu_backend = 0;
    } else if (gpu_is_available(1)) {
        gpu_backend = 1;
    }
    
    if (gpu_backend < 0) {
        printf("Skipping performance tests (no GPU available)\n");
        return;
    }
    
    gpu_eval_context_t* ctx = gpu_eval_init(0, MAX_TEST_BOARDS, gpu_backend);
    if (!ctx) {
        printf("Failed to initialize GPU context\n");
        return;
    }
    
    int test_sizes[] = {100, 1000, 5000};
    int n_test_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    for (int t = 0; t < n_test_sizes; t++) {
        int n_boards = test_sizes[t];
        int n_players = 2;
        
        printf("\nTesting %d boards with %d players...\n", n_boards, n_players);
        
        /* Generate test data */
        StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
        StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));
        
        for (int i = 0; i < n_boards; i++) {
            StdDeck_CardMask dead;
            StdDeck_CardMask_RESET(dead);

            boards[i] = random_cards(5, dead);
            StdDeck_CardMask_OR(dead, dead, boards[i]);

            for (int j = 0; j < n_players; j++) {
                hole_cards[i * n_players + j] = random_cards(2, dead);
                StdDeck_CardMask_OR(dead, dead, hole_cards[i * n_players + j]);
            }
        }
        
        /* Time GPU evaluation */
        clock_t gpu_start = clock();
        gpu_eval_result_t gpu_result = {0};
        int ret = gpu_eval_batch_boards(ctx, boards, hole_cards, n_boards, n_players, &gpu_result);
        clock_t gpu_end = clock();
        
        double gpu_time = ((double)(gpu_end - gpu_start)) / CLOCKS_PER_SEC;
        
        TEST_ASSERT(ret == 0, "GPU batch evaluation performance test");
        
        if (ret == 0) {
            printf("GPU time: %.4f seconds (%.0f evals/sec)\n", 
                   gpu_time, (n_boards * n_players) / gpu_time);
            free(gpu_result.hand_values);
        }
        
        free(boards);
        free(hole_cards);
    }
    
    gpu_eval_cleanup(ctx);
}

/* Test Monte Carlo simulation */
static void test_monte_carlo_simulation(void) {
    printf("\n=== Testing Monte Carlo Simulation ===\n");
    
    int gpu_backend = -1;
    if (gpu_is_available(0)) {
        gpu_backend = 0;
    } else if (gpu_is_available(1)) {
        gpu_backend = 1;
    }
    
    if (gpu_backend < 0) {
        printf("Skipping Monte Carlo tests (no GPU available)\n");
        return;
    }
    
    gpu_eval_context_t* ctx = gpu_eval_init(0, MONTE_CARLO_SIMS, gpu_backend);
    if (!ctx) {
        printf("Failed to initialize GPU context\n");
        return;
    }
    
    int test_sims[] = {1000, 10000, 50000};
    int n_test_sims = sizeof(test_sims) / sizeof(test_sims[0]);
    
    for (int t = 0; t < n_test_sims; t++) {
        int n_simulations = test_sims[t];
        int n_players = 2;
        float equities[2];
        
        printf("\nTesting %d Monte Carlo simulations with %d players...\n", 
               n_simulations, n_players);
        
        clock_t start = clock();
        int ret = gpu_monte_carlo_equity(ctx, NULL, n_players, n_simulations, equities);
        clock_t end = clock();
        
        double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
        
        TEST_ASSERT(ret == 0, "Monte Carlo simulation execution");
        
        if (ret == 0) {
            /* Basic sanity checks */
            float total_equity = equities[0] + equities[1];
            TEST_ASSERT(total_equity >= 0.95 && total_equity <= 1.05, 
                       "Monte Carlo equity sum approximately 1.0");
            
            TEST_ASSERT(equities[0] >= 0.0 && equities[0] <= 1.0, 
                       "Player 1 equity in valid range");
            TEST_ASSERT(equities[1] >= 0.0 && equities[1] <= 1.0, 
                       "Player 2 equity in valid range");
            
            printf("Results: P1=%.3f, P2=%.3f, Total=%.3f\n", 
                   equities[0], equities[1], total_equity);
            printf("Time: %.4f seconds (%.0f sims/sec)\n", 
                   time_taken, n_simulations / time_taken);
        }
    }
    
    gpu_eval_cleanup(ctx);
}

/* Test error handling */
static void test_error_handling(void) {
    printf("\n=== Testing Error Handling ===\n");
    
    /* Test invalid device ID */
    gpu_eval_context_t* ctx = gpu_eval_init(999, 1000, 0);
    TEST_ASSERT(ctx == NULL, "Invalid device ID returns NULL");
    
    /* Test invalid backend type */
    ctx = gpu_eval_init(0, 1000, 999);
    TEST_ASSERT(ctx == NULL, "Invalid backend type returns NULL");
    
    /* Test NULL context operations */
    int ret = gpu_eval_batch_boards(NULL, NULL, NULL, 0, 0, NULL);
    TEST_ASSERT(ret != 0, "NULL context returns error");
    
    ret = gpu_monte_carlo_equity(NULL, NULL, 0, 0, NULL);
    TEST_ASSERT(ret != 0, "NULL context Monte Carlo returns error");
    
    /* Test cleanup with NULL */
    gpu_eval_cleanup(NULL);
    TEST_ASSERT(1, "NULL context cleanup doesn't crash");
}

#endif /* HAVE_CUDA */

/* Main test function */
int main(int argc, char* argv[]) {
    printf("GPU Acceleration Test Suite\n");
    printf("===========================\n");

#ifdef HAVE_CUDA
    int cuda_available = gpu_is_available(0);
    int opencl_available = gpu_is_available(1);
    if (!cuda_available && !opencl_available) {
        printf("SKIP: No GPU backend available\n");
        return TEST_SKIP_CODE;
    }
#else
    printf("SKIP: GPU acceleration not compiled in\n");
    return TEST_SKIP_CODE;
#endif

    /* Seed random number generator */
    srand((unsigned int)time(NULL));
    
#ifdef HAVE_CUDA
    test_gpu_availability();
    test_gpu_context();
    test_batch_evaluation_accuracy();
    test_batch_evaluation_performance();
    test_monte_carlo_simulation();
    test_error_handling();
#else
    printf("GPU acceleration not compiled in (HAVE_CUDA not defined)\n");
    printf("To enable GPU tests, build with CUDA support\n");
#endif
    
    /* Print test summary */
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", g_results.tests_run);
    printf("Tests passed: %d\n", g_results.tests_passed);
    printf("Tests failed: %d\n", g_results.tests_failed);
    
    if (g_results.tests_failed == 0) {
        printf("All tests passed! ✓\n");
        return 0;
    } else {
        printf("Some tests failed! ✗\n");
        return 1;
    }
}
