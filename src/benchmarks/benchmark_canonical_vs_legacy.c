/*
 * Canonical vs Legacy Evaluator Performance Benchmark
 *
 * Compares the performance and accuracy of:
 * - New canonical 5-card evaluator (in EvalContext)
 * - Legacy poker evaluation system (if available)
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/canonical_5card.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/modern_combinations.h>

/* Test hand database for comprehensive benchmarking */
typedef struct
{
    const char *name;
    const char *cards;
    hand_class_t expected_class;
} test_hand_t;

static const test_hand_t test_hands[] = {
    {"Royal Flush", "As Ks Qs Js Ts", HAND_STRAIGHT_FLUSH},
    {"Straight Flush", "9h 8h 7h 6h 5h", HAND_STRAIGHT_FLUSH},
    {"Four of a Kind", "Ah Ad Ac As Kh", HAND_FOUR_KIND},
    {"Full House", "Kh Kd Kc 7h 7d", HAND_FULL_HOUSE},
    {"Flush", "Ah Jh 9h 6h 3h", HAND_FLUSH},
    {"Straight", "Ts 9h 8d 7c 6s", HAND_STRAIGHT},
    {"Wheel Straight", "5h 4d 3c 2s Ah", HAND_STRAIGHT},
    {"Three of a Kind", "Qh Qd Qc Jh 9s", HAND_THREE_KIND},
    {"Two Pair", "Jh Jd 8h 8c 5s", HAND_TWO_PAIR},
    {"One Pair", "Th Td 9h 7c 4s", HAND_PAIR},
    {"High Card", "Ah Kd Qc Jh 9s", HAND_HIGH_CARD},
    {"Low High Card", "8h 6d 4c 3s 2h", HAND_HIGH_CARD}};

static const int num_test_hands = sizeof(test_hands) / sizeof(test_hands[0]);

static void benchmark_canonical_evaluator(void)
{
    printf("🔥 Canonical Evaluator Performance Benchmark\n");
    printf("=============================================\n\n");

    EvalConfig config = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&config);
    if (!ctx)
    {
        printf("Failed to create evaluation context\n");
        return;
    }

    printf("Testing canonical 5-card evaluator accuracy:\n");

    int correct_classifications = 0;
    int total_hands = 0;

    for (int i = 0; i < num_test_hands; i++)
    {
        mask_t hand = string_to_mask(test_hands[i].cards);
        eval_t result = pe_eval_5c(ctx, hand);
        hand_class_t actual_class = eval_get_hand_class(result);

        total_hands++;
        if (actual_class == test_hands[i].expected_class)
        {
            correct_classifications++;
            printf("  ✅ %-20s: %s -> %s (%u)\n",
                   test_hands[i].name, test_hands[i].cards,
                   eval_hand_class_name(actual_class), result);
        }
        else
        {
            printf("  ❌ %-20s: %s -> %s (expected %s) (%u)\n",
                   test_hands[i].name, test_hands[i].cards,
                   eval_hand_class_name(actual_class),
                   eval_hand_class_name(test_hands[i].expected_class), result);
        }
    }

    printf("\nAccuracy: %d/%d (%.1f%%)\n\n",
           correct_classifications, total_hands,
           (100.0 * correct_classifications) / total_hands);

    /* Performance benchmark */
    const int iterations = 100000;
    printf("Performance benchmark (%d iterations per hand):\n", iterations);

    clock_t total_start = clock();
    uint64_t total_evaluations = 0;

    for (int i = 0; i < num_test_hands; i++)
    {
        mask_t hand = string_to_mask(test_hands[i].cards);

        clock_t start = clock();
        eval_t result = 0;

        for (int iter = 0; iter < iterations; iter++)
        {
            result = pe_eval_5c(ctx, hand);
        }

        clock_t end = clock();
        double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
        total_evaluations += iterations;

        printf("  %-20s: %.3f ms (%d eval/sec), final value: %u\n",
               test_hands[i].name, time_taken * 1000,
               (int)(iterations / time_taken), result);
    }

    clock_t total_end = clock();
    double total_time = ((double)(total_end - total_start)) / CLOCKS_PER_SEC;

    printf("\nOverall Performance:\n");
    printf("  Total evaluations: %llu\n", (unsigned long long)total_evaluations);
    printf("  Total time: %.3f seconds\n", total_time);
    printf("  Average rate: %.0f evaluations/second\n", (double)total_evaluations / total_time);
    printf("  Average time: %.3f nanoseconds/evaluation\n", (total_time * 1000000000) / (double)total_evaluations);

    /* Memory usage */
    size_t memory_usage = eval_context_memory_usage(ctx);
    printf("  Memory usage: %.2f KB\n", (double)memory_usage / 1024.0);

    /* Statistics */
    eval_stats_t stats;
    eval_context_get_stats(ctx, &stats);
    printf("\nEvaluation Statistics:\n");
    printf("  5-card evaluations: %llu\n", (unsigned long long)stats.evaluations_5c);
    printf("  LUT lookups: %llu\n", (unsigned long long)stats.lut_lookups);

    eval_context_destroy(ctx);
}

