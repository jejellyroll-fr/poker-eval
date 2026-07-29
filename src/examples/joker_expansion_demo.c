/*
 * joker_expansion_demo.c - Demonstration of controlled joker expansion
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include <poker_eval/core/joker_expansion_controlled.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/cardmask_compat.h>

static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void print_mask_cards(mask_t mask) {
    const char* ranks = "23456789TJQKA";
    const char* suits = "cdhs";

    printf("[");
    bool first = true;

    for (int pos = 0; pos < 52; pos++) {
        if (mask & (1ULL << pos)) {
            if (!first) printf(" ");
            printf("%c%c", ranks[pos % 13], suits[pos / 13]);
            first = false;
        }
    }

    /* Check joker */
    if (mask & (1ULL << 52)) {
        if (!first) printf(" ");
        printf("Jo");
    }

    printf("]");
}

static void demo_basic_expansion(void) {
    printf("\n=== Basic Joker Expansion Demo ===\n");

    /* Create a hand with joker: As Kh Qd Jc Jo */
    mask_t hand = 0;
    hand |= (1ULL << (12 + 3 * 13));  /* As */
    hand |= (1ULL << (11 + 2 * 13));  /* Kh */
    hand |= (1ULL << (10 + 1 * 13));  /* Qd */
    hand |= (1ULL << (9 + 0 * 13));   /* Jc */
    hand |= (1ULL << 52);             /* Joker */

    printf("Original hand: ");
    print_mask_cards(hand);
    printf("\n");

    /* Configure different expansion strategies */
    JokerExpansionConfig configs[] = {
        joker_config_performance(),
        joker_config_accuracy(),
        joker_config_holdem()
    };

    const char* config_names[] = {
        "Performance",
        "Accuracy",
        "Hold'em"
    };

    for (int c = 0; c < 3; c++) {
        printf("\n--- %s Strategy ---\n", config_names[c]);

        JokerExpansionIterator* iter = joker_expansion_create(&configs[c], NULL);
        if (!iter) {
            printf("Failed to create iterator\n");
            continue;
        }

        if (!joker_expansion_init(iter, hand)) {
            printf("Failed to initialize iterator\n");
            joker_expansion_destroy(iter);
            continue;
        }

        printf("Max expansions: %u\n", configs[c].max_expansions);
        printf("Strategy: %d\n", configs[c].strategy);

        int expansion_count = 0;
        mask_t expanded_hand;
        JokerExpansionResult result;

        while (joker_expansion_next(iter, &expanded_hand, &result) && expansion_count < 5) {
            printf("  Expansion %d: ", expansion_count + 1);
            print_mask_cards(expanded_hand);
            printf(" (value: %u)\n", result.hand_value);
            expansion_count++;
        }

        printf("Total expansions tried: %d\n", expansion_count);

        JokerPerformanceStats stats;
        joker_expansion_get_stats(iter, &stats);
        printf("Performance: %.0f expansions, %.1f%% cache hit rate\n",
               stats.avg_expansions_per_eval, stats.cache_hit_rate * 100.0);

        joker_expansion_destroy(iter);
    }
}

static void demo_performance_comparison(void) {
    printf("\n=== Performance Comparison ===\n");

    /* Create various test hands */
    mask_t test_hands[] = {
        /* Flush draw with joker */
        (1ULL << (12 + 3 * 13)) | (1ULL << (11 + 3 * 13)) |
        (1ULL << (10 + 3 * 13)) | (1ULL << (9 + 3 * 13)) | (1ULL << 52),

        /* Straight draw with joker */
        (1ULL << (12 + 0 * 13)) | (1ULL << (11 + 1 * 13)) |
        (1ULL << (10 + 2 * 13)) | (1ULL << (9 + 3 * 13)) | (1ULL << 52),

        /* Trips with joker */
        (1ULL << (12 + 0 * 13)) | (1ULL << (12 + 1 * 13)) |
        (1ULL << (12 + 2 * 13)) | (1ULL << (5 + 3 * 13)) | (1ULL << 52)
    };

    const char* hand_names[] = {
        "Flush draw + Joker",
        "Straight draw + Joker",
        "Trips + Joker"
    };

    int num_hands = sizeof(test_hands) / sizeof(test_hands[0]);
    int iterations = 1000;

    JokerExpansionConfig legacy_config = joker_config_accuracy();  /* Full expansion */
    JokerExpansionConfig modern_config = joker_config_performance();  /* Controlled */

    for (int h = 0; h < num_hands; h++) {
        printf("\n--- %s ---\n", hand_names[h]);
        print_mask_cards(test_hands[h]);
        printf("\n");

        /* Legacy approach (full expansion) */
        double start_time = get_time();
        for (int i = 0; i < iterations; i++) {
            eval_t result = joker_eval_controlled(NULL, test_hands[h], &legacy_config);
            (void)result;  /* Suppress unused warning */
        }
        double legacy_time = get_time() - start_time;

        /* Modern approach (controlled expansion) */
        start_time = get_time();
        for (int i = 0; i < iterations; i++) {
            eval_t result = joker_eval_controlled(NULL, test_hands[h], &modern_config);
            (void)result;  /* Suppress unused warning */
        }
        double modern_time = get_time() - start_time;

        printf("Legacy (full):       %.3f ms (%d expansions)\n",
               legacy_time * 1000.0, legacy_config.max_expansions);
        printf("Modern (controlled): %.3f ms (%d expansions)\n",
               modern_time * 1000.0, modern_config.max_expansions);
        printf("Speedup: %.2fx\n", legacy_time / modern_time);
    }
}

