/*
 * modern_combinations_example.c - Example demonstrating modern combinatorial generator
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/time_compat.h>

/* Test basic combination generation */
static void test_basic_generation(void) {
    printf("=== Testing Basic Combination Generation ===\n");

    /* Create a simple hand with 5 cards, choose 3 */
    mask_t test_hand = string_to_mask("As Kh Qd Jc Ts");
    combo_generator_t* gen = combo_generator_create_simple(test_hand, 3);
    assert(gen != NULL);

    printf("Generating 3-card combinations from: ");
    char hand_str[64];
    mask_to_string(test_hand, hand_str, sizeof(hand_str));
    printf("%s\n", hand_str);

    uint64_t expected = combo_binomial(5, 3);
    printf("Expected combinations: %llu\n", (unsigned long long)expected);
    printf("Generator reports: %llu\n", (unsigned long long)combo_generator_total(gen));

    assert(combo_generator_total(gen) == expected);

    /* Generate all combinations */
    printf("\nAll 3-card combinations:\n");
    mask_t combination;
    int count = 0;
    while (combo_generator_next(gen, &combination)) {
        char combo_str[64];
        mask_to_string(combination, combo_str, sizeof(combo_str));
        printf("  %d: %s\n", ++count, combo_str);

        /* Verify each combination has exactly 3 cards */
        assert(mask_popcount(combination) == 3);
        /* Verify combination is subset of original hand */
        assert(mask_is_subset(combination, test_hand));
    }

    printf("Generated %d combinations\n", count);
    assert(count == (int)expected);

    combo_generator_destroy(gen);
    printf("✓ Basic generation works correctly\n\n");
}

/* Test different combination sizes */
static void test_different_sizes(void) {
    printf("=== Testing Different Combination Sizes ===\n");

    mask_t deck = string_to_mask("As Kh Qd Jc Ts 9h 8s 7d");
    printf("Testing with deck: ");
    char deck_str[128];
    mask_to_string(deck, deck_str, sizeof(deck_str));
    printf("%s (%d cards)\n", deck_str, mask_popcount(deck));

    for (uint32_t size = 1; size <= 5; size++) {
        combo_generator_t* gen = combo_generator_create_simple(deck, size);
        assert(gen != NULL);

        uint64_t total = combo_generator_total(gen);
        uint64_t expected = combo_binomial(mask_popcount(deck), size);

        printf("  %u-card combinations: %llu (expected: %llu)\n",
               size, (unsigned long long)total, (unsigned long long)expected);
        assert(total == expected);

        /* Count generated combinations */
        mask_t combo;
        uint64_t count = 0;
        while (combo_generator_next(gen, &combo)) {
            assert(mask_popcount(combo) == size);
            count++;
        }
        assert(count == total);
        (void)count; /* Suppress unused warning */

        combo_generator_destroy(gen);
    }

    printf("✓ Different sizes work correctly\n\n");
}

