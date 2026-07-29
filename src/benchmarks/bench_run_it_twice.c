/*
 * bench_run_it_twice.c - Performance benchmarks for Run It Twice
 *
 * Measures:
 * - Throughput (calculations/sec) for different player counts
 * - Scaling with number of runouts (2-8)
 * - Hold'em vs Omaha performance
 * - Variance calculation overhead
 */

#include <poker_eval/equity/run_it_twice.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Platform-specific high-resolution timing */
#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <sys/time.h>
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
#endif

/* Helper to create random hole cards */
static void random_holes(int num_players, int cards_per_player,
                        StdDeck_CardMask holes[], StdDeck_CardMask *used) {
    StdDeck_CardMask_RESET(*used);

    for (int p = 0; p < num_players; p++) {
        StdDeck_CardMask_RESET(holes[p]);
        int dealt = 0;

        while (dealt < cards_per_player) {
            int card = rand() % StdDeck_N_CARDS;
            if (!StdDeck_CardMask_CARD_IS_SET(*used, card)) {
                StdDeck_CardMask_SET(holes[p], card);
                StdDeck_CardMask_SET(*used, card);
                dealt++;
            }
        }
    }
}

/* Helper to create random board */
static StdDeck_CardMask random_board(int num_cards, StdDeck_CardMask used) {
    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);

    int dealt = 0;
    while (dealt < num_cards) {
        int card = rand() % StdDeck_N_CARDS;
        if (!StdDeck_CardMask_CARD_IS_SET(used, card)) {
            StdDeck_CardMask_SET(board, card);
            StdDeck_CardMask_SET(used, card);
            dealt++;
        }
    }

    return board;
}

/* Benchmark: Hold'em RIT-2 heads-up throughput */
static void bench_holdem_headsup_rit2(int iterations) {
    printf("\n=== Hold'em Heads-Up RIT-2 Throughput ===\n");

    StdDeck_CardMask holes[2];
    rit_result_t result;

    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        StdDeck_CardMask used;
        random_holes(2, 2, holes, &used);
        StdDeck_CardMask flop = random_board(3, used);

        rit_holdem_equity(holes, 2, flop, 2, &result);
    }

    double elapsed = get_time_ms() - start;
    double calcs_per_sec = iterations / (elapsed / 1000.0);

    printf("Iterations: %d\n", iterations);
    printf("Time: %.2f ms\n", elapsed);
    printf("Throughput: %.0f calcs/sec\n", calcs_per_sec);
    printf("Avg per calc: %.3f ms\n", elapsed / iterations);
}

/* Benchmark: Scaling with number of players */
static void bench_player_scaling(int iterations) {
    printf("\n=== Player Count Scaling (Hold'em RIT-2) ===\n");
    printf("\nPlayers  Time(ms)  Calcs/sec  Ms/calc\n");
    printf("-------  --------  ---------  -------\n");

    for (int num_players = 2; num_players <= 6; num_players++) {
        StdDeck_CardMask holes[10];
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(num_players, 2, holes, &used);
            StdDeck_CardMask flop = random_board(3, used);

            rit_holdem_equity(holes, num_players, flop, 2, &result);
        }

        double elapsed = get_time_ms() - start;
        double calcs_per_sec = iterations / (elapsed / 1000.0);

        printf("  %d      %7.1f   %8.0f   %7.3f\n",
            num_players, elapsed, calcs_per_sec, elapsed / iterations);
    }
}

/* Benchmark: Scaling with number of runouts */
static void bench_runout_scaling(int iterations) {
    printf("\n=== Runout Count Scaling (Hold'em 2-way) ===\n");
    printf("\nRunouts  Time(ms)  Calcs/sec  Ms/calc  Variance\n");
    printf("-------  --------  ---------  -------  ---------\n");

    int runout_counts[] = {2, 3, 4, 6, 8};

    for (int i = 0; i < 5; i++) {
        int num_runouts = runout_counts[i];
        StdDeck_CardMask holes[2];
        rit_result_t result;

        double start = get_time_ms();
        double total_variance = 0.0;

        for (int iter = 0; iter < iterations; iter++) {
            StdDeck_CardMask used;
            random_holes(2, 2, holes, &used);
            StdDeck_CardMask flop = random_board(3, used);

            rit_holdem_equity(holes, 2, flop, num_runouts, &result);
            total_variance += result.equity_variance[0];
        }

        double elapsed = get_time_ms() - start;
        double calcs_per_sec = iterations / (elapsed / 1000.0);
        double avg_variance = total_variance / iterations;

        printf("  %d      %7.1f   %8.0f   %7.3f    %.5f\n",
            num_runouts, elapsed, calcs_per_sec,
            elapsed / iterations, avg_variance);
    }
}

/* Benchmark: Hold'em vs Omaha comparison */
static void bench_holdem_vs_omaha(int iterations) {
    printf("\n=== Hold'em vs Omaha Performance (RIT-2) ===\n");

    /* Hold'em benchmark */
    {
        StdDeck_CardMask holes[2];
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(2, 2, holes, &used);
            StdDeck_CardMask flop = random_board(3, used);

            rit_holdem_equity(holes, 2, flop, 2, &result);
        }

        double elapsed = get_time_ms() - start;
        printf("\nHold'em:\n");
        printf("  Time: %.2f ms\n", elapsed);
        printf("  Throughput: %.0f calcs/sec\n", iterations / (elapsed / 1000.0));
        printf("  Avg: %.3f ms/calc\n", elapsed / iterations);
    }

    /* Omaha benchmark */
    {
        StdDeck_CardMask holes[2];
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(2, 4, holes, &used);  /* 4 cards for Omaha */
            StdDeck_CardMask flop = random_board(3, used);

            rit_omaha_equity(holes, 2, flop, 2, &result);
        }

        double elapsed = get_time_ms() - start;
        printf("\nOmaha:\n");
        printf("  Time: %.2f ms\n", elapsed);
        printf("  Throughput: %.0f calcs/sec\n", iterations / (elapsed / 1000.0));
        printf("  Avg: %.3f ms/calc\n", elapsed / iterations);
    }
}

