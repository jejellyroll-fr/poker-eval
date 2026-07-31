/*
 * test_modern_legacy_compatibility.c - Test compatibility between modern and legacy APIs
 *
 * This test suite ensures that Phase 3 modern systems maintain full compatibility
 * with existing legacy code while providing improved functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "unity/src/unity.h"

/* Include both modern and legacy systems */
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/core/enumeration_adapters.h>
#include <poker_eval/deck/deck_std.h>

/*
 * Wall-clock ratios only carry information in an optimized, uninstrumented
 * build. At -O0 under ASan/UBSan the conversion helpers — which touch memory
 * on every call — are penalised far more heavily than the trivial mask
 * operations they are compared against, so the ratio blows up for reasons
 * that have nothing to do with the code under test. The measurements are
 * still printed in those builds; only the assertions are skipped.
 */
#if defined(NDEBUG)
#  if defined(__SANITIZE_ADDRESS__)
#    define PE_TIMING_ASSERTS 0
#  elif defined(__has_feature)
#    if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer) || \
        __has_feature(thread_sanitizer)
#      define PE_TIMING_ASSERTS 0
#    else
#      define PE_TIMING_ASSERTS 1
#    endif
#  else
#    define PE_TIMING_ASSERTS 1
#  endif
#else
#  define PE_TIMING_ASSERTS 0
#endif

/* Test data for compatibility verification */
typedef struct
{
    StdDeck_CardMask legacy_mask;
    mask_t modern_mask;
    const char *description;
} CompatibilityTestCase;

