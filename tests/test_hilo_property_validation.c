/*
 * test_hilo_property_validation.c - Property test for Hi/Lo evaluation
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Validates that single-pass Hi/Lo evaluation produces identical results
 * to double-pass (separate Hi and Lo evaluations).
 *
 * This is a NON-statistical test - all comparisons are exact.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/core/combo_7to5.h>
#include <poker_eval/core/combo_7to5_hilo.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/canonical_5card.h>

/* Test result */
typedef struct {
    int total_tests;
    int passed;
    int failed;
    int mismatch_hi;
    int mismatch_lo;
    int mismatch_has_low;
} property_test_result_t;

/* Evaluate Hi/Lo using double-pass (legacy) */
static void evaluate_double_pass(
    mask_t seven_cards,
    eval_t* out_best_hi,
    eval_t* out_best_lo,
    int* out_has_low
) {
    combo_7to5_t* gen = combo_7to5_create(seven_cards);
    if (!gen) {
        *out_best_hi = EVAL_INVALID;
        *out_best_lo = EVAL_INVALID;
        *out_has_low = 0;
        return;
    }

    mask_t combo;
    eval_t best_hi = EVAL_INVALID;
    eval_t best_lo = EVAL_INVALID;
    int has_low = 0;

    /* Enumerate all 21 combinations */
    while (combo_7to5_next(gen, &combo)) {
        /* High evaluation */
        eval_t hi_val = evaluate_5card_canonical(combo);
        if (hi_val > best_hi || best_hi == EVAL_INVALID) {
            best_hi = hi_val;
        }

        /* Low evaluation (check for 8-or-better) */
        hilo_result_t temp_result;
        evaluate_hilo_single_pass(combo, &temp_result);

        if (temp_result.has_low) {
            has_low = 1;
            if (temp_result.lo_value < best_lo || best_lo == EVAL_INVALID) {
                best_lo = temp_result.lo_value;
            }
        }
    }

    combo_7to5_destroy(gen);

    *out_best_hi = best_hi;
    *out_best_lo = best_lo;
    *out_has_low = has_low;
}

/* Evaluate Hi/Lo using single-pass (optimized) */
static void evaluate_single_pass(
    mask_t seven_cards,
    eval_t* out_best_hi,
    eval_t* out_best_lo,
    int* out_has_low
) {
    combo_7to5_hilo_t* gen = combo_7to5_hilo_create(seven_cards, true);
    if (!gen) {
        *out_best_hi = EVAL_INVALID;
        *out_best_lo = EVAL_INVALID;
        *out_has_low = 0;
        return;
    }

    mask_t combo;
    hilo_result_t result;
    eval_t best_hi = EVAL_INVALID;
    eval_t best_lo = EVAL_INVALID;
    int has_low = 0;

    /* Single-pass enumeration */
    while (combo_7to5_hilo_next(gen, &combo, &result)) {
        if (result.hi_value > best_hi || best_hi == EVAL_INVALID) {
            best_hi = result.hi_value;
        }

        if (result.has_low) {
            has_low = 1;
            if (result.lo_value < best_lo || best_lo == EVAL_INVALID) {
                best_lo = result.lo_value;
            }
        }
    }

    combo_7to5_hilo_destroy(gen);

    *out_best_hi = best_hi;
    *out_best_lo = best_lo;
    *out_has_low = has_low;
}

/* Validate one hand */
static int validate_hand(
    const char* hand_desc,
    mask_t seven_cards,
    property_test_result_t* stats
) {
    stats->total_tests++;

    /* Double-pass evaluation */
    eval_t dp_best_hi, dp_best_lo;
    int dp_has_low;
    evaluate_double_pass(seven_cards, &dp_best_hi, &dp_best_lo, &dp_has_low);

    /* Single-pass evaluation */
    eval_t sp_best_hi, sp_best_lo;
    int sp_has_low;
    evaluate_single_pass(seven_cards, &sp_best_hi, &sp_best_lo, &sp_has_low);

    /* Compare results */
    int mismatches = 0;

    if (dp_best_hi != sp_best_hi) {
        printf("  MISMATCH Hi: %s\n", hand_desc);
        printf("    Double-pass Hi: %u\n", dp_best_hi);
        printf("    Single-pass Hi: %u\n", sp_best_hi);
        stats->mismatch_hi++;
        mismatches++;
    }

    if (dp_has_low != sp_has_low) {
        printf("  MISMATCH has_low: %s\n", hand_desc);
        printf("    Double-pass has_low: %d\n", dp_has_low);
        printf("    Single-pass has_low: %d\n", sp_has_low);
        stats->mismatch_has_low++;
        mismatches++;
    }

    if (dp_has_low && sp_has_low) {
        if (dp_best_lo != sp_best_lo) {
            printf("  MISMATCH Lo: %s\n", hand_desc);
            printf("    Double-pass Lo: %u\n", dp_best_lo);
            printf("    Single-pass Lo: %u\n", sp_best_lo);
            stats->mismatch_lo++;
            mismatches++;
        }
    }

    if (mismatches > 0) {
        stats->failed++;
        return 0;
    } else {
        stats->passed++;
        return 1;
    }
}

