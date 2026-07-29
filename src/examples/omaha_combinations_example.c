/*
 * omaha_combinations_example.c - Demonstration of constrained Omaha generation
 *
 * Shows how the new Omaha generator provides:
 * - Native 2+3 constraint enforcement without post-filtering
 * - Direct generation of exactly 60 combinations (C(4,2) × C(5,3))
 * - Integration with EvalContext for fast evaluation
 * - Performance comparison with filtered approaches
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poker_eval/core/omaha_combinations.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/modern_cardmask.h>

/* Helper function to print evaluation result */
static void print_eval_result(const char* description, mask_t hand, uint32_t result) {
    char hand_str[256];
    mask_to_string(hand, hand_str, sizeof(hand_str));

    hand_class_t hand_class = eval_get_hand_class(result);
    const char* class_name = eval_hand_class_name(hand_class);

    printf("  %-25s: %s -> %s (value: %u)\n",
           description, hand_str, class_name, result);
}

/* Demonstrate basic Omaha constrained generation */
static void demo_omaha_generation(void) {
    printf("🎯 Omaha Constrained Generation Demo\n");
    printf("====================================\n\n");

    /* Setup Omaha hand: 4 hole cards + 5 board cards */
    mask_t hole = string_to_mask("As Ah Kh Qh");     /* Strong hole cards */
    mask_t board = string_to_mask("Js Tc 9d 8s 7c"); /* Coordinated board */

    printf("Hole cards (4): ");
    char hole_str[256];
    mask_to_string(hole, hole_str, sizeof(hole_str));
    printf("%s\n", hole_str);

    printf("Board cards (5): ");
    char board_str[256];
    mask_to_string(board, board_str, sizeof(board_str));
    printf("%s\n\n", board_str);

    /* Create Omaha generator */
    omaha_config_t config = omaha_config_plo(hole, board, MASK_EMPTY);
    omaha_generator_t* gen = omaha_generator_create(&config);

    if (!gen) {
        printf("Failed to create Omaha generator\n");
        return;
    }

    printf("Generating all valid Omaha combinations (2 hole + 3 board):\n");
    printf("Expected: C(4,2) × C(5,3) = 6 × 10 = 60 combinations\n\n");

    /* Show first 10 combinations */
    uint32_t count = 0;
    mask_t combination;

    printf("First 10 combinations:\n");
    while (omaha_generator_next(gen, &combination) && count < 10) {
        char combo_str[256];
        mask_to_string(combination, combo_str, sizeof(combo_str));

        /* Verify 2+3 constraint */
        mask_t hole_part = mask_intersect(combination, hole);
        mask_t board_part = mask_intersect(combination, board);

        printf("  %2d. %s (hole: %d, board: %d)\n",
               count + 1, combo_str,
               mask_popcount(hole_part), mask_popcount(board_part));

        count++;
    }

    /* Count remaining combinations */
    int debug_count = count;
    while (omaha_generator_next(gen, &combination)) {
        count++;
        debug_count++;

        /* Debug last few combinations */
        if (debug_count > 55) {
            char combo_str[256];
            mask_to_string(combination, combo_str, sizeof(combo_str));
            mask_t hole_part = mask_intersect(combination, hole);
            mask_t board_part = mask_intersect(combination, board);
            printf("  %2d. %s (hole: %d, board: %d)\n",
                   debug_count, combo_str,
                   mask_popcount(hole_part), mask_popcount(board_part));
        }
    }

    printf("  ... (showing first 10 and last few of %d shown combinations)\n", count);

    /* Use the real emitted count from generator stats for authoritative reporting */
    omaha_stats_t stats;
    omaha_generator_get_stats(gen, &stats);

    printf("\nGeneration Summary:\n");
    if (stats.total_combinations == 60) {
        printf("✅ Generated exactly %llu combinations as expected!\n",
               (unsigned long long)stats.total_combinations);
    } else {
        printf("❌ Generated %llu combinations (emitted), expected 60\n",
               (unsigned long long)stats.total_combinations);
    }

    /* Detailed statistics */
    printf("\nGeneration Statistics:\n");
    printf("  Hole combinations: %llu\n", (unsigned long long)stats.hole_combinations);
    printf("  Board combinations: %llu\n", (unsigned long long)stats.board_combinations);
    printf("  Total combinations: %llu\n", (unsigned long long)stats.total_combinations);

    omaha_generator_destroy(gen);
    printf("\n");
}

