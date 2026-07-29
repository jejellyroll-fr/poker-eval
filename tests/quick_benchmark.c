/* quick_benchmark.c -- Quick benchmark to measure micro-optimization impact
 *
 * This program runs a quick performance test focusing on the INNER_LOOP
 * where our micro-optimizations have the most impact.
 *
 * Copyright (C) 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>

/* Get current time in microseconds */
static double get_time_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

int main(int argc, char *argv[]) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    enum_result_t result;
    
    printf("=== Quick Micro-Optimization Benchmark ===\n\n");
    
    /* Setup: AA vs KK on turn */
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(board);
    
    /* Player 1: As Ah */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    
    /* Player 2: Ks Kh */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    /* Board: 9s 8s 7h 6c */
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_OR(dead, dead, board);
    
    /* Warm up */
    enumResultClear(&result);
    enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, 4, 0, &result);
    
    /* Benchmark 1: Standard holdem (tests division optimization) */
    printf("Test 1: AA vs KK on turn (44 river cards)\n");
    
    double start = get_time_usec();
    int iterations = 1000;
    
    for (int i = 0; i < iterations; i++) {
        enumResultClear(&result);
        enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, 4, 0, &result);
    }
    
    double end = get_time_usec();
    double elapsed = (end - start) / 1000000.0;
    
    printf("  Time: %.3f seconds for %d iterations\n", elapsed, iterations);
    printf("  Boards evaluated per iteration: %d\n", result.nsamples);
    printf("  Total boards/second: %.0f\n", (result.nsamples * iterations) / elapsed);
    printf("  Microseconds per board: %.2f\n", (elapsed * 1000000.0) / (result.nsamples * iterations));
    
    /* Benchmark 2: Hi/Lo game (tests division optimization with split pots) */
    printf("\nTest 2: Hi/Lo game (tests 0.5/n division optimization)\n");
    
    /* Board for Hi/Lo: A 2 3 (low possibility) */
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS));
    
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);
    
    start = get_time_usec();
    iterations = 100;
    
    for (int i = 0; i < iterations; i++) {
        enumResultClear(&result);
        enumExhaustive_dispatch(game_holdem8, pockets, board, dead, 2, 3, 0, &result);
    }
    
    end = get_time_usec();
    elapsed = (end - start) / 1000000.0;
    
    printf("  Time: %.3f seconds for %d iterations\n", elapsed, iterations);
    printf("  Boards evaluated per iteration: %d\n", result.nsamples);
    printf("  Total boards/second: %.0f\n", (result.nsamples * iterations) / elapsed);
    
    /* Summary of optimizations */
    printf("\n=== Micro-Optimizations Active ===\n");
    printf("1. Division lookup tables: Replacing 1/n and 0.5/n with table lookups\n");
    printf("2. Branch prediction hints: Using likely() for common cases\n");
    printf("3. Table alignment: Attempting 64-byte alignment (check with test_micro_optimizations)\n");
    
    printf("\nNote: To measure the full impact, compare with a version compiled without\n");
    printf("      the micro_optimizations.h header and its optimizations.\n");
    
    return 0;
}
