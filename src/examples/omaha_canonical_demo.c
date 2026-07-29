/*
 * Omaha + Canonical Evaluator Integration Demo
 *
 * Shows the complete integration between:
 * - Omaha constrained 2+3 generation
 * - Canonical 5-card evaluator
 * - High-performance evaluation context
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/core/omaha_combinations.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

typedef struct {
    mask_t hand;
    eval_t value;
    hand_class_t hand_class;
} omaha_result_t;

static void demo_omaha_canonical_integration(void) {
    printf("🔗 Omaha + Canonical Evaluator Integration\n");
    printf("===========================================\n\n");

    /* Create evaluation context with canonical evaluator */
    EvalConfig config = eval_config_omaha();
    EvalContext* ctx = eval_context_create(&config);
    if (!ctx) {
        printf("Failed to create evaluation context\n");
        return;
    }

    printf("✅ Created EvalContext with canonical 5-card evaluator\n");
    printf("✅ Configured for Omaha poker rules\n\n");

    /* Setup strong Omaha hand */
    mask_t hole = string_to_mask("Ad Ac Kh Ks");      /* Pocket Aces + Kings */
    mask_t board = string_to_mask("As Ah Kc Jc Tc");  /* Full house potential */

    char hole_str[256], board_str[256];
    mask_to_string(hole, hole_str, sizeof(hole_str));
    mask_to_string(board, board_str, sizeof(board_str));

    printf("Omaha hand:\n");
    printf("  Hole cards (4):  %s\n", hole_str);
    printf("  Board cards (5): %s\n\n", board_str);

    /* Create Omaha constrained generator */
    omaha_config_t omaha_config = omaha_config_default();
    omaha_config.enable_stats = true;
    /* Bind constraints explicitly (generator API takes a single config) */
    omaha_config.constraints.hole_cards = hole;
    omaha_config.constraints.board_cards = board;
    omaha_config.constraints.dead_cards = MASK_EMPTY;

    omaha_generator_t* gen = omaha_generator_create(&omaha_config);
    if (!gen) {
        printf("Failed to create Omaha generator\n");
        eval_context_destroy(ctx);
        return;
    }

    printf("✅ Created Omaha constrained generator (2+3 enforcement)\n");
    printf("✅ Expected combinations: C(4,2) × C(5,3) = 6 × 10 = 60\n\n");

    /* Evaluate all combinations using canonical evaluator */
    printf("Evaluating all Omaha combinations with canonical evaluator:\n");

    omaha_result_t results[100];
    int result_count = 0;
    mask_t combination;

    clock_t start = clock();

    while (omaha_generator_next(gen, &combination) && result_count < 100) {
        eval_t eval_value = pe_eval_5c(ctx, combination);
        hand_class_t hand_class = eval_get_hand_class(eval_value);

        results[result_count].hand = combination;
        results[result_count].value = eval_value;
        results[result_count].hand_class = hand_class;
        result_count++;
    }

    clock_t end = clock();
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;

    /* Use real emitted count from generator stats for reporting */
    omaha_stats_t emitted_stats; 
    omaha_generator_get_stats(gen, &emitted_stats);
    printf("  Combinations generated: %llu\n", (unsigned long long)emitted_stats.total_combinations);
    printf("  Evaluation time: %.3f milliseconds\n", time_taken * 1000);
    if (emitted_stats.total_combinations > 0) {
        printf("  Average per combination: %.3f microseconds\n\n",
               (time_taken * 1000000.0) / (double)emitted_stats.total_combinations);
    } else {
        printf("  Average per combination: n/a (no combinations)\n\n");
    }

    /* Find and display best hands */
    printf("Top 5 Omaha combinations:\n");

    /* Simple sort to find top 5 */
    for (int i = 0; i < result_count - 1; i++) {
        for (int j = i + 1; j < result_count; j++) {
            if (results[j].value > results[i].value) {
                omaha_result_t temp = results[i];
                results[i] = results[j];
                results[j] = temp;
            }
        }
    }

    for (int i = 0; i < 5 && i < result_count; i++) {
        char hand_str[256];
        mask_to_string(results[i].hand, hand_str, sizeof(hand_str));
        printf("  #%d: %s -> %s (%u)\n",
               i + 1, hand_str, eval_hand_class_name(results[i].hand_class), results[i].value);
    }

    /* Statistics */
    printf("\nStatistics:\n");
    omaha_stats_t omaha_stats;
    omaha_generator_get_stats(gen, &omaha_stats);
    printf("  Hole combinations: %llu\n", (unsigned long long)omaha_stats.hole_combinations);
    printf("  Board combinations: %llu\n", (unsigned long long)omaha_stats.board_combinations);
    printf("  Total combinations: %llu (expected: %llu)\n",
           (unsigned long long)omaha_stats.total_combinations,
           (unsigned long long)omaha_generator_total(gen));

    eval_stats_t eval_stats;
    eval_context_get_stats(ctx, &eval_stats);
    printf("  5-card evaluations: %llu\n", (unsigned long long)eval_stats.evaluations_5c);
    printf("  LUT lookups: %llu\n", (unsigned long long)eval_stats.lut_lookups);

    printf("\n✅ Integration complete: Omaha generation + Canonical evaluation!\n");

    omaha_generator_destroy(gen);
    eval_context_destroy(ctx);
}

