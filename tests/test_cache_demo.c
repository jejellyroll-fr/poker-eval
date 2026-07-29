/* test_cache_demo.c - Demonstrate cache effectiveness */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/eval_cache.h>

int main(void)
{
    printf("=== EVALUATION CACHE DEMONSTRATION ===\n\n");

    /* Create a scenario where cache is beneficial:
     * Monte Carlo simulation evaluating the same hands repeatedly */

    const int n_unique_hands = 500;
    const int n_evaluations_per_hand = 10000;

    /* Generate unique 7-card hands */
    StdDeck_CardMask *hands = malloc(n_unique_hands * sizeof(StdDeck_CardMask));
    for (int i = 0; i < n_unique_hands; i++)
    {
        StdDeck_CardMask_RESET(hands[i]);
        StdDeck_CardMask used;
        StdDeck_CardMask_RESET(used);

        /* Add 7 unique cards */
        int cards = 0;
        while (cards < 7)
        {
            int c = rand() % 52;
            StdDeck_CardMask card;
            StdDeck_CardMask_RESET(card);
            StdDeck_CardMask_SET(card, c);
            if (!StdDeck_CardMask_ANY_SET(used, card))
            {
                StdDeck_CardMask_OR(hands[i], hands[i], card);
                StdDeck_CardMask_OR(used, used, card);
                cards++;
            }
        }
    }

    printf("Test setup:\n");
    printf("- %d unique 7-card hands\n", n_unique_hands);
    printf("- Each hand evaluated %d times\n", n_evaluations_per_hand);
    printf("- Total evaluations: %d\n\n", n_unique_hands * n_evaluations_per_hand);

    /* Warm up */
    for (int i = 0; i < 100; i++)
    {
        StdDeck_StdRules_EVAL_N(hands[i % n_unique_hands], 7);
    }

    /* Test 1: Without cache */
    printf("1. WITHOUT CACHE:\n");
    clock_t start = clock();

    for (int iter = 0; iter < n_evaluations_per_hand; iter++)
    {
        for (int i = 0; i < n_unique_hands; i++)
        {
            HandVal val = StdDeck_StdRules_EVAL_N(hands[i], 7);
            (void)val; /* Prevent optimization */
        }
    }

    clock_t end = clock();
    double time_no_cache = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("   Time: %.3f seconds\n", time_no_cache);

    /* Test 2: With cache */
    printf("\n2. WITH CACHE:\n");

    /* Initialize cache */
    eval_cache_init_global(4096);

    start = clock();

    for (int iter = 0; iter < n_evaluations_per_hand; iter++)
    {
        for (int i = 0; i < n_unique_hands; i++)
        {
            HandVal val = StdDeck_StdRules_EVAL_N_Cached(hands[i], 7);
            (void)val;
        }
    }

    end = clock();
    double time_with_cache = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("   Time: %.3f seconds\n", time_with_cache);

    /* Results */
    printf("\n3. RESULTS:\n");
    printf("   Speedup: %.2fx faster with cache\n", time_no_cache / time_with_cache);
    printf("   Time saved: %.3f seconds\n", time_no_cache - time_with_cache);

    /* Cache statistics */
    printf("\n");
    eval_cache_print_stats(eval_cache_get_global());

    /* Analysis */
    eval_cache_t *cache = eval_cache_get_global();
    double hit_rate = (double)cache->stats.hits / ((double)cache->stats.hits + (double)cache->stats.misses) * 100;
    double avg_time_no_cache = time_no_cache / (n_unique_hands * n_evaluations_per_hand) * 1e9;
    double avg_time_with_cache = time_with_cache / (n_unique_hands * n_evaluations_per_hand) * 1e9;

    printf("\n4. ANALYSIS:\n");
    printf("   Average time per evaluation:\n");
    printf("   - Without cache: %.1f nanoseconds\n", avg_time_no_cache);
    printf("   - With cache: %.1f nanoseconds\n", avg_time_with_cache);
    printf("   - Cache lookup overhead: ~%.1f nanoseconds\n",
           (avg_time_with_cache - avg_time_no_cache * (100 - hit_rate) / 100));

    printf("\n   Conclusion: The cache provides %.0f%% hit rate and %.2fx speedup\n",
           hit_rate, time_no_cache / time_with_cache);
    printf("   for workloads with repeated hand evaluations.\n");

    free(hands);
    eval_cache_destroy_global();

    return 0;
}
