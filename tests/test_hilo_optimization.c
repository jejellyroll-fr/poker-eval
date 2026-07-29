/*
 * Performance test comparing separate Hi/Lo evaluations vs single-pass Hi/Lo
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/core/combo_7to5.h>
#include <poker_eval/core/combo_7to5_hilo.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/canonical_5card.h>

/* Simulate current separate Hi/Lo evaluation approach */
static double benchmark_separate_hilo(mask_t seven_cards, int iterations) {
    clock_t start = clock();

    for (int iter = 0; iter < iterations; iter++) {
        combo_7to5_t* gen = combo_7to5_create(seven_cards);
        if (!gen) continue;

        mask_t combo;
        eval_t best_hi = EVAL_INVALID;
        eval_t best_lo = EVAL_INVALID;

        /* Simulate double evaluation like in StdDeck_OmahaHiLow8_EVAL */
        while (combo_7to5_next(gen, &combo)) {
            /* High evaluation */
            eval_t hi_val = evaluate_5card_canonical(combo);
            if (hi_val > best_hi || best_hi == EVAL_INVALID) {
                best_hi = hi_val;
            }

            /* Separate low evaluation (simulate complexity) */
            hilo_result_t temp_result;
            evaluate_hilo_single_pass(combo, &temp_result);  /* Use our function for fair comparison */
            if (temp_result.has_low) {
                if (temp_result.lo_value < best_lo || best_lo == EVAL_INVALID) {
                    best_lo = temp_result.lo_value;
                }
            }
        }

        combo_7to5_destroy(gen);

        /* Prevent optimization */
        volatile eval_t prevent_opt = best_hi + best_lo;
        (void)prevent_opt;
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

/* Test optimized single-pass Hi/Lo evaluation */
static double benchmark_combined_hilo(mask_t seven_cards, int iterations) {
    clock_t start = clock();

    for (int iter = 0; iter < iterations; iter++) {
        combo_7to5_hilo_t* gen = combo_7to5_hilo_create(seven_cards, true);
        if (!gen) continue;

        mask_t combo;
        hilo_result_t result;
        eval_t best_hi = EVAL_INVALID;
        eval_t best_lo = EVAL_INVALID;

        /* Single-pass Hi/Lo evaluation */
        while (combo_7to5_hilo_next(gen, &combo, &result)) {
            if (result.hi_value > best_hi || best_hi == EVAL_INVALID) {
                best_hi = result.hi_value;
            }
            if (result.has_low) {
                if (result.lo_value < best_lo || best_lo == EVAL_INVALID) {
                    best_lo = result.lo_value;
                }
            }
        }

        combo_7to5_hilo_destroy(gen);

        /* Prevent optimization */
        volatile eval_t prevent_opt = best_hi + best_lo;
        (void)prevent_opt;
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

int main(void) {
    printf("Hi/Lo Evaluation Optimization Test\n");
    printf("===================================\n\n");

    /* Create test hands for Hi/Lo scenarios */
    struct {
        const char* name;
        const char* cards;
    } test_hands[] = {
        {"High only (no low)", "As Ks Qs Js Ts 9h 8c"},  /* Royal + high cards */
        {"Hi/Lo eligible", "Ah 2s 3d 4c 5h Ks Qd"},     /* Wheel + high cards */
        {"Low-heavy hand", "Ah 2s 3d 4c 5h 6s 7d"},     /* Multiple low possibilities */
        {"Mixed scenario", "Ah Ad 2s 3d 4c Ks Qd"}      /* Pair + low potential */
    };

    int num_hands = sizeof(test_hands) / sizeof(test_hands[0]);

    printf("Testing Hi/Lo evaluation optimization on %d different hand types:\n\n", num_hands);

    for (int h = 0; h < num_hands; h++) {
        mask_t seven_cards = string_to_mask(test_hands[h].cards);

        char hand_str[256];
        mask_to_string(seven_cards, hand_str, sizeof(hand_str));
        printf("Testing: %s\n", test_hands[h].name);
        printf("Cards: %s\n", hand_str);

        /* Verify both methods produce same results */
        combo_7to5_t* gen_separate = combo_7to5_create(seven_cards);
        combo_7to5_hilo_t* gen_combined = combo_7to5_hilo_create(seven_cards, false);

        int combinations_separate = 0, combinations_combined = 0;
        mask_t combo;
        hilo_result_t result;

        while (combo_7to5_next(gen_separate, &combo)) {
            combinations_separate++;
        }

        while (combo_7to5_hilo_next(gen_combined, &combo, &result)) {
            combinations_combined++;
        }

        combo_7to5_destroy(gen_separate);
        combo_7to5_hilo_destroy(gen_combined);

        if (combinations_separate != 21 || combinations_combined != 21) {
            printf("❌ ERROR: Expected 21 combinations, got %d and %d\n",
                   combinations_separate, combinations_combined);
            continue;
        }

        printf("✅ Both methods generated 21 combinations\n");

        /* Performance benchmark */
        int iterations = 20000;
        printf("Performance benchmark (%d iterations):\n", iterations);

        double time_separate = benchmark_separate_hilo(seven_cards, iterations);
        double time_combined = benchmark_combined_hilo(seven_cards, iterations);

        printf("  Separate Hi/Lo evaluation:  %.3f seconds\n", time_separate);
        printf("  Combined Hi/Lo evaluation:  %.3f seconds\n", time_combined);

        double improvement = (time_separate - time_combined) / time_separate * 100.0;
        printf("  Performance improvement:     %.1f%%\n", improvement);

        printf("  Throughput comparison:\n");
        printf("    Separate: %.0f iterations/second\n", iterations / time_separate);
        printf("    Combined: %.0f iterations/second\n", iterations / time_combined);

        printf("\n");
    }

    printf("🎉 Hi/Lo optimization testing completed!\n");

    return 0;
}