/* Benchmark: Variance calculation overhead */
static void bench_variance_overhead(int iterations) {
    printf("\n=== Variance Calculation Overhead ===\n");

    StdDeck_CardMask holes[2];
    rit_config_t config = rit_config_holdem_rit2();
    config.num_runouts = 4;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Without variance calculation */
    {
        config.calc_variance = false;
        config.track_individual = false;
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(2, 2, holes, &used);
            StdDeck_CardMask flop = random_board(3, used);

            rit_calculate_equity(holes, 2, flop, dead, &config, &result);
        }

        double elapsed = get_time_ms() - start;
        printf("\nWithout variance tracking:\n");
        printf("  Time: %.2f ms\n", elapsed);
        printf("  Throughput: %.0f calcs/sec\n", iterations / (elapsed / 1000.0));
    }

    /* With variance calculation */
    {
        config.calc_variance = true;
        config.track_individual = true;
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(2, 2, holes, &used);
            StdDeck_CardMask flop = random_board(3, used);

            rit_calculate_equity(holes, 2, flop, dead, &config, &result);
        }

        double elapsed = get_time_ms() - start;
        printf("\nWith variance tracking:\n");
        printf("  Time: %.2f ms\n", elapsed);
        printf("  Throughput: %.0f calcs/sec\n", iterations / (elapsed / 1000.0));
    }
}

/* Benchmark: Different board stages */
static void bench_stages(int iterations) {
    printf("\n=== Stage Performance (Hold'em RIT-2) ===\n");
    printf("\nStage         Cards   Time(ms)  Calcs/sec\n");
    printf("------------  ------  --------  ---------\n");

    /* Flop stage (deal 5 cards) */
    {
        StdDeck_CardMask holes[2];
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(2, 2, holes, &used);
            StdDeck_CardMask empty;
            StdDeck_CardMask_RESET(empty);

            rit_holdem_equity(holes, 2, empty, 2, &result);
        }

        double elapsed = get_time_ms() - start;
        printf("Flop          5       %7.1f   %8.0f\n",
            elapsed, iterations / (elapsed / 1000.0));
    }

    /* Turn stage (deal 2 cards) */
    {
        StdDeck_CardMask holes[2];
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(2, 2, holes, &used);
            StdDeck_CardMask flop = random_board(3, used);

            rit_holdem_equity(holes, 2, flop, 2, &result);
        }

        double elapsed = get_time_ms() - start;
        printf("Turn+River    2       %7.1f   %8.0f\n",
            elapsed, iterations / (elapsed / 1000.0));
    }

    /* River stage (deal 1 card) */
    {
        StdDeck_CardMask holes[2];
        rit_result_t result;

        double start = get_time_ms();

        for (int i = 0; i < iterations; i++) {
            StdDeck_CardMask used;
            random_holes(2, 2, holes, &used);
            StdDeck_CardMask board = random_board(4, used);

            rit_holdem_equity(holes, 2, board, 2, &result);
        }

        double elapsed = get_time_ms() - start;
        printf("River         1       %7.1f   %8.0f\n",
            elapsed, iterations / (elapsed / 1000.0));
    }
}

/* Benchmark: Memory usage estimation */
static void bench_memory_usage(void) {
    printf("\n=== Memory Usage Estimates ===\n\n");

    printf("Structure sizes:\n");
    printf("  rit_config_t:          %zu bytes\n", sizeof(rit_config_t));
    printf("  rit_result_t:          %zu bytes\n", sizeof(rit_result_t));
    printf("  rit_runout_result_t:   %zu bytes\n", sizeof(rit_runout_result_t));

    printf("\nPer-calculation memory (10 players, 8 runouts):\n");
    size_t config_size = sizeof(rit_config_t);
    size_t result_size = sizeof(rit_result_t);
    size_t total = config_size + result_size;

    printf("  Config:   %6zu bytes\n", config_size);
    printf("  Result:   %6zu bytes\n", result_size);
    printf("  Total:    %6zu bytes (%.2f KB)\n", total, (double)total / 1024.0);
}

/* Main benchmark runner */
int main(int argc, char *argv[]) {
    int iterations = 1000;

    /* Parse command line arguments */
    if (argc > 1) {
        iterations = atoi(argv[1]);
        if (iterations <= 0) iterations = 1000;
    }

    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║    Run It Twice Performance Benchmarks    ║\n");
    printf("╚════════════════════════════════════════════╝\n");
    printf("\nIterations per benchmark: %d\n", iterations);

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Run benchmarks */
    bench_holdem_headsup_rit2(iterations);
    bench_player_scaling(iterations);
    bench_runout_scaling(iterations);
    bench_holdem_vs_omaha(iterations);
    bench_variance_overhead(iterations);
    bench_stages(iterations);
    bench_memory_usage();

    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║          Benchmarks Complete               ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");

    return 0;
}