static void demo_performance_scaling(void) {
    printf("\n🚀 Performance Scaling Demo\n");
    printf("============================\n\n");

    EvalConfig config = eval_config_omaha();
    EvalContext* ctx = eval_context_create(&config);
    if (!ctx) return;

    printf("Testing Omaha + Canonical evaluator performance scaling:\n\n");

    /* Test different scenarios */
    struct {
        const char* name;
        const char* hole;
        const char* board;
    } test_cases[] = {
        {"Weak hand", "2c 3d 4h 5s", "6c 7d 8h 9s Tc"},
        {"Medium hand", "Jc Qd Kh As", "2c 3d 4h 5s 6c"},
        {"Strong hand", "Ad Ac Kh Ks", "As Ah Kc Jc Tc"},
        {"Nuts potential", "Ah Kh Qh Jh", "Th 9h 8h 7h 6h"}
    };

    for (int i = 0; i < 4; i++) {
        mask_t hole = string_to_mask(test_cases[i].hole);
        mask_t board = string_to_mask(test_cases[i].board);

        omaha_config_t omaha_config = omaha_config_default();
        omaha_config.constraints.hole_cards = hole;
        omaha_config.constraints.board_cards = board;
        omaha_config.constraints.dead_cards = MASK_EMPTY;
        omaha_generator_t* gen = omaha_generator_create(&omaha_config);
        if (!gen) continue;

        clock_t start = clock();
        mask_t combination;
        int count = 0;
        eval_t best_eval = 0;

        while (omaha_generator_next(gen, &combination)) {
            eval_t eval_value = pe_eval_5c(ctx, combination);
            if (eval_value > best_eval) {
                best_eval = eval_value;
            }
            count++;
        }

        clock_t end = clock();
        double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;

        printf("  %-15s: %2d combinations, %.3f ms, best: %s (%u)\n",
               test_cases[i].name, count, time_taken * 1000,
               eval_hand_class_name(eval_get_hand_class(best_eval)), best_eval);

        omaha_generator_destroy(gen);
    }

    eval_context_destroy(ctx);
}

int main(void) {
    printf("🎯 Omaha + Canonical Evaluator Integration Demo\n");
    printf("===============================================\n\n");

    printf("This demo showcases the complete integration of:\n");
    printf("- Omaha constrained 2+3 combination generation\n");
    printf("- Canonical 5-card poker hand evaluator\n");
    printf("- Thread-safe immutable evaluation context\n");
    printf("- High-performance evaluation pipeline\n\n");

    demo_omaha_canonical_integration();
    demo_performance_scaling();

    printf("\n🎉 Omaha + Canonical integration demo completed!\n");

    return 0;
}
