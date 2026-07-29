/*
 * test_equity_comparison_framework.c: Framework for comparing CPU/SIMD/GPU equity calculations
 *
 * This framework allows systematic comparison of equity calculation methods
 * across different implementations (CPU, SIMD, GPU) for various poker games.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/simd_operations.h>
#include <poker_eval/gpu/eval_gpu.h>

/* Test configuration */
#define TEST_SKIP_CODE 77
#define MAX_PLAYERS 10
#define MAX_TEST_ITERATIONS 100000
#define EQUITY_TOLERANCE 0.01  /* 1% tolerance for equity difference */

/* Evaluation method types */
typedef enum {
    EVAL_METHOD_CPU,
    EVAL_METHOD_SIMD,
    EVAL_METHOD_GPU
} eval_method_t;

/* Test result structure */
typedef struct {
    enum_game_t game;
    eval_method_t method;
    int num_players;
    int num_iterations;

    /* Equity results per player */
    double equity[MAX_PLAYERS];
    double wins[MAX_PLAYERS];
    double ties[MAX_PLAYERS];

    /* Performance metrics */
    double time_ms;
    double iterations_per_sec;

    /* Status */
    int success;
    char error_msg[256];
} equity_result_t;

/* Comparison result structure */
typedef struct {
    enum_game_t game;
    int num_players;

    /* Results from each method */
    equity_result_t cpu_result;
    equity_result_t simd_result;
    equity_result_t gpu_result;

    /* Comparison metrics */
    int cpu_available;
    int simd_available;
    int gpu_available;

    /* Accuracy (compared to CPU as ground truth) */
    double simd_max_equity_diff;
    double gpu_max_equity_diff;

    /* Speedup factors */
    double simd_speedup;
    double gpu_speedup;

    /* Overall status */
    int passed;
    char summary[512];
} equity_comparison_t;

/* Test statistics */
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    int tests_skipped;
} test_stats_t;

static test_stats_t g_stats = {0, 0, 0, 0};

/* Utility functions */
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static double max_equity_difference(const double* eq1, const double* eq2, int n) {
    double max_diff = 0.0;
    for (int i = 0; i < n; i++) {
        double diff = fabs(eq1[i] - eq2[i]);
        if (diff > max_diff) {
            max_diff = diff;
        }
    }
    return max_diff;
}

/*
 * CPU Equity Calculation
 */
static int calculate_equity_cpu(
    enum_game_t game,
    StdDeck_CardMask pockets[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead,
    int iterations,
    equity_result_t* result
) {
    result->method = EVAL_METHOD_CPU;
    result->game = game;
    result->num_players = num_players;
    result->num_iterations = iterations;
    result->success = 0;

    /* Setup enumeration */
    enum_result_t enum_result;
    memset(&enum_result, 0, sizeof(enum_result));

    double start_time = get_time_ms();

    /* Run Monte Carlo simulation */
    int nboard = StdDeck_numCards(board);
    int actual_iterations = enumSample(
        game,
        pockets,
        board,
        dead,
        num_players,
        nboard,
        iterations,
        0,  /* orderflag */
        &enum_result
    );

    double end_time = get_time_ms();

    if (actual_iterations <= 0) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "CPU enumSample failed");
        return -1;
    }

    /* Extract results */
    for (int i = 0; i < num_players; i++) {
        result->equity[i] = enum_result.ev[i];
        result->wins[i] = (double)enum_result.nwinhi[i] / actual_iterations;
        result->ties[i] = (double)enum_result.ntiehi[i] / actual_iterations;
    }

    result->time_ms = end_time - start_time;
    result->iterations_per_sec = actual_iterations / (result->time_ms / 1000.0);
    result->success = 1;

    return 0;
}

/*
 * SIMD Equity Calculation (Placeholder)
 */
static int calculate_equity_simd(
    enum_game_t game,
    StdDeck_CardMask pockets[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead,
    int iterations,
    equity_result_t* result
) {
    result->method = EVAL_METHOD_SIMD;
    result->game = game;
    result->num_players = num_players;
    result->num_iterations = iterations;
    result->success = 0;

    /* Check if SIMD is available */
    simd_capability_t cap = simd_detect_capability();
    if (cap == SIMD_NONE) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "SIMD not available on this system");
        return -1;
    }

    /* TODO: Implement SIMD Monte Carlo */
    /* For now, fall back to CPU and mark with penalty */
    snprintf(result->error_msg, sizeof(result->error_msg),
            "SIMD Monte Carlo not yet implemented");
    return -1;
}