/* Test poker-specific configurations */
static void test_poker_configurations(void) {
    printf("=== Testing Poker-Specific Configurations ===\n");

    /* Texas Hold'em: 2 hole cards from remaining deck */
    mask_t holdem_board = string_to_mask("As Kh Qd Jc Ts");
    mask_t dead_cards = string_to_mask("2s 3h");
    combo_config_t holdem_config = combo_config_holdem(holdem_board, dead_cards);

    combo_generator_t* holdem_gen = combo_generator_create(&holdem_config);
    assert(holdem_gen != NULL);

    uint64_t holdem_combos = combo_generator_total(holdem_gen);
    uint32_t remaining_cards = 52 - mask_popcount(holdem_board) - mask_popcount(dead_cards);
    uint64_t expected_holdem = combo_binomial(remaining_cards, 2);

    printf("Hold'em configuration:\n");
    printf("  Board: ");
    char board_str[64];
    mask_to_string(holdem_board, board_str, sizeof(board_str));
    printf("%s\n", board_str);
    printf("  Dead cards: ");
    char dead_str[64];
    mask_to_string(dead_cards, dead_str, sizeof(dead_str));
    printf("%s\n", dead_str);
    printf("  Remaining cards: %u\n", remaining_cards);
    printf("  Possible hole card combinations: %llu (expected: %llu)\n",
           (unsigned long long)holdem_combos, (unsigned long long)expected_holdem);

    assert(holdem_combos == expected_holdem);

    /* Verify no hole card combinations contain board or dead cards */
    mask_t hole_cards = MASK_EMPTY;
    int holdem_count = 0;
    while (combo_generator_next(holdem_gen, &hole_cards) && holdem_count < 10) {
        assert(!mask_intersects(hole_cards, holdem_board));
        assert(!mask_intersects(hole_cards, dead_cards));
        assert(mask_popcount(hole_cards) == 2);

        char hole_str[32];
        mask_to_string(hole_cards, hole_str, sizeof(hole_str));
        printf("    Example hole cards: %s\n", hole_str);
        holdem_count++;
    }

    combo_generator_destroy(holdem_gen);

    /* Omaha: 4 hole cards */
    combo_config_t omaha_config = combo_config_omaha(holdem_board, dead_cards, 4);
    combo_generator_t* omaha_gen = combo_generator_create(&omaha_config);
    assert(omaha_gen != NULL);

    uint64_t omaha_combos = combo_generator_total(omaha_gen);
    uint64_t expected_omaha = combo_binomial(remaining_cards, 4);

    printf("\nOmaha configuration:\n");
    printf("  Possible 4-card combinations: %llu (expected: %llu)\n",
           (unsigned long long)omaha_combos, (unsigned long long)expected_omaha);

    assert(omaha_combos == expected_omaha);

    combo_generator_destroy(omaha_gen);

    printf("✓ Poker configurations work correctly\n\n");
}

/* Test batch generation */
static void test_batch_generation(void) {
    printf("=== Testing Batch Generation ===\n");

    mask_t test_deck = string_to_mask("As Kh Qd Jc Ts 9h 8s 7d 6c 5h");
    combo_generator_t* gen = combo_generator_create_simple(test_deck, 3);
    assert(gen != NULL);

    uint64_t total = combo_generator_total(gen);
    printf("Total combinations: %llu\n", (unsigned long long)total);

    enum { batch_size = 10 };
    mask_t batch[batch_size];
    size_t total_generated = 0;

    printf("Generating in batches of %zu (showing first 30 for demo):\n", (size_t)batch_size);

    while (combo_generator_has_next(gen)) {
        size_t generated = combo_generator_next_batch(gen, batch, batch_size);
        printf("  Batch: %zu combinations\n", generated);

        for (size_t i = 0; i < generated; i++) {
            assert(mask_popcount(batch[i]) == 3);
            assert(mask_is_subset(batch[i], test_deck));
        }

        total_generated += generated;

        if (total_generated >= 30) break; /* Limit output for demo */
    }

    printf("Generated %zu combinations in batches\n", total_generated);

    combo_generator_destroy(gen);
    printf("✓ Batch generation works correctly\n\n");
}

/* Test iterator interface */
static void test_iterator_interface(void) {
    printf("=== Testing Iterator Interface ===\n");

    mask_t test_cards = string_to_mask("As Kh Qd Jc Ts 9h");
    combo_iterator_t* iter = combo_iterator_create_simple(test_cards, 4, 5);
    assert(iter != NULL);

    printf("Using iterator for 4-card combinations from 6 cards\n");

    mask_t combination = MASK_EMPTY;
    int count = 0;
    while (combo_iterator_has_next(iter) && count < 10) {
        if (combo_iterator_next(iter, &combination)) {
            char combo_str[64];
            mask_to_string(combination, combo_str, sizeof(combo_str));
            printf("  %d: %s\n", ++count, combo_str);

            assert(mask_popcount(combination) == 4);
            assert(mask_is_subset(combination, test_cards));
        } else {
            break; /* No more combinations */
        }
    }

    printf("Generated %d combinations via iterator\n", count);

    /* Test reset */
    combo_iterator_reset(iter);
    assert(combo_iterator_has_next(iter));

    printf("Iterator reset successfully\n");

    combo_iterator_destroy(iter);
    printf("✓ Iterator interface works correctly\n\n");
}