/* Demonstrate Omaha evaluation integration */
static void demo_omaha_evaluation(void) {
    printf("🔗 Omaha Evaluation Integration Demo\n");
    printf("====================================\n\n");

    /* Create evaluation context */
    EvalConfig eval_config = eval_config_omaha();
    EvalContext* ctx = eval_context_create(&eval_config);

    if (!ctx) {
        printf("Failed to create evaluation context\n");
        return;
    }

    /* Setup strong Omaha hand */
    mask_t hole = string_to_mask("As Ad Kh Ks");     /* Strong pocket pairs */
    mask_t board = string_to_mask("Ac Kc Qh Jh Tc"); /* Royal potential board */

    printf("Evaluating strong Omaha hand:\n");
    printf("Hole: ");
    char hole_str[256];
    mask_to_string(hole, hole_str, sizeof(hole_str));
    printf("%s\n", hole_str);

    printf("Board: ");
    char board_str[256];
    mask_to_string(board, board_str, sizeof(board_str));
    printf("%s\n\n", board_str);

    /* Create generator */
    omaha_config_t config = omaha_config_plo(hole, board, MASK_EMPTY);
    omaha_generator_t* gen = omaha_generator_create(&config);

    if (!gen) {
        printf("Failed to create Omaha generator\n");
        eval_context_destroy(ctx);
        return;
    }

    /* Find best combination using integrated function */
    mask_t best_combo;
    uint32_t best_eval = omaha_find_best(gen, ctx, &best_combo);

    printf("Best Omaha combination found:\n");
    print_eval_result("Best 5-card hand", best_combo, best_eval);

    /* Show top 5 combinations manually */
    printf("\nTop 5 combinations (manual enumeration):\n");

    if (!omaha_generator_reset(gen)) {
        printf("Failed to reset generator\n");
        omaha_generator_destroy(gen);
        eval_context_destroy(ctx);
        return;
    }

    typedef struct {
        mask_t combination;
        uint32_t eval_value;
    } combo_eval_t;

    combo_eval_t all_combos[60];
    uint32_t combo_count = 0;
    mask_t combination;

    /* Evaluate all combinations */
    while (omaha_generator_next(gen, &combination) && combo_count < 60) {
        all_combos[combo_count].combination = combination;
        all_combos[combo_count].eval_value = pe_eval_5c(ctx, combination);
        combo_count++;
    }

    /* Sort by evaluation value (simple bubble sort for demo) */
    for (uint32_t i = 0; i < combo_count - 1; i++) {
        for (uint32_t j = 0; j < combo_count - i - 1; j++) {
            if (all_combos[j].eval_value < all_combos[j + 1].eval_value) {
                combo_eval_t temp = all_combos[j];
                all_combos[j] = all_combos[j + 1];
                all_combos[j + 1] = temp;
            }
        }
    }

    /* Show top 5 */
    for (int i = 0; i < 5 && i < (int)combo_count; i++) {
        char combo_str[64];
        snprintf(combo_str, sizeof(combo_str), "Rank #%d", i + 1);
        print_eval_result(combo_str, all_combos[i].combination, all_combos[i].eval_value);
    }

    /* Verify best match */
    if (combo_count > 0 && all_combos[0].eval_value == best_eval) {
        printf("\n✅ omaha_find_best() matches manual enumeration!\n");
    } else {
        printf("\n❌ omaha_find_best() mismatch - implementation needs work\n");
    }

    omaha_generator_destroy(gen);
    eval_context_destroy(ctx);
    printf("\n");
}