static void benchmark_direct_canonical(void)
{
    printf("\n🚀 Direct Canonical vs EvalContext Comparison\n");
    printf("==============================================\n\n");

    const int iterations = 50000;
    printf("Comparing evaluation methods (%d iterations):\n\n", iterations);

    mask_t test_hand = string_to_mask("As Ks Qs Js Ts"); /* Royal flush */
    char hand_str[256];
    mask_to_string(test_hand, hand_str, sizeof(hand_str));
    printf("Test hand: %s\n\n", hand_str);

    /* Method 1: EvalContext (includes thread-safety overhead) */
    EvalConfig config = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&config);

    clock_t start1 = clock();
    eval_t result1 = 0;
    for (int i = 0; i < iterations; i++)
    {
        result1 = pe_eval_5c(ctx, test_hand);
    }
    clock_t end1 = clock();
    double time1 = ((double)(end1 - start1)) / CLOCKS_PER_SEC;

    /* Method 2: Direct canonical evaluator */
    clock_t start2 = clock();
    uint32_t result2 = 0;
    for (int i = 0; i < iterations; i++)
    {
        result2 = evaluate_5card_canonical(test_hand);
    }
    clock_t end2 = clock();
    double time2 = ((double)(end2 - start2)) / CLOCKS_PER_SEC;

    printf("Method 1 - EvalContext (pe_eval_5c):\n");
    printf("  Time: %.3f seconds\n", time1);
    printf("  Rate: %.0f evaluations/second\n", iterations / time1);
    printf("  Result: %u\n\n", result1);

    printf("Method 2 - Direct Canonical:\n");
    printf("  Time: %.3f seconds\n", time2);
    printf("  Rate: %.0f evaluations/second\n", iterations / time2);
    printf("  Result: %u\n\n", result2);

    double speedup = time1 / time2;
    printf("Performance Analysis:\n");
    printf("  Direct canonical is %.1fx faster than EvalContext\n", speedup);
    printf("  EvalContext overhead: %.1f%%\n", ((time1 - time2) / time2) * 100);
    printf("  Results match: %s\n", (result1 == result2) ? "✅ Yes" : "❌ No");

    eval_context_destroy(ctx);
}

static void benchmark_7card_enumeration(void)
{
    printf("\n🎯 7-Card Enumeration Performance\n");
    printf("=================================\n\n");

    /* Test different 7-card scenarios */
    struct
    {
        const char *name;
        const char *cards;
    } test_7cards[] = {
        {"Royal + junk", "As Ks Qs Js Ts 2h 3c"},
        {"Trips + pair", "Ah Ad Ac Kh Kd 2s 3c"},
        {"Mixed suits", "Ah Kd Qc Js Th 9h 8c"},
        {"Low cards", "7h 6d 5c 4s 3h 2d 8c"}};

    const int iterations = 5000;
    for (int pass = 0; pass < 2; ++pass) {
        EvalConfig config = eval_config_holdem();
        config.enable_simd = (pass == 0); /* pass 0: fast-path ON, pass 1: OFF */
        EvalContext *ctx = eval_context_create(&config);
        if (!ctx) return;

        printf("Testing 7→5 enumeration performance (%d iters) [enable_simd=%s]:\n\n",
               iterations, config.enable_simd ? "true" : "false");

        for (int i = 0; i < 4; i++) {
            mask_t seven_cards = string_to_mask(test_7cards[i].cards);

            clock_t start = clock();
            eval_t best_result = 0;

            for (int iter = 0; iter < iterations; iter++) {
                eval_t result = pe_eval_7c(ctx, seven_cards);
                if (result > best_result) {
                    best_result = result;
                }
            }

            clock_t end = clock();
            double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
            hand_class_t hand_class = eval_get_hand_class(best_result);

            printf("  %-15s: %.3f ms, %.0f eval/sec, best: %s (%u)\n",
                   test_7cards[i].name, time_taken * 1000,
                   iterations / time_taken,
                   eval_hand_class_name(hand_class), best_result);
        }

        /* Statistics */
        eval_stats_t stats;
        eval_context_get_stats(ctx, &stats);
        printf("\n7-Card Evaluation Statistics [enable_simd=%s]:\n",
               config.enable_simd ? "true" : "false");
        printf("  7-card evaluations: %llu\n", (unsigned long long)stats.evaluations_7c);
        printf("  Total combinations enumerated: %llu\n", (unsigned long long)stats.combinations_enum);
        double avg = stats.evaluations_7c ? (double)stats.combinations_enum / (double)stats.evaluations_7c : 0.0;
        printf("  Average combinations per 7-card: %.2f\n\n", avg);

        eval_context_destroy(ctx);
    }
}

int main(void)
{
    printf("⚡ Canonical Evaluator Comprehensive Benchmark\n");
    printf("==============================================\n\n");

    printf("This benchmark tests:\n");
    printf("- Canonical 5-card evaluator accuracy\n");
    printf("- Performance vs direct evaluation\n");
    printf("- 7→5 enumeration efficiency\n");
    printf("- Memory usage and statistics\n\n");

    benchmark_canonical_evaluator();
    benchmark_direct_canonical();
    benchmark_7card_enumeration();

    printf("\n🎉 Benchmark completed successfully!\n");

    return 0;
}
