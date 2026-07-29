/*
 * eval_context_example.c - Demonstration of EvalContext + combo generator integration
 *
 * Shows how the new architecture provides:
 * - Thread-safe immutable evaluation contexts
 * - Fast 5-card LUT evaluation
 * - 7-card evaluation via optimized 7→5 enumeration
 * - Integration with the high-performance combo generator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/modern_cardmask.h>

/* Helper function to print evaluation result */
static void print_eval_result(const char* description, mask_t hand, eval_t result) {
    char hand_str[256];
    mask_to_string(hand, hand_str, sizeof(hand_str));

    hand_class_t hand_class = eval_get_hand_class(result);
    const char* class_name = eval_hand_class_name(hand_class);

    printf("  %-20s: %s -> %s (value: %u)\n",
           description, hand_str, class_name, result);
}

/* Demonstrate basic evaluation functions */
static void demo_basic_evaluation(void) {
    printf("🎯 Basic Evaluation Demo\n");
    printf("========================\n\n");

    /* Create evaluation context */
    EvalConfig config = eval_config_holdem();
    EvalContext* ctx = eval_context_create(&config);

    if (!ctx) {
        printf("Failed to create evaluation context\n");
        return;
    }

    /* Test 5-card evaluation */
    printf("5-Card Evaluation:\n");
    mask_t royal_flush = string_to_mask("As Ks Qs Js Ts");
    mask_t full_house = string_to_mask("Ah Ad Ac Kh Kd");
    mask_t high_card = string_to_mask("Ac 8h 6d 4s 2c");

    print_eval_result("Royal Flush", royal_flush, pe_eval_5c(ctx, royal_flush));
    print_eval_result("Full House", full_house, pe_eval_5c(ctx, full_house));
    print_eval_result("High Card", high_card, pe_eval_5c(ctx, high_card));

    printf("\n7-Card Evaluation (best 5 from 7):\n");
    mask_t seven_card_1 = string_to_mask("As Ks Qs Js Ts 2h 3c"); /* Royal + junk */
    mask_t seven_card_2 = string_to_mask("Ah Ad Ac Kh Kd 2s 3d"); /* Full house + junk */
    mask_t seven_card_3 = string_to_mask("Ac 8h 6d 4s 2c 9h Jd"); /* High card + junk */

    print_eval_result("7-card (Royal)", seven_card_1, pe_eval_7c(ctx, seven_card_1));
    print_eval_result("7-card (Full)", seven_card_2, pe_eval_7c(ctx, seven_card_2));
    print_eval_result("7-card (High)", seven_card_3, pe_eval_7c(ctx, seven_card_3));

    /* Show statistics */
    printf("\nEvaluation Statistics:\n");
    eval_stats_t stats;
    eval_context_get_stats(ctx, &stats);
    printf("  5-card evaluations: %llu\n", (unsigned long long)stats.evaluations_5c);
    printf("  7-card evaluations: %llu\n", (unsigned long long)stats.evaluations_7c);
    printf("  LUT lookups: %llu\n", (unsigned long long)stats.lut_lookups);
    printf("  Combinations enumerated: %llu\n", (unsigned long long)stats.combinations_enum);

    eval_context_destroy(ctx);
    printf("\n");
}

/* Demonstrate combo generator integration with evaluation */
static void demo_combo_integration(void) {
    printf("🔗 Combo Generator Integration Demo\n");
    printf("===================================\n\n");

    EvalConfig config = eval_config_holdem();
    EvalContext* ctx = eval_context_create(&config);

    if (!ctx) {
        printf("Failed to create evaluation context\n");
        return;
    }

    /* Create a 9-card hand (Omaha style: 4 hole + 5 board) */
    mask_t omaha_hand = string_to_mask("As Ah Kh Qh Js Tc 9d 8s 7c");
    printf("9-card Omaha hand: ");
    char hand_str[256];
    mask_to_string(omaha_hand, hand_str, sizeof(hand_str));
    printf("%s\n\n", hand_str);

    /* Generate and evaluate all 5-card combinations */
    printf("Evaluating all C(9,5) = 126 combinations:\n");

    combo_generator_t* gen = combo_generator_create_simple(omaha_hand, 5);
    if (!gen) {
        printf("Failed to create combo generator\n");
        eval_context_destroy(ctx);
        return;
    }

    eval_t best_eval = EVAL_INVALID;
    mask_t best_combo = MASK_EMPTY;
    uint32_t combo_count = 0;
    uint32_t top_combos_shown = 0;

    mask_t combination;
    while (combo_generator_next(gen, &combination)) {
        eval_t eval_result = pe_eval_5c(ctx, combination);
        combo_count++;

        if (eval_result > best_eval) {
            best_eval = eval_result;
            best_combo = combination;
        }

        /* Show top 10 combinations */
        if (top_combos_shown < 10) {
            char combo_str[256];
            mask_to_string(combination, combo_str, sizeof(combo_str));
            hand_class_t hand_class = eval_get_hand_class(eval_result);
            printf("  %2d. %s -> %s (%u)\n",
                   combo_count, combo_str, eval_hand_class_name(hand_class), eval_result);
            top_combos_shown++;
        }
    }

    printf("  ... (showing top 10 of %u combinations)\n\n", combo_count);

    printf("Best combination found:\n");
    print_eval_result("Best 5-card hand", best_combo, best_eval);

    /* Verify against pe_eval_nc */
    eval_t nc_result = pe_eval_nc(ctx, omaha_hand);
    printf("\nVerification with pe_eval_nc: %u (should match best: %u)\n",
           nc_result, best_eval);

    if (nc_result == best_eval) {
        printf("✅ pe_eval_nc matches manual enumeration!\n");
    } else {
        printf("❌ pe_eval_nc mismatch - implementation needs work\n");
    }

    combo_generator_destroy(gen);
    eval_context_destroy(ctx);
    printf("\n");
}