static void demo_cache_effectiveness(void) {
    printf("\n=== Cache Effectiveness Demo ===\n");

    /* Create a hand that will benefit from caching */
    mask_t base_hand = (1ULL << (12 + 0 * 13)) | (1ULL << (11 + 1 * 13)) |
                       (1ULL << (10 + 2 * 13)) | (1ULL << 52);

    JokerExpansionConfig config = joker_config_default();
    config.enable_cache = true;
    config.cache_size = 64;

    JokerExpansionIterator* iter = joker_expansion_create(&config, NULL);
    if (!iter) {
        printf("Failed to create iterator\n");
        return;
    }

    printf("Testing cache with repeated evaluations...\n");

    /* First evaluation - cache miss */
    double start_time = get_time();
    joker_expansion_init(iter, base_hand);

    mask_t expanded_hand;
    JokerExpansionResult result;
    int expansions = 0;
    while (joker_expansion_next(iter, &expanded_hand, &result)) {
        expansions++;
    }
    double first_time = get_time() - start_time;

    /* Reset and repeat - should hit cache */
    start_time = get_time();
    joker_expansion_reset(iter);
    joker_expansion_init(iter, base_hand);

    int cached_expansions = 0;
    while (joker_expansion_next(iter, &expanded_hand, &result)) {
        cached_expansions++;
    }
    double cached_time = get_time() - start_time;

    printf("First evaluation:  %.3f ms (%d expansions)\n",
           first_time * 1000.0, expansions);
    printf("Cached evaluation: %.3f ms (%d expansions)\n",
           cached_time * 1000.0, cached_expansions);

    if (cached_time > 0) {
        printf("Cache speedup: %.2fx\n", first_time / cached_time);
    }

    JokerPerformanceStats stats;
    joker_expansion_get_stats(iter, &stats);
    printf("Cache hit rate: %.1f%%\n", stats.cache_hit_rate * 100.0);

    joker_expansion_destroy(iter);
}

static void demo_strategic_vs_naive(void) {
    printf("\n=== Strategic vs Naive Expansion ===\n");

    /* Hand with multiple improvement paths */
    mask_t complex_hand = 0;
    complex_hand |= (1ULL << (12 + 3 * 13));  /* As */
    complex_hand |= (1ULL << (11 + 3 * 13));  /* Ks */
    complex_hand |= (1ULL << (10 + 3 * 13));  /* Qs */
    complex_hand |= (1ULL << (5 + 1 * 13));   /* 7h */
    complex_hand |= (1ULL << 52);             /* Joker */

    printf("Complex hand: ");
    print_mask_cards(complex_hand);
    printf("\n");

    /* Strategic expansion */
    JokerExpansionConfig strategic_config = joker_config_default();
    strategic_config.strategy = JOKER_EXPAND_STRATEGIC;
    strategic_config.max_expansions = 10;

    /* Ranking-based expansion */
    JokerExpansionConfig ranking_config = joker_config_default();
    ranking_config.strategy = JOKER_EXPAND_RANKING;
    ranking_config.max_expansions = 10;

    printf("\n--- Strategic Expansion ---\n");
    JokerExpansionIterator* strategic_iter = joker_expansion_create(&strategic_config, NULL);
    if (strategic_iter && joker_expansion_init(strategic_iter, complex_hand)) {
        mask_t expanded;
        JokerExpansionResult result;
        int count = 0;

        while (joker_expansion_next(strategic_iter, &expanded, &result) && count < 5) {
            printf("  Option %d: ", count + 1);
            print_mask_cards(expanded);
            printf("\n");
            count++;
        }
    }
    joker_expansion_destroy(strategic_iter);

    printf("\n--- Ranking-based Expansion ---\n");
    JokerExpansionIterator* ranking_iter = joker_expansion_create(&ranking_config, NULL);
    if (ranking_iter && joker_expansion_init(ranking_iter, complex_hand)) {
        mask_t expanded;
        JokerExpansionResult result;
        int count = 0;

        while (joker_expansion_next(ranking_iter, &expanded, &result) && count < 5) {
            printf("  Option %d: ", count + 1);
            print_mask_cards(expanded);
            printf("\n");
            count++;
        }
    }
    joker_expansion_destroy(ranking_iter);
}

int main(void) {
    printf("Controlled Joker Expansion Demonstration\n");
    printf("=========================================\n");

    srand((unsigned int)time(NULL));

    demo_basic_expansion();
    demo_performance_comparison();
    demo_cache_effectiveness();
    demo_strategic_vs_naive();

    printf("\nDemo completed successfully.\n");
    return 0;
}
