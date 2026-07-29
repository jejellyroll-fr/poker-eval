/*
 * batched_montecarlo_example.c - Example and benchmark for batched Monte-Carlo
 *
 * This program demonstrates the performance improvement of batched Monte-Carlo
 * sampling compared to the traditional approach.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/batched_montecarlo.h>

/* Get current time in microseconds */
static long long get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

/* Format time in a human-readable way */
static void format_time(long long us, char *buf) {
    if (us < 1000) {
        snprintf(buf, 64, "%lld µs", us);
    } else if (us < 1000000) {
        snprintf(buf, 64, "%.2f ms", (double)us / 1000.0);
    } else {
        snprintf(buf, 64, "%.2f s", (double)us / 1000000.0);
    }
}

/* Run a benchmark comparison */
static void benchmark_comparison(enum_game_t game, const char *game_name,
                               StdDeck_CardMask pockets[], int npockets,
                               StdDeck_CardMask board, StdDeck_CardMask dead,
                               int nboard, int iterations) {
    enum_result_t result_regular, result_batched;
    long long start_time, end_time;
    long long time_regular, time_batched;
    char time_buf[64];
    int err;
    
    printf("\n=== Benchmarking %s with %d iterations ===\n", game_name, iterations);
    printf("Players: %d, Board cards: %d\n", npockets, nboard);
    
    /* Warm up the CPU */
    printf("Warming up...\n");
    enumSample(game, pockets, board, dead, npockets, nboard, 1000, 0, &result_regular);
    enumSampleBatched(game, pockets, board, dead, npockets, nboard, 1000, 0, &result_batched);
    
    /* Benchmark regular enumSample */
    printf("\nRunning regular enumSample...\n");
    start_time = get_time_us();
    err = enumSample(game, pockets, board, dead, npockets, nboard, iterations, 0, &result_regular);
    end_time = get_time_us();
    time_regular = end_time - start_time;
    
    if (err) {
        printf("Error in regular enumSample: %d\n", err);
        return;
    }
    
    format_time(time_regular, time_buf);
    printf("Regular enumSample: %s\n", time_buf);
    printf("  Samples/second: %.0f\n", (double)iterations * 1000000.0 / (double)time_regular);
    
    /* Benchmark batched enumSample */
    printf("\nRunning batched enumSample...\n");
    start_time = get_time_us();
    err = enumSampleBatched(game, pockets, board, dead, npockets, nboard, iterations, 0, &result_batched);
    end_time = get_time_us();
    time_batched = end_time - start_time;
    
    if (err) {
        printf("Error in batched enumSample: %d\n", err);
        return;
    }
    
    format_time(time_batched, time_buf);
    printf("Batched enumSample: %s\n", time_buf);
    printf("  Samples/second: %.0f\n", (double)iterations * 1000000.0 / (double)time_batched);
    
    /* Calculate speedup */
    double speedup = (double)time_regular / (double)time_batched;
    printf("\nSpeedup: %.2fx\n", speedup);
    printf("Time saved: %.0f%%\n", (1.0 - 1.0/speedup) * 100);
    
    /* Verify results match */
    printf("\nVerifying results match...\n");
    int results_match = 1;
    for (int i = 0; i < npockets; i++) {
        double ev_regular = result_regular.ev[i] / result_regular.nsamples;
        double ev_batched = result_batched.ev[i] / result_batched.nsamples;
        double diff = fabs(ev_regular - ev_batched);
        
        if (diff > 0.001) {  /* Allow small differences due to randomness */
            printf("  Player %d: EV mismatch (regular: %.4f, batched: %.4f)\n",
                   i, ev_regular, ev_batched);
            results_match = 0;
        }
    }
    
    if (results_match) {
        printf("  ✓ Results match within tolerance\n");
    }
}

int main(int argc, char *argv[]) {
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    int iterations = 100000;
    
    /* Parse command line arguments */
    if (argc > 1) {
        iterations = atoi(argv[1]);
        if (iterations <= 0) {
            printf("Usage: %s [iterations]\n", argv[0]);
            printf("Default iterations: 100000\n");
            return 1;
        }
    }
    
    /* Initialize random seed */
    srand((unsigned int)time(NULL));
    
    printf("Batched Monte-Carlo Benchmark\n");
    printf("=============================\n");
    printf("Batch size: %d\n", BATCH_SIZE);
    
    /* Test 1: Hold'em heads-up, no board */
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(board);
    
    /* Player 1: As Ah */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    
    /* Player 2: Ks Kh */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    benchmark_comparison(game_holdem, "Hold'em Heads-up (AA vs KK)", 
                        pockets, 2, board, dead, 0, iterations);
    
    /* Test 2: Hold'em 6-way, flop dealt */
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(board);
    
    /* Set up 6 random hands */
    for (int i = 0; i < 6; i++) {
        StdDeck_CardMask_RESET(pockets[i]);
        int c1, c2;
        do {
            c1 = rand() % StdDeck_N_CARDS;
        } while (StdDeck_CardMask_CARD_IS_SET(dead, c1));
        StdDeck_CardMask_SET(pockets[i], c1);
        StdDeck_CardMask_SET(dead, c1);
        
        do {
            c2 = rand() % StdDeck_N_CARDS;
        } while (StdDeck_CardMask_CARD_IS_SET(dead, c2));
        StdDeck_CardMask_SET(pockets[i], c2);
        StdDeck_CardMask_SET(dead, c2);
    }
    
    /* Deal a flop */
    for (int i = 0; i < 3; i++) {
        int c;
        do {
            c = rand() % StdDeck_N_CARDS;
        } while (StdDeck_CardMask_CARD_IS_SET(dead, c));
        StdDeck_CardMask_SET(board, c);
        StdDeck_CardMask_SET(dead, c);
    }
    
    benchmark_comparison(game_holdem, "Hold'em 6-way (flop dealt)",
                        pockets, 6, board, dead, 3, iterations / 10);
    
    /* Test 3: Omaha heads-up */
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(board);
    
    /* Deal 4 cards to each player */
    for (int i = 0; i < 2; i++) {
        StdDeck_CardMask_RESET(pockets[i]);
        for (int j = 0; j < 4; j++) {
            int c;
            do {
                c = rand() % StdDeck_N_CARDS;
            } while (StdDeck_CardMask_CARD_IS_SET(dead, c));
            StdDeck_CardMask_SET(pockets[i], c);
            StdDeck_CardMask_SET(dead, c);
        }
    }
    
    benchmark_comparison(game_omaha, "Omaha Heads-up",
                        pockets, 2, board, dead, 0, iterations / 5);
    
    printf("\n=== Benchmark Complete ===\n");
    
    return 0;
}