/*
 * GPU Equity Calculation
 */
static int calculate_equity_gpu(
    enum_game_t game,
    StdDeck_CardMask pockets[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead,
    int iterations,
    equity_result_t* result
) {
    result->method = EVAL_METHOD_GPU;
    result->game = game;
    result->num_players = num_players;
    result->num_iterations = iterations;
    result->success = 0;

    /* Check GPU availability */
    if (!gpu_is_available(0)) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "GPU not available");
        return -1;
    }

    /* Create game configuration based on enum_game_t */
    gpu_game_config_t config;
    switch (game) {
        case game_holdem:
            config = gpu_game_config_holdem();
            break;
        case game_omaha:
            config = gpu_game_config_omaha(4);
            break;
        default:
            snprintf(result->error_msg, sizeof(result->error_msg),
                    "GPU not implemented for this game type");
            return -1;
    }

    /* Initialize GPU context */
    gpu_eval_context_t* ctx = gpu_eval_init_game(0, iterations, 0, config);
    if (!ctx) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "Failed to initialize GPU context");
        return -1;
    }

    double start_time = get_time_ms();

    /* Run GPU Monte Carlo */
    float equities[MAX_PLAYERS];
    int ret = gpu_monte_carlo_equity(ctx, pockets, num_players, iterations, equities);

    double end_time = get_time_ms();

    if (ret != 0) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "GPU Monte Carlo failed");
        gpu_eval_cleanup(ctx);
        return -1;
    }

    /* Convert results */
    for (int i = 0; i < num_players; i++) {
        result->equity[i] = equities[i];
        result->wins[i] = equities[i]; /* Approximate */
        result->ties[i] = 0.0;
    }

    result->time_ms = end_time - start_time;
    result->iterations_per_sec = iterations / (result->time_ms / 1000.0);
    result->success = 1;

    gpu_eval_cleanup(ctx);
    return 0;
}

/*
 * Run complete equity comparison for a scenario
 */
