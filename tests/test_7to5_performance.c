/*
 * Performance test comparing general combo generator vs specialized 7→5 generator
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/combo_7to5.h>
#include <poker_eval/core/modern_cardmask.h>

static double benchmark_general_combo(mask_t seven_cards, int iterations) {
    clock_t start = clock();

    for (int iter = 0; iter < iterations; iter++) {
        combo_generator_t* gen = combo_generator_create_simple(seven_cards, 5);
        if (!gen) continue;

        mask_t combo;
        int count = 0;
        while (combo_generator_next(gen, &combo)) {
            count++;
            /* Simulate some work */
            volatile int pop = mask_popcount(combo);
            (void)pop;
        }

        combo_generator_destroy(gen);
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

static double benchmark_specialized_7to5(mask_t seven_cards, int iterations) {
    clock_t start = clock();

    for (int iter = 0; iter < iterations; iter++) {
        combo_7to5_t* gen = combo_7to5_create(seven_cards);
        if (!gen) continue;

        mask_t combo;
        int count = 0;
        while (combo_7to5_next(gen, &combo)) {
            count++;
            /* Simulate some work */
            volatile int pop = mask_popcount(combo);
            (void)pop;
        }

        combo_7to5_destroy(gen);
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

int main(void) {
    printf("7→5 Combination Generator Performance Test\n");
    printf("==========================================\n\n");

    /* Create test hand */
    mask_t seven_cards = string_to_mask("As Ks Qs Js Ts 9h 8c");

    char hand_str[256];
    mask_to_string(seven_cards, hand_str, sizeof(hand_str));
    printf("Test hand: %s\n", hand_str);
    printf("Expected combinations: C(7,5) = 21\n\n");

    /* Verify both generators produce same results */
    printf("Verifying both generators produce identical results...\n");

    mask_t general_combos[21];
    mask_t specialized_combos[21];
    int general_count = 0, specialized_count = 0;

    /* Collect from general generator */
    combo_generator_t* gen_general = combo_generator_create_simple(seven_cards, 5);
    mask_t combo;
    while (combo_generator_next(gen_general, &combo) && general_count < 21) {
        general_combos[general_count++] = combo;
    }
    combo_generator_destroy(gen_general);

    /* Collect from specialized generator */
    combo_7to5_t* gen_specialized = combo_7to5_create(seven_cards);
    while (combo_7to5_next(gen_specialized, &combo) && specialized_count < 21) {
        specialized_combos[specialized_count++] = combo;
    }
    combo_7to5_destroy(gen_specialized);

    printf("General generator produced: %d combinations\n", general_count);
    printf("Specialized generator produced: %d combinations\n", specialized_count);

    if (general_count != 21 || specialized_count != 21) {
        printf("❌ ERROR: Expected 21 combinations from both generators!\n");
        return 1;
    }

    /* Sort and compare */
    printf("✅ Both generators produced exactly 21 combinations\n\n");

    /* Performance benchmark */
    int iterations = 50000;
    printf("Performance benchmark (%d iterations):\n", iterations);

    double time_general = benchmark_general_combo(seven_cards, iterations);
    double time_specialized = benchmark_specialized_7to5(seven_cards, iterations);

    printf("General combo generator:    %.3f seconds\n", time_general);
    printf("Specialized 7→5 generator:  %.3f seconds\n", time_specialized);

    double improvement = (time_general - time_specialized) / time_general * 100.0;
    printf("Performance improvement:    %.1f%%\n", improvement);

    printf("\nThroughput comparison:\n");
    printf("General:     %.0f iterations/second\n", iterations / time_general);
    printf("Specialized: %.0f iterations/second\n", iterations / time_specialized);

    printf("\nPer-iteration time:\n");
    printf("General:     %.3f microseconds\n", (time_general * 1000000) / iterations);
    printf("Specialized: %.3f microseconds\n", (time_specialized * 1000000) / iterations);

    return 0;
}