/* Test mathematical utilities */
static void test_mathematical_utilities(void) {
    printf("=== Testing Mathematical Utilities ===\n");

    /* Test binomial coefficient calculation */
    struct {
        uint32_t n, k;
        uint64_t expected;
    } binomial_tests[] = {
        {5, 0, 1},
        {5, 1, 5},
        {5, 2, 10},
        {5, 3, 10},
        {5, 4, 5},
        {5, 5, 1},
        {52, 2, 1326},
        {52, 5, 2598960},
        {13, 5, 1287}
    };

    printf("Testing binomial coefficient calculations:\n");
    for (size_t i = 0; i < sizeof(binomial_tests) / sizeof(binomial_tests[0]); i++) {
        uint32_t n = binomial_tests[i].n;
        uint32_t k = binomial_tests[i].k;
        uint64_t expected = binomial_tests[i].expected;
        uint64_t result = combo_binomial(n, k);

        printf("  C(%u,%u) = %llu (expected: %llu) %s\n",
               n, k, (unsigned long long)result, (unsigned long long)expected,
               result == expected ? "✓" : "✗");

        assert(result == expected);
    }

    /* Test count functions */
    mask_t full_deck = MASK_EMPTY;
    for (int i = 0; i < 52; i++) {
        full_deck = mask_set(full_deck, i);
    }

    uint64_t count_5 = combo_count_combinations(full_deck, 5);
    uint64_t expected_5 = combo_binomial(52, 5);
    printf("\nCounting combinations: C(52,5) = %llu (expected: %llu)\n",
           (unsigned long long)count_5, (unsigned long long)expected_5);
    assert(count_5 == expected_5);

    printf("✓ Mathematical utilities work correctly\n\n");
}

/* Test performance with larger datasets */
static void test_performance(void) {
    printf("=== Testing Performance ===\n");

    /* Create full deck */
    mask_t full_deck = MASK_EMPTY;
    for (int i = 0; i < 52; i++) {
        full_deck = mask_set(full_deck, i);
    }

    /* Test generation of 5-card hands - create fresh generator for clean stats */
    combo_generator_t* gen = combo_generator_create_simple(full_deck, 5);
    assert(gen != NULL);

    uint64_t total = combo_generator_total(gen);
    printf("Generating subset of %llu total 5-card combinations...\n",
           (unsigned long long)total);

    /* Verify initial stats are clean */
    uint64_t initial_generated;
    combo_generator_get_stats(gen, &initial_generated, NULL, NULL, NULL);
    printf("Initial stats: %llu combinations generated\n", (unsigned long long)initial_generated);

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    const int test_count = 100000;
    mask_t combination;
    int generated = 0;

    while (generated < test_count && combo_generator_next(gen, &combination)) {
        assert(mask_popcount(combination) == 5);
        generated++;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    uint64_t time_ns = (end_time.tv_sec - start_time.tv_sec) * 1000000000ULL +
                      (end_time.tv_nsec - start_time.tv_nsec);
    double time_per_combo = (double)time_ns / generated;
    double combos_per_sec = 1000000000.0 / time_per_combo;

    printf("Performance results:\n");
    printf("  Generated: %d combinations\n", generated);
    printf("  Time: %.2f ms\n", (double)time_ns / 1000000.0);
    printf("  Time per combination: %.0f ns\n", time_per_combo);
    printf("  Combinations per second: %.0f\n", combos_per_sec);

    /* Verify stats match our count */
    uint64_t stats_generated;
    combo_generator_get_stats(gen, &stats_generated, NULL, NULL, NULL);
    printf("\nStats verification: expected=%d, actual=%llu %s\n",
           generated, (unsigned long long)stats_generated,
           generated == (int)stats_generated ? "✓" : "✗");
    assert(generated == (int)stats_generated);

    /* Print generator statistics */
    combo_generator_print_stats(gen);

    combo_generator_destroy(gen);
    printf("✓ Performance test completed\n\n");
}

/* Test self-test function */
static void test_self_test(void) {
    printf("=== Testing Self-Test Function ===\n");

    bool result = combo_generator_self_test();
    printf("Self-test result: %s\n", result ? "PASS" : "FAIL");
    assert(result);

    printf("✓ Self-test works correctly\n\n");
}

int main(void) {
    printf("Modern Combinatorial Generator Test\n");
    printf("==================================\n\n");

    test_basic_generation();
    test_different_sizes();
    test_poker_configurations();
    test_batch_generation();
    test_iterator_interface();
    test_mathematical_utilities();
    test_performance();
    test_self_test();

    printf("🎉 All tests passed! Modern combinatorial generator is working.\n");
    return 0;
}