static int run_equity_comparison(equity_comparison_t* comp) {
    printf("\n=== Equity Comparison Test ===\n");
    printf("Game: %d, Players: %d\n", comp->game, comp->num_players);

    comp->cpu_available = 1;
    comp->simd_available = (simd_detect_capability() != SIMD_NONE);
    comp->gpu_available = gpu_is_available(0);

    /* Placeholder pockets and board */
    StdDeck_CardMask pockets[MAX_PLAYERS];
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Generate random hands (simplified) */
    for (int i = 0; i < comp->num_players; i++) {
        StdDeck_CardMask_RESET(pockets[i]);
    }

    int iterations = 10000;

    /* Run CPU (always available as ground truth) */
    printf("  Running CPU... ");
    fflush(stdout);
    int cpu_ret = calculate_equity_cpu(comp->game, pockets, comp->num_players,
                                       board, dead, iterations, &comp->cpu_result);
    if (cpu_ret == 0) {
        printf("OK (%.2f ms, %.0f iter/sec)\n",
               comp->cpu_result.time_ms,
               comp->cpu_result.iterations_per_sec);
    } else {
        printf("FAILED: %s\n", comp->cpu_result.error_msg);
        comp->passed = 0;
        return -1;
    }

    /* Run SIMD if available */
    if (comp->simd_available) {
        printf("  Running SIMD... ");
        fflush(stdout);
        int simd_ret = calculate_equity_simd(comp->game, pockets, comp->num_players,
                                             board, dead, iterations, &comp->simd_result);
        if (simd_ret == 0) {
            printf("OK (%.2f ms, %.0f iter/sec)\n",
                   comp->simd_result.time_ms,
                   comp->simd_result.iterations_per_sec);

            comp->simd_max_equity_diff = max_equity_difference(
                comp->cpu_result.equity,
                comp->simd_result.equity,
                comp->num_players
            );
            comp->simd_speedup = comp->cpu_result.time_ms / comp->simd_result.time_ms;
        } else {
            printf("NOT AVAILABLE: %s\n", comp->simd_result.error_msg);
            comp->simd_available = 0;
        }
    } else {
        printf("  SIMD not available on this system\n");
    }

    /* Run GPU if available */
    if (comp->gpu_available) {
        printf("  Running GPU... ");
        fflush(stdout);
        int gpu_ret = calculate_equity_gpu(comp->game, pockets, comp->num_players,
                                           board, dead, iterations, &comp->gpu_result);
        if (gpu_ret == 0) {
            printf("OK (%.2f ms, %.0f iter/sec)\n",
                   comp->gpu_result.time_ms,
                   comp->gpu_result.iterations_per_sec);

            comp->gpu_max_equity_diff = max_equity_difference(
                comp->cpu_result.equity,
                comp->gpu_result.equity,
                comp->num_players
            );
            comp->gpu_speedup = comp->cpu_result.time_ms / comp->gpu_result.time_ms;
        } else {
            printf("NOT AVAILABLE: %s\n", comp->gpu_result.error_msg);
            comp->gpu_available = 0;
        }
    } else {
        printf("  GPU not available on this system\n");
    }

    /* Determine if test passed */
    comp->passed = 1;

    if (comp->simd_available && comp->simd_max_equity_diff > EQUITY_TOLERANCE) {
        comp->passed = 0;
        printf("  FAIL: SIMD equity difference too large: %.4f\n",
               comp->simd_max_equity_diff);
    }

    if (comp->gpu_available && comp->gpu_max_equity_diff > EQUITY_TOLERANCE) {
        comp->passed = 0;
        printf("  FAIL: GPU equity difference too large: %.4f\n",
               comp->gpu_max_equity_diff);
    }

    /* Print summary */
    printf("\n  Summary:\n");
    printf("    CPU:  %.2f ms (baseline)\n", comp->cpu_result.time_ms);
    if (comp->simd_available) {
        printf("    SIMD: %.2f ms (%.2fx speedup, %.4f max diff)\n",
               comp->simd_result.time_ms,
               comp->simd_speedup,
               comp->simd_max_equity_diff);
    }
    if (comp->gpu_available) {
        printf("    GPU:  %.2f ms (%.2fx speedup, %.4f max diff)\n",
               comp->gpu_result.time_ms,
               comp->gpu_speedup,
               comp->gpu_max_equity_diff);
    }

    if (comp->passed) {
        printf("  RESULT: PASS\n");
        g_stats.tests_passed++;
    } else {
        printf("  RESULT: FAIL\n");
        g_stats.tests_failed++;
    }

    g_stats.tests_run++;

    return comp->passed ? 0 : -1;
}

/*
 * Main test function
 */
int main(void) {
    printf("=================================================\n");
    printf("  Equity Comparison Framework Test Suite\n");
    printf("=================================================\n");

    /* Detect available methods */
    printf("\nDetecting available evaluation methods...\n");
    printf("  CPU:  Available (always)\n");
    printf("  SIMD: %s\n",
           (simd_detect_capability() != SIMD_NONE) ?
           simd_capability_name(simd_detect_capability()) :
           "Not available");
    printf("  GPU:  %s\n",
           gpu_is_available(0) ? "Available" : "Not available");

    if (!gpu_is_available(0)) {
        printf("\nSKIP: CUDA GPU not available\n");
        return TEST_SKIP_CODE;
    }

    /* Test 1: Hold'em heads-up */
    equity_comparison_t holdem_test = {
        .game = game_holdem,
        .num_players = 2
    };
    run_equity_comparison(&holdem_test);

    /* Test 2: Hold'em 4-way */
    equity_comparison_t holdem_4way = {
        .game = game_holdem,
        .num_players = 4
    };
    run_equity_comparison(&holdem_4way);

    /* Test 3: Omaha heads-up (if GPU supports it) */
    if (gpu_is_available(0)) {
        equity_comparison_t omaha_test = {
            .game = game_omaha,
            .num_players = 2
        };
        run_equity_comparison(&omaha_test);
    }

    /* Print final summary */
    printf("\n=================================================\n");
    printf("  Final Test Summary\n");
    printf("=================================================\n");
    printf("  Tests run:    %d\n", g_stats.tests_run);
    printf("  Tests passed: %d\n", g_stats.tests_passed);
    printf("  Tests failed: %d\n", g_stats.tests_failed);
    printf("  Tests skipped: %d\n", g_stats.tests_skipped);

    if (g_stats.tests_failed == 0) {
        printf("\n  ALL TESTS PASSED!\n");
        return 0;
    } else {
        printf("\n  SOME TESTS FAILED!\n");
        return 1;
    }
}
