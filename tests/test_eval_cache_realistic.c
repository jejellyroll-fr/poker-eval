/* test_eval_cache_realistic.c - Realistic test showing cache effectiveness */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval_cache.h>
#include <poker_eval/core/eval.h>

static double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void)
{
    printf("=== REALISTIC CACHE TEST ===\n\n");

    /* Scenario: Evaluating many boards with fixed pocket cards (common in simulations) */
    StdDeck_CardMask pocket1, pocket2, hand1, hand2;
    HandVal val1, val2;
    (void)val1; /* Suppress unused variable warning */
    (void)val2; /* Suppress unused variable warning */

    /* Fixed pockets: AA vs KK */
    StdDeck_CardMask_RESET(pocket1);
    StdDeck_CardMask_SET(pocket1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask_RESET(pocket2);
    StdDeck_CardMask_SET(pocket2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));

    /* Generate 1000 different boards */
    const int n_boards = 1000;
    const int n_iterations = 1000; /* Simulate each board 1000 times */
    StdDeck_CardMask *boards = malloc(n_boards * sizeof(StdDeck_CardMask));

    /* Generate boards (avoiding conflicts with pockets) */
    for (int i = 0; i < n_boards; i++)
    {
        StdDeck_CardMask_RESET(boards[i]);
        StdDeck_CardMask used = pocket1;
        StdDeck_CardMask_OR(used, used, pocket2);

        int cards_added = 0;
        while (cards_added < 5)
        {
            int card = rand() % 52;
            StdDeck_CardMask card_mask;
            StdDeck_CardMask_RESET(card_mask);
            StdDeck_CardMask_SET(card_mask, card);

            if (!StdDeck_CardMask_ANY_SET(used, card_mask))
            {
                StdDeck_CardMask_OR(boards[i], boards[i], card_mask);
                StdDeck_CardMask_OR(used, used, card_mask);
                cards_added++;
            }
        }
    }

    /* Test 1: Without cache */
    printf("1. Without cache (evaluating %d boards, %d times each):\n", n_boards, n_iterations);
    double start = get_time();

    for (int iter = 0; iter < n_iterations; iter++)
    {
        for (int i = 0; i < n_boards; i++)
        {
            StdDeck_CardMask_OR(hand1, pocket1, boards[i]);
            StdDeck_CardMask_OR(hand2, pocket2, boards[i]);
            val1 = StdDeck_StdRules_EVAL_N(hand1, 7);
            val2 = StdDeck_StdRules_EVAL_N(hand2, 7);
        }
    }

    double time_nocache = get_time() - start;
    int total_evals = n_boards * n_iterations * 2;
    printf("   Time: %.3f seconds\n", time_nocache);
    printf("   Total evaluations: %d\n", total_evals);
    printf("   Evaluations/sec: %.0f\n\n", total_evals / time_nocache);

    /* Test 2: With cache */
    printf("2. With cache:\n");
    eval_cache_init_global(8192);

    start = get_time();

    for (int iter = 0; iter < n_iterations; iter++)
    {
        for (int i = 0; i < n_boards; i++)
        {
            StdDeck_CardMask_OR(hand1, pocket1, boards[i]);
            StdDeck_CardMask_OR(hand2, pocket2, boards[i]);
            val1 = StdDeck_StdRules_EVAL_N_Cached(hand1, 7);
            val2 = StdDeck_StdRules_EVAL_N_Cached(hand2, 7);
        }
    }

    double time_cached = get_time() - start;
    printf("   Time: %.3f seconds\n", time_cached);
    printf("   Evaluations/sec: %.0f\n", total_evals / time_cached);
    printf("   Speedup: %.2fx\n\n", time_nocache / time_cached);

    /* Print cache statistics */
    eval_cache_print_stats(eval_cache_get_global());

    /* Expected: High hit rate since we're evaluating the same hands repeatedly */
    double hit_rate = (double)g_eval_cache->stats.hits / ((double)g_eval_cache->stats.hits + (double)g_eval_cache->stats.misses) * 100;
    printf("\nAnalysis: With %d unique hands evaluated %d times each,\n", n_boards * 2, n_iterations);
    printf("we achieve a %.1f%% cache hit rate and %.2fx speedup.\n", hit_rate, time_nocache / time_cached);

    free(boards);
    eval_cache_destroy_global();

    return 0;
}