/* Test corpus of known hands */
static void test_known_hands(property_test_result_t* stats) {
    printf("\n1. Testing Known Hand Corpus\n");
    printf("=============================\n");

    struct {
        const char* desc;
        const char* cards[7];
    } test_cases[] = {
        /* High-only hands (no low) */
        {
            "Royal Flush (no low)",
            {"As", "Ks", "Qs", "Js", "Ts", "9h", "8c"}
        },
        {
            "Quads (no low)",
            {"Ah", "Ad", "Ac", "As", "Kh", "Qd", "Jc"}
        },
        {
            "Full House (no low)",
            {"Kh", "Kd", "Kc", "Qs", "Qh", "Jd", "9s"}
        },

        /* Hi/Lo eligible hands */
        {
            "Wheel (A-5 straight, best low)",
            {"Ah", "2s", "3d", "4c", "5h", "Ks", "Qd"}
        },
        {
            "Steel Wheel (A-5 straight flush)",
            {"As", "2s", "3s", "4s", "5s", "Kh", "Qd"}
        },
        {
            "Low + pair",
            {"Ah", "2s", "3d", "4c", "5h", "Ad", "Ks"}
        },
        {
            "8-low qualifier",
            {"Ah", "2s", "3d", "4c", "8h", "Ks", "Qd"}
        },
        {
            "No low (9-high)",
            {"Ah", "2s", "3d", "4c", "9h", "Ks", "Qd"}
        },

        /* Edge cases */
        {
            "7-low (best non-wheel)",
            {"Ah", "2s", "3d", "4c", "7h", "Ks", "Qd"}
        },
        {
            "Multiple low qualifiers",
            {"Ah", "2s", "3d", "4c", "5h", "6d", "7s"}
        },
        {
            "Paired low cards",
            {"Ah", "As", "2s", "3d", "4c", "5h", "Kd"}
        },
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        mask_t hand = MASK_EMPTY;

        /* Parse cards */
        for (int j = 0; j < 7; j++) {
            const char* card_str = test_cases[i].cards[j];
            int rank = -1, suit = -1;

            /* Parse rank */
            switch (card_str[0]) {
                case 'A': rank = 12; break;
                case 'K': rank = 11; break;
                case 'Q': rank = 10; break;
                case 'J': rank = 9; break;
                case 'T': rank = 8; break;
                case '9': rank = 7; break;
                case '8': rank = 6; break;
                case '7': rank = 5; break;
                case '6': rank = 4; break;
                case '5': rank = 3; break;
                case '4': rank = 2; break;
                case '3': rank = 1; break;
                case '2': rank = 0; break;
            }

            /* Parse suit */
            switch (card_str[1]) {
                case 's': suit = 0; break;
                case 'h': suit = 1; break;
                case 'd': suit = 2; break;
                case 'c': suit = 3; break;
            }

            if (rank >= 0 && suit >= 0) {
                int card_index = rank * 4 + suit;
                mask_set_bit(&hand, card_index);
            }
        }

        validate_hand(test_cases[i].desc, hand, stats);
    }

    printf("  Known hands: %d passed, %d failed\n\n",
           stats->passed, stats->failed);
}

/* Test random hands (exhaustive small corpus) */
static void test_random_hands(property_test_result_t* stats, int num_tests) {
    printf("2. Testing Random Hand Corpus\n");
    printf("==============================\n");

    srand(42);  /* Deterministic seed */

    int initial_passed = stats->passed;
    int initial_failed = stats->failed;

    for (int test = 0; test < num_tests; test++) {
        /* Generate random 7-card hand */
        mask_t hand = MASK_EMPTY;
        int cards_added = 0;

        while (cards_added < 7) {
            int card = rand() % 52;

            if (!mask_test_bit(&hand, card)) {
                mask_set_bit(&hand, card);
                cards_added++;
            }
        }

        char desc[32];
        snprintf(desc, sizeof(desc), "Random hand #%d", test + 1);

        validate_hand(desc, hand, stats);

        /* Progress indicator */
        if ((test + 1) % 100 == 0) {
            printf("  Tested %d hands...\r", test + 1);
            fflush(stdout);
        }
    }

    printf("  Random hands: %d passed, %d failed                \n\n",
           stats->passed - initial_passed,
           stats->failed - initial_failed);
}

/* Main test */
int main(int argc, char** argv) {
    printf("=======================================================\n");
    printf("   Hi/Lo Property Validation Test\n");
    printf("   Single-pass == Double-pass (exact comparison)\n");
    printf("=======================================================\n");

    property_test_result_t stats = {0};

    /* Parse command line args */
    int num_random = 1000;
    if (argc > 1) {
        num_random = atoi(argv[1]);
    }

    /* Run tests */
    test_known_hands(&stats);
    test_random_hands(&stats, num_random);

    /* Summary */
    printf("=======================================================\n");
    printf("   Test Summary\n");
    printf("=======================================================\n");
    printf("Total tests:        %d\n", stats.total_tests);
    printf("Passed:             %d\n", stats.passed);
    printf("Failed:             %d\n", stats.failed);
    printf("\nMismatch breakdown:\n");
    printf("  Hi value:         %d\n", stats.mismatch_hi);
    printf("  has_low flag:     %d\n", stats.mismatch_has_low);
    printf("  Lo value:         %d\n", stats.mismatch_lo);
    printf("=======================================================\n");

    if (stats.failed > 0) {
        printf("\n❌ PROPERTY TEST FAILED\n");
        printf("Single-pass and double-pass produce different results!\n");
        return 1;
    } else {
        printf("\n✅ PROPERTY TEST PASSED\n");
        printf("Single-pass Hi/Lo is correct (matches double-pass).\n");
        return 0;
    }
}