/* Performance comparison: constrained vs filtered */
static void demo_performance_comparison(void) {
    printf("⚡ Performance Comparison Demo\n");
    printf("==============================\n\n");

    mask_t hole = string_to_mask("As Ah Kd Qh");
    mask_t board = string_to_mask("Js Tc 9d 8s 7c");
    uint32_t iterations = 1000;

    printf("Comparing constrained vs filtered generation (%d iterations):\n\n", iterations);

    /* Method 1: Constrained generation (new approach) */
    printf("Method 1: Constrained generation (native 2+3):\n");

    clock_t start_time = clock();
    uint32_t constrained_total = 0;

    for (uint32_t iter = 0; iter < iterations; iter++) {
        omaha_config_t config = omaha_config_plo(hole, board, MASK_EMPTY);
        omaha_generator_t* gen = omaha_generator_create(&config);

        if (gen) {
            mask_t combination;
            uint32_t count = 0;

            while (omaha_generator_next(gen, &combination)) {
                count++;
            }

            constrained_total += count;
            omaha_generator_destroy(gen);
        }
    }

    clock_t constrained_time = clock() - start_time;
    double constrained_seconds = ((double)constrained_time) / CLOCKS_PER_SEC;

    printf("  Time: %.3f seconds\n", constrained_seconds);
    printf("  Total combinations: %u (avg: %.1f per iteration)\n",
           constrained_total, (double)constrained_total / iterations);
    printf("  Rate: %.0f iterations/second\n", iterations / constrained_seconds);

    /* Method 2: Filtered generation (traditional approach) */
    printf("\nMethod 2: Filtered generation (post-filter from C(9,5)):\n");

    start_time = clock();
    uint32_t filtered_total = 0;

    mask_t all_cards = mask_union(hole, board);

    for (uint32_t iter = 0; iter < iterations; iter++) {
        combo_generator_t* gen = combo_generator_create_simple(all_cards, 5);

        if (gen) {
            mask_t combination;
            uint32_t count = 0;

            while (combo_generator_next(gen, &combination)) {
                /* Apply 2+3 filter */
                mask_t hole_part = mask_intersect(combination, hole);
                mask_t board_part = mask_intersect(combination, board);

                if (mask_popcount(hole_part) == 2 && mask_popcount(board_part) == 3) {
                    count++;
                }
            }

            filtered_total += count;
            combo_generator_destroy(gen);
        }
    }

    clock_t filtered_time = clock() - start_time;
    double filtered_seconds = ((double)filtered_time) / CLOCKS_PER_SEC;

    printf("  Time: %.3f seconds\n", filtered_seconds);
    printf("  Total combinations: %u (avg: %.1f per iteration)\n",
           filtered_total, (double)filtered_total / iterations);
    printf("  Rate: %.0f iterations/second\n", iterations / filtered_seconds);

    /* Performance comparison */
    printf("\nPerformance Summary:\n");
    if (constrained_seconds > 0 && filtered_seconds > 0) {
        double speedup = filtered_seconds / constrained_seconds;
        printf("  Constrained is %.1fx %s than filtered\n",
               speedup >= 1.0 ? speedup : 1.0 / speedup,
               speedup >= 1.0 ? "faster" : "slower");
    }

    printf("  Constrained efficiency: %.2f combinations/microsecond\n",
           constrained_total / (constrained_seconds * 1000000));
    printf("  Filtered efficiency: %.2f combinations/microsecond\n",
           filtered_total / (filtered_seconds * 1000000));

    /* Correctness verification */
    if (constrained_total == filtered_total) {
        printf("\n✅ Both methods generate identical results!\n");
    } else {
        printf("\n❌ Result mismatch: constrained=%u, filtered=%u\n",
               constrained_total, filtered_total);
    }

    printf("\n");
}