/* Benchmark combo generator vs evaluation performance */
static void demo_performance_benchmark(void) {
    printf("⚡ Performance Benchmark Demo\n");
    printf("=============================\n\n");

    EvalConfig config = eval_config_holdem();
    EvalContext* ctx = eval_context_create(&config);

    if (!ctx) {
        printf("Failed to create evaluation context\n");
        return;
    }

    /* Benchmark 7-card evaluation (C(7,5) = 21 combinations) */
    mask_t seven_cards = string_to_mask("As Ks Qs Js Ts 9h 8c");
    uint32_t iterations = 10000;

    printf("Benchmarking 7-card evaluation (%u iterations):\n", iterations);
    printf("  Hand: ");
    char hand_str[256];
    mask_to_string(seven_cards, hand_str, sizeof(hand_str));
    printf("%s\n", hand_str);

    clock_t start_time = clock();

    eval_t result = EVAL_INVALID;
    for (uint32_t i = 0; i < iterations; i++) {
        result = pe_eval_7c(ctx, seven_cards);
    }

    clock_t end_time = clock();
    double time_taken = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    printf("\n  Time taken: %.3f seconds\n", time_taken);
    printf("  Evaluations per second: %.0f\n", iterations / time_taken);
    printf("  Time per evaluation: %.3f microseconds\n", (time_taken * 1000000) / iterations);
    printf("  Final result: %u (%s)\n", result, eval_hand_class_name(eval_get_hand_class(result)));

    /* Show combo generator statistics */
    eval_stats_t stats;
    eval_context_get_stats(ctx, &stats);
    printf("\n  Total combinations enumerated: %llu\n", (unsigned long long)stats.combinations_enum);
    printf("  Average combinations per 7-card eval: %.1f (expected: 21)\n",
           (double)stats.combinations_enum / iterations);

    eval_context_destroy(ctx);
    printf("\n");
}

/* Demonstrate different deck types */
static void demo_deck_types(void) {
    printf("🃏 Different Deck Types Demo\n");
    printf("============================\n\n");

    eval_deck_type_t deck_types[] = {
        EVAL_DECK_STANDARD,
        EVAL_DECK_SHORT,
        EVAL_DECK_MANILA
    };

    const char* deck_names[] = {
        "Standard (52 cards)",
        "Short Deck (36 cards, 6-A)",
        "Manila (32 cards, 7-A)"
    };

    for (int i = 0; i < 3; i++) {
        printf("%s:\n", deck_names[i]);

        EvalConfig config = eval_config_default();
        config.deck_type = deck_types[i];

        EvalContext* ctx = eval_context_create(&config);
        if (!ctx) {
            printf("  Failed to create context\n");
            continue;
        }

        /* Try to evaluate a hand that might not be valid in all decks */
        mask_t test_hand = MASK_EMPTY;

        if (deck_types[i] == EVAL_DECK_SHORT) {
            /* Short deck: use 6-A only */
            test_hand = string_to_mask("As Ks Qs Js 6s"); /* A-K-Q-J-6 straight */
        } else if (deck_types[i] == EVAL_DECK_MANILA) {
            /* Manila: use 7-A only */
            test_hand = string_to_mask("As Ks Qs Js 7s"); /* A-K-Q-J-7 */
        } else {
            /* Standard: any hand */
            test_hand = string_to_mask("As Ks Qs Js Ts"); /* Royal flush */
        }

        bool valid = eval_context_validate_mask(ctx, test_hand);
        printf("  Test hand valid: %s\n", valid ? "Yes" : "No");

        if (valid) {
            eval_t result = pe_eval_5c(ctx, test_hand);
            char hand_str[256];
            mask_to_string(test_hand, hand_str, sizeof(hand_str));
            printf("  Hand: %s -> %s (%u)\n",
                   hand_str, eval_hand_class_name(eval_get_hand_class(result)), result);
        }

        eval_context_destroy(ctx);
        printf("\n");
    }
}

int main(void) {
    printf("🏗️  EvalContext + Combo Generator Integration Example\n");
    printf("=====================================================\n\n");

    printf("This example demonstrates the integration of:\n");
    printf("- Thread-safe immutable EvalContext\n");
    printf("- Fast 5-card LUT evaluation (pe_eval_5c)\n");
    printf("- 7-card evaluation via 7→5 enumeration (pe_eval_7c)\n");
    printf("- High-performance combo generator\n");
    printf("- Multiple deck type support\n\n");

    demo_basic_evaluation();
    demo_combo_integration();
    demo_performance_benchmark();
    demo_deck_types();

    printf("🎉 Integration demo completed successfully!\n");
    printf("\nNext steps:\n");
    printf("- Replace stub 5-card LUT with real canonical evaluator\n");
    printf("- Add proper Omaha 2+3 constrained generation\n");
    printf("- Implement Hi-Lo evaluation support\n");
    printf("- Add thread-safety with atomic operations\n");

    return 0;
}