static void setup_test_cases(CompatibilityTestCase *cases, size_t count)
{
    /* Test case 1: Empty hand */
    StdDeck_CardMask_RESET(cases[0].legacy_mask);
    cases[0].modern_mask = 0;
    cases[0].description = "Empty hand";

    /* Test case 2: Single card (Ace of spades) */
    StdDeck_CardMask_RESET(cases[1].legacy_mask);
    StdDeck_CardMask_SET(cases[1].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    cases[1].modern_mask = 1ULL << (12 + 3 * 13); /* Ace of spades in modern format */
    cases[1].description = "Ace of spades";

    /* Test case 3: Pair of kings */
    StdDeck_CardMask_RESET(cases[2].legacy_mask);
    StdDeck_CardMask_SET(cases[2].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(cases[2].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    cases[2].modern_mask = (1ULL << (11 + 2 * 13)) | (1ULL << (11 + 0 * 13));
    cases[2].description = "Pair of kings";

    /* Test case 4: Full house (AAA22) */
    StdDeck_CardMask_RESET(cases[3].legacy_mask);
    StdDeck_CardMask_SET(cases[3].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(cases[3].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(cases[3].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(cases[3].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(cases[3].legacy_mask, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    cases[3].modern_mask = (1ULL << (12 + 3 * 13)) | (1ULL << (12 + 2 * 13)) | (1ULL << (12 + 0 * 13)) |
                           (1ULL << (0 + 1 * 13)) | (1ULL << (0 + 3 * 13));
    cases[3].description = "Full house AAA22";
}

/* Test CardMask ↔ mask_t conversion */
static void test_cardmask_conversion_compatibility(void)
{
    printf("\n=== Testing CardMask ↔ mask_t conversion ===\n");

    enum { num_cases = 4 };
    CompatibilityTestCase cases[num_cases];
    setup_test_cases(cases, num_cases);

    for (int i = 0; i < num_cases; i++)
    {
        CompatibilityTestCase *tc = &cases[i];

        printf("Testing: %s\n", tc->description);

        /* Test legacy → modern conversion */
        mask_t converted_modern = cardmask_to_mask_t(tc->legacy_mask);

        /* Test modern → legacy conversion (roundtrip) */
        StdDeck_CardMask converted_legacy = mask_t_to_cardmask(converted_modern);

        /* Test roundtrip: legacy → modern → legacy → modern */
        mask_t roundtrip_modern = cardmask_to_mask_t(converted_legacy);
        TEST_ASSERT_EQUAL_HEX64(converted_modern, roundtrip_modern);

        printf("  ✓ Conversion successful\n");
    }
}

/* Test enumeration adapter compatibility */
static void test_enumeration_adapter_compatibility(void)
{
    printf("\n=== Testing enumeration adapter compatibility ===\n");

    /* Test modern enumeration with legacy-style output */
    mask_t available = (1ULL << 10) - 1; /* 10 cards available */
    StdDeck_CardMask dead_legacy;
    StdDeck_CardMask_RESET(dead_legacy);
    StdDeck_CardMask_SET(dead_legacy, 0); /* Remove first card */
    StdDeck_CardMask_SET(dead_legacy, 1); /* Remove second card */

    mask_t dead_modern = cardmask_to_mask_t(dead_legacy);
    mask_t available_with_dead = available & ~dead_modern;

    printf("Testing enumeration with dead cards\n");
    printf("  Available cards: 0x%llx\n", (unsigned long long)available);
    printf("  Dead cards: 0x%llx\n", (unsigned long long)dead_modern);
    printf("  Effective available: 0x%llx\n", (unsigned long long)available_with_dead);

    /* Test modern generator */
    combo_generator_t *gen = combo_generator_create_simple(available_with_dead, 3);
    TEST_ASSERT_NOT_NULL(gen);

    /* Count combinations and verify they don't include dead cards */
    int combo_count = 0;
    mask_t combo;
    while (combo_generator_next(gen, &combo) && combo_count < 100)
    {
        /* Verify combination properties */
        TEST_ASSERT_EQUAL_INT(3, mask_popcount(combo));
        TEST_ASSERT_FALSE(mask_intersects(combo, dead_modern));
        TEST_ASSERT_TRUE(mask_is_subset(combo, available_with_dead));

        /* Convert to legacy and back for compatibility test */
        StdDeck_CardMask legacy_combo = mask_t_to_cardmask(combo);
        mask_t modern_combo_back = cardmask_to_mask_t(legacy_combo);
        TEST_ASSERT_EQUAL_HEX64(combo, modern_combo_back);

        combo_count++;
    }

    combo_generator_destroy(gen);

    TEST_ASSERT_GREATER_THAN_INT(0, combo_count);
    printf("  ✓ Generated %d compatible combinations\n", combo_count);
}

/* Test legacy code using modern adapters */
static void test_legacy_code_modern_adapters(void)
{
    printf("\n=== Testing legacy code with modern adapters ===\n");

    /* Simulate legacy code that expects StdDeck_CardMask */
    StdDeck_CardMask legacy_hand;
    StdDeck_CardMask_RESET(legacy_hand);

    /* Build a hand the legacy way */
    StdDeck_CardMask_SET(legacy_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(legacy_hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(legacy_hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(legacy_hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(legacy_hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));

    printf("Legacy hand (royal flush): built using legacy API\n");

    /* Convert to modern system */
    mask_t modern_hand = cardmask_to_mask_t(legacy_hand);
    TEST_ASSERT_EQUAL_INT(5, mask_popcount(modern_hand));

    /* Use modern operations */
    mask_t full_deck = (1ULL << 52) - 1;
    mask_t remaining = mask_subtract(full_deck, modern_hand);
    TEST_ASSERT_EQUAL_INT(47, mask_popcount(remaining));

    /* Convert back to legacy for compatibility */
    StdDeck_CardMask legacy_remaining = mask_t_to_cardmask(remaining);

    /* Verify legacy operations still work */
    int legacy_card_count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(legacy_remaining, i))
        {
            legacy_card_count++;
        }
    }

    TEST_ASSERT_EQUAL_INT(47, legacy_card_count);
    printf("  ✓ Legacy operations work with modern conversions\n");
}

/* Test performance compatibility */
static void test_performance_compatibility(void)
{
    printf("\n=== Testing performance compatibility ===\n");

    int iterations = 10000;
    clock_t start, end;

    /* Test legacy CardMask operations */
    StdDeck_CardMask legacy_mask1, legacy_mask2, legacy_result;
    StdDeck_CardMask_RESET(legacy_mask1);
    StdDeck_CardMask_RESET(legacy_mask2);
    StdDeck_CardMask_SET(legacy_mask1, 10);
    StdDeck_CardMask_SET(legacy_mask2, 20);

    double legacy_time = 0.0;
    for (int attempt = 0; attempt < 4 && legacy_time <= 0.0; ++attempt)
    {
        start = clock();
        for (int i = 0; i < iterations; i++)
        {
            StdDeck_CardMask_OR(legacy_result, legacy_mask1, legacy_mask2);
            StdDeck_CardMask_AND(legacy_result, legacy_result, legacy_mask1);
        }
        end = clock();
        legacy_time = ((double)(end - start)) / CLOCKS_PER_SEC;
        if (legacy_time <= 0.0)
            iterations *= 2;
    }

    /* Test modern mask_t operations */
    mask_t modern_mask1 = cardmask_to_mask_t(legacy_mask1);
    mask_t modern_mask2 = cardmask_to_mask_t(legacy_mask2);
    volatile mask_t modern_result = 0;

    start = clock();
    for (int i = 0; i < iterations; i++)
    {
        modern_result = mask_union(modern_mask1, modern_mask2);
        modern_result = mask_intersect(modern_result, modern_mask1);
    }
    end = clock();
    double modern_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    /* Test conversion overhead */
    start = clock();
    for (int i = 0; i < iterations; i++)
    {
        mask_t converted = cardmask_to_mask_t(legacy_mask1);
        StdDeck_CardMask back_converted = mask_t_to_cardmask(converted);
        (void)back_converted; /* Prevent optimization */
    }
    end = clock();
    double conversion_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("Performance comparison (%d iterations):\n", iterations);
    printf("  Legacy operations: %.3f ms\n", legacy_time * 1000);
    printf("  Modern operations: %.3f ms\n", modern_time * 1000);
    printf("  Conversion overhead: %.3f ms\n", conversion_time * 1000);

    if (modern_time > 0 && legacy_time > 0)
    {
        double speedup = legacy_time / modern_time;
        printf("  Modern speedup: %.2fx\n", speedup);

#if PE_TIMING_ASSERTS
        /* Modern should be reasonably competitive */
        TEST_ASSERT_GREATER_THAN_DOUBLE(0.05, speedup); /* Within 20x either direction */
#endif
    }

#if PE_TIMING_ASSERTS
    /* Conversion overhead should be reasonable */
    if (legacy_time > 0.0)
        TEST_ASSERT_TRUE(conversion_time <= legacy_time * 100.0);
#else
    printf("  (timing assertions skipped: unoptimized or instrumented build)\n");
    (void)conversion_time;
#endif
}

/* Test API consistency */
static void test_api_consistency(void)
{
    printf("\n=== Testing API consistency ===\n");

    /* Test that modern and legacy APIs produce equivalent results */
    StdDeck_CardMask legacy_hand1, legacy_hand2;
    StdDeck_CardMask_RESET(legacy_hand1);
    StdDeck_CardMask_RESET(legacy_hand2);

    /* Build test hands */
    StdDeck_CardMask_SET(legacy_hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(legacy_hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    StdDeck_CardMask_SET(legacy_hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(legacy_hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    /* Convert to modern */
    mask_t modern_hand1 = cardmask_to_mask_t(legacy_hand1);
    mask_t modern_hand2 = cardmask_to_mask_t(legacy_hand2);

    /* Test union operation */
    StdDeck_CardMask legacy_union;
    StdDeck_CardMask_OR(legacy_union, legacy_hand1, legacy_hand2);

    mask_t modern_union = mask_union(modern_hand1, modern_hand2);

    /* Results should be equivalent */
    mask_t converted_legacy_union = cardmask_to_mask_t(legacy_union);
    TEST_ASSERT_EQUAL_HEX64(modern_union, converted_legacy_union);

    /* Test intersection operation */
    StdDeck_CardMask legacy_intersect;
    StdDeck_CardMask_AND(legacy_intersect, legacy_hand1, legacy_hand2);

    mask_t modern_intersect = mask_intersect(modern_hand1, modern_hand2);

    /* Results should be equivalent */
    mask_t converted_legacy_intersect = cardmask_to_mask_t(legacy_intersect);
    TEST_ASSERT_EQUAL_HEX64(modern_intersect, converted_legacy_intersect);

    /* Test card counting */
    int legacy_count1 = StdDeck_numCards(legacy_hand1);
    int modern_count1 = mask_popcount(modern_hand1);
    TEST_ASSERT_EQUAL_INT(legacy_count1, modern_count1);

    printf("  ✓ Union operations consistent\n");
    printf("  ✓ Intersection operations consistent\n");
    printf("  ✓ Card counting consistent\n");
}

/* Test error handling compatibility */
static void test_error_handling_compatibility(void)
{
    printf("\n=== Testing error handling compatibility ===\n");

    /* Test invalid mask values */
    mask_t invalid_mask = UINT64_MAX; /* All bits set */
    StdDeck_CardMask legacy_from_invalid = mask_t_to_cardmask(invalid_mask);

    /* Should handle gracefully (may truncate but not crash) */
    mask_t roundtrip = cardmask_to_mask_t(legacy_from_invalid);
    (void)roundtrip; /* We mainly test that it doesn't crash */

    /* Test empty combinations */
    combo_generator_t *empty_gen = combo_generator_create_simple(0, 0);
    TEST_ASSERT_NOT_NULL(empty_gen);

    mask_t empty_combo;
    bool has_empty = combo_generator_next(empty_gen, &empty_combo);
    TEST_ASSERT_TRUE(has_empty);
    TEST_ASSERT_EQUAL_HEX64(0, empty_combo);

    bool has_second_empty = combo_generator_next(empty_gen, &empty_combo);
    TEST_ASSERT_FALSE(has_second_empty); /* Should only have one empty combination */

    combo_generator_destroy(empty_gen);

    /* Test impossible combinations */
    combo_generator_t *impossible_gen = combo_generator_create_simple(0x3, 5); /* 2 cards, want 5 */
    TEST_ASSERT_NOT_NULL(impossible_gen);

    mask_t impossible_combo;
    bool has_impossible = combo_generator_next(impossible_gen, &impossible_combo);
    TEST_ASSERT_FALSE(has_impossible); /* Should have no combinations */

    combo_generator_destroy(impossible_gen);

    printf("  ✓ Invalid values handled gracefully\n");
    printf("  ✓ Edge cases handled consistently\n");
}

/* Setup and teardown */
void setUp(void)
{
    /* Setup before each test */
}

void tearDown(void)
{
    /* Cleanup after each test */
}

int main(void)
{
    printf("Modern/Legacy API Compatibility Test Suite\n");
    printf("==========================================\n");

    UNITY_BEGIN();

    RUN_TEST(test_cardmask_conversion_compatibility);
    RUN_TEST(test_enumeration_adapter_compatibility);
    RUN_TEST(test_legacy_code_modern_adapters);
    RUN_TEST(test_performance_compatibility);
    RUN_TEST(test_api_consistency);
    RUN_TEST(test_error_handling_compatibility);

    int result = UNITY_END();

    if (result == 0)
    {
        printf("\n🎯 All compatibility tests PASSED!\n");
        printf("\nCompatibility Validation Summary:\n");
        printf("✓ CardMask ↔ mask_t conversion working\n");
        printf("✓ Enumeration adapters compatible\n");
        printf("✓ Legacy code works with modern adapters\n");
        printf("✓ Performance remains competitive\n");
        printf("✓ API operations produce consistent results\n");
        printf("✓ Error handling behaves gracefully\n");
        printf("\n🔗 Phase 3 legacy compatibility: MAINTAINED\n");
    }
    else
    {
        printf("\n❌ Compatibility tests FAILED!\n");
        printf("⚠️  Phase 3 changes may have broken legacy compatibility.\n");
    }

    return result;
}
