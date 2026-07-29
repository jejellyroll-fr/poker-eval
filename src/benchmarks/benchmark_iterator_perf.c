/*
 * benchmark_iterator_perf.c - Benchmark for optimized iterator performance
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/enumeration_adapters.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/cardmask_compat.h>

/* Timer utilities */
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void print_perf(const char* label, uint64_t operations, double time_sec) {
    printf("%-25s: %8.3f ms, %10.1f ops/sec, %8llu ops\n",
           label, time_sec * 1000.0, (double)operations / time_sec,
           (unsigned long long)operations);
}

/* Benchmark JokerDeck k-combination iterator */
static void benchmark_joker_iterator(void) {
    printf("\n=== JokerDeck K-Combination Iterator ===\n");

    /* Test different sizes */
    int test_sizes[] = {2, 3, 5, 7};
    int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (int t = 0; t < num_tests; t++) {
        int k = test_sizes[t];

        /* Create reduced available set (40 cards) */
        uint64_t avail = 0;
        for (int i = 0; i < 40; i++) {
            avail |= (1ULL << i);
        }

        joker_k_iter iter;
        joker_kiter_init(&iter, avail, k);

        uint64_t count = 0;
        uint64_t comb;
        double start = get_time();

        while (joker_kiter_next(&iter, &comb)) {
            count++;
            /* Verify combination has correct size */
            if (__builtin_popcountll(comb) != k) {
                printf("ERROR: Invalid combination size!\n");
                return;
            }
        }

        double elapsed = get_time() - start;

        char label[64];
        snprintf(label, sizeof(label), "JokerDeck C(%d,%d)", 40, k);
        print_perf(label, count, elapsed);
    }
}

/* Benchmark StdDeck combo_generator */
static void benchmark_stddeck_generator(void) {
    printf("\n=== StdDeck Combo Generator ===\n");

    int test_sizes[] = {2, 3, 5, 7};
    int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (int t = 0; t < num_tests; t++) {
        int k = test_sizes[t];

        /* Create available mask (40 cards from standard deck) */
        mask_t avail = 0;
        for (int i = 0; i < 40; i++) {
            avail |= (1ULL << i);
        }

        combo_generator_t* gen = combo_generator_create_simple(avail, k);
        if (!gen) {
            printf("ERROR: Failed to create generator\n");
            continue;
        }

        uint64_t count = 0;
        mask_t combo;
        double start = get_time();

        while (combo_generator_next(gen, &combo)) {
            count++;
            /* Verify combination has correct size */
            if (mask_popcount(combo) != k) {
                printf("ERROR: Invalid combination size!\n");
                combo_generator_destroy(gen);
                return;
            }
        }

        double elapsed = get_time() - start;
        combo_generator_destroy(gen);

        char label[64];
        snprintf(label, sizeof(label), "StdDeck C(%d,%d)", 40, k);
        print_perf(label, count, elapsed);
    }
}

/* Benchmark sampling performance */
static void benchmark_sampling(void) {
    printf("\n=== Sampling Performance ===\n");

    /* StdDeck sampling */
    mask_t avail_std = 0;
    for (int i = 0; i < 52; i++) {
        avail_std |= (1ULL << i);
    }

    /* JokerDeck sampling */
    uint64_t avail_joker = 0;
    for (int i = 0; i < 53; i++) {
        avail_joker |= (1ULL << i);
    }

    int sample_sizes[] = {2, 5, 7};
    int iterations = 100000;

    for (int s = 0; s < 3; s++) {
        int k = sample_sizes[s];

        /* StdDeck sampling */
        double start = get_time();
        for (int i = 0; i < iterations; i++) {
            mask_t sample = sample_k_from_mask(avail_std, k);
            if (mask_popcount(sample) != k) {
                printf("ERROR: StdDeck sample size incorrect\n");
                return;
            }
        }
        double elapsed_std = get_time() - start;

        /* JokerDeck sampling */
        start = get_time();
        for (int i = 0; i < iterations; i++) {
            uint64_t sample = joker_sample_k(avail_joker, k);
            if (__builtin_popcountll(sample) != k) {
                printf("ERROR: JokerDeck sample size incorrect\n");
                return;
            }
        }
        double elapsed_joker = get_time() - start;

        char label_std[64], label_joker[64];
        snprintf(label_std, sizeof(label_std), "StdDeck sample(%d)", k);
        snprintf(label_joker, sizeof(label_joker), "JokerDeck sample(%d)", k);

        print_perf(label_std, iterations, elapsed_std);
        print_perf(label_joker, iterations, elapsed_joker);
    }
}

/* Benchmark cache effectiveness */
static void benchmark_cache_effectiveness(void) {
    printf("\n=== Cache Effectiveness ===\n");

    mask_t avail = 0;
    for (int i = 0; i < 45; i++) {
        avail |= (1ULL << i);
    }

    /* Multiple generators with same mask to test caching */
    combo_generator_t* gen1 = combo_generator_create_simple(avail, 5);
    combo_generator_t* gen2 = combo_generator_create_simple(avail, 5);

    if (!gen1 || !gen2) {
        printf("ERROR: Failed to create generators\n");
        return;
    }

    /* Warm up cache */
    mask_t dummy;
    for (int i = 0; i < 1000; i++) {
        combo_generator_next(gen1, &dummy);
    }

    /* Test cache hits */
    uint64_t hits1, misses1, hits2, misses2;
    combo_generator_get_stats(gen1, NULL, NULL, &hits1, &misses1);

    for (int i = 0; i < 1000; i++) {
        combo_generator_next(gen2, &dummy);
    }

    combo_generator_get_stats(gen2, NULL, NULL, &hits2, &misses2);

    printf("Generator 1 - Hits: %llu, Misses: %llu, Rate: %.1f%%\n",
           (unsigned long long)hits1, (unsigned long long)misses1,
           (double)hits1 * 100.0 / (double)(hits1 + misses1));

    printf("Generator 2 - Hits: %llu, Misses: %llu, Rate: %.1f%%\n",
           (unsigned long long)hits2, (unsigned long long)misses2,
           (double)hits2 * 100.0 / (double)(hits2 + misses2));

    combo_generator_destroy(gen1);
    combo_generator_destroy(gen2);
}

int main(void) {
    printf("Iterator Performance Benchmark\n");
    printf("==============================\n");

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    benchmark_joker_iterator();
    benchmark_stddeck_generator();
    benchmark_sampling();
    benchmark_cache_effectiveness();

    printf("\nBenchmark completed successfully.\n");
    return 0;
}
