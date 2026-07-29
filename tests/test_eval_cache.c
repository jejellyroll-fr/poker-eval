/* test_eval_cache.c - Demonstrate LRU evaluation cache effectiveness */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval_cache.h>
#include <poker_eval/core/eval.h>

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static StdDeck_CardMask random7(void)
{
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    while (StdDeck_numCards(m) < 7)
    {
        int c = rand() % 52;
        StdDeck_CardMask card;
        StdDeck_CardMask_RESET(card);
        StdDeck_CardMask_SET(card, c);
        if (!StdDeck_CardMask_ANY_SET(m, card))
            StdDeck_CardMask_OR(m, m, card);
    }
    return m;
}

int main(void)
{
    srand((unsigned int)time(NULL));
    const int N = 50000;
    StdDeck_CardMask *hands = malloc(sizeof(StdDeck_CardMask) * N);
    for (int i = 0; i < N; i++)
        hands[i] = random7();
    /* baseline */ double t0 = now_sec();
    for (int i = 0; i < N; i++)
        StdDeck_StdRules_EVAL_N(hands[i], 7);
    double t1 = now_sec();
    printf("No cache: %.3f s (%.0f eval/s)\n", t1 - t0, N / (t1 - t0));
    /* create cache */ eval_cache_init_global(16384);
    /* warmup */ for (int i = 0; i < N; i++)
        StdDeck_StdRules_EVAL_N_Cached(hands[i], 7);
    /* timed cached run */ double t2 = now_sec();
    for (int i = 0; i < N; i++)
        StdDeck_StdRules_EVAL_N_Cached(hands[i], 7);
    double t3 = now_sec();
    printf("With cache: %.3f s (%.0f eval/s)\n", t3 - t2, N / (t3 - t2));
    /* stats */ eval_cache_print_stats(eval_cache_get_global());
    free(hands);
    eval_cache_destroy_global();
    return 0;
}