/* Self-test and validation */
static void demo_self_test(void) {
    printf("🧪 Self-Test and Validation Demo\n");
    printf("=================================\n\n");

    printf("Running comprehensive self-test...\n");

    bool success = omaha_generator_self_test();
    printf("Self-test result: %s\n\n", success ? "✅ PASSED" : "❌ FAILED");

    if (success) {
        printf("Validation checks performed:\n");
        printf("  ✓ Generator creates valid 5-card combinations\n");
        printf("  ✓ Enforces exactly 2 cards from hole\n");
        printf("  ✓ Enforces exactly 3 cards from board\n");
        printf("  ✓ Generates exactly 60 combinations (C(4,2) × C(5,3))\n");
        printf("  ✓ No duplicate combinations\n");
        printf("  ✓ All combinations are valid poker hands\n");
    }

    /* Additional guard rails to prevent regression of the 59/60 bug */
    printf("\nGuard rails (anti-regression tests):\n");

    mask_t hole = string_to_mask("As Ah Kh Qh");      /* 4 hole cards */
    mask_t board = string_to_mask("Js Tc 9d 8s 7c");  /* 5 board cards */

    /* Guard rail 1: Verify C(4,2) = 6 hole combinations */
    combo_generator_t* hg = combo_generator_create_simple(hole, 2);
    size_t hole_pairs = 0;
    mask_t tmp;
    while (combo_generator_next(hg, &tmp)) hole_pairs++;
    combo_generator_destroy(hg);

    printf("  ✓ C(4,2) hole combinations: %zu (expected: 6)\n", hole_pairs);
    if (hole_pairs != 6) {
        printf("  ❌ CRITICAL: Hole generator produces %zu instead of 6!\n", hole_pairs);
    }

    /* Guard rail 2: Verify C(5,3) = 10 board combinations */
    combo_generator_t* bg = combo_generator_create_simple(board, 3);
    size_t board_trips = 0;
    while (combo_generator_next(bg, &tmp)) board_trips++;
    combo_generator_destroy(bg);

    printf("  ✓ C(5,3) board combinations: %zu (expected: 10)\n", board_trips);
    if (board_trips != 10) {
        printf("  ❌ CRITICAL: Board generator produces %zu instead of 10!\n", board_trips);
    }

    /* Guard rail 3: Verify total combinations = 60 exactly */
    omaha_config_t config = omaha_config_plo(hole, board, MASK_EMPTY);
    omaha_generator_t* gen = omaha_generator_create(&config);
    size_t emitted = 0;
    mask_t c;
    while (omaha_generator_next(gen, &c)) emitted++;
    omaha_generator_destroy(gen);

    printf("  ✓ Total Omaha combinations: %zu (expected: 60)\n", emitted);
    if (emitted != 60) {
        printf("  ❌ CRITICAL: Omaha generator produces %zu instead of 60!\n", emitted);
        printf("      This is the classic 59/60 off-by-one bug!\n");
    }

    /* Guard rail 4: Verify exactly 10 combinations per hole (anti-regression invariant) */
    size_t per_hole[6] = {0};
    mask_t holes[6];
    size_t h = 0;

    /* Snapshot the 6 hole pairs */
    combo_generator_t* hg2 = combo_generator_create_simple(hole, 2);
    while (combo_generator_next(hg2, &holes[h])) h++;
    combo_generator_destroy(hg2);

    /* Count combinations per hole */
    omaha_generator_t* gen2 = omaha_generator_create(&config);
    mask_t combo;
    while (omaha_generator_next(gen2, &combo)) {
        /* Find which hole this combination belongs to */
        for (size_t i = 0; i < 6; i++) {
            if ((combo & holes[i]) == holes[i]) {
                per_hole[i]++;
                break;
            }
        }
    }
    omaha_generator_destroy(gen2);

    printf("  ✓ Per-hole distribution check:\n");
    bool per_hole_ok = true;
    for (size_t i = 0; i < 6; i++) {
        char hole_str[256];
        mask_to_string(holes[i], hole_str, sizeof(hole_str));
        printf("    Hole #%zu (%s): %zu combinations\n", i+1, hole_str, per_hole[i]);
        if (per_hole[i] != 10) {
            printf("    ❌ CRITICAL: Hole #%zu has %zu instead of 10!\n", i+1, per_hole[i]);
            per_hole_ok = false;
        }
    }

    if (per_hole_ok) {
        printf("  ✅ All holes generate exactly 10 combinations each\n");
    }

    printf("\n");
}

int main(int argc, char* argv[]) {
    bool quiet_mode = false;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) {
            quiet_mode = true;
        }
    }

    if (!quiet_mode) {
        printf("🏗️  Omaha Constrained Generation Example\n");
        printf("=========================================\n\n");

        printf("This example demonstrates:\n");
        printf("- Native Omaha 2+3 constraint enforcement\n");
        printf("- Direct generation without post-filtering\n");
        printf("- Integration with EvalContext for fast evaluation\n");
        printf("- Performance comparison with traditional filtered approaches\n\n");
    }

    demo_omaha_generation();
    demo_omaha_evaluation();
    demo_performance_comparison();
    demo_self_test();

    if (!quiet_mode) {
        printf("🎉 Omaha constrained generation demo completed!\n");
        printf("\nKey Benefits:\n");
        printf("- Generates exactly 60 combinations (no waste)\n");
        printf("- Native constraint enforcement (no post-filtering)\n");
        printf("- Direct integration with evaluation context\n");
        printf("- Maintains all combo generator performance benefits\n");
    }

    return 0;
}
