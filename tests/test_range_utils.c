/*
 * Comprehensive tests for Advanced Range Parser utility functions
 */

#include <stdio.h>
#include <string.h>
#include <poker_eval/range/AdvancedRangeParser.h>

/* Test macros */
#define TEST_ASSERT(condition, message)       \
    do {                                      \
        if (!(condition)) {                   \
            printf("❌ FAIL: %s\n", message); \
            return 0;                         \
        }                                     \
    } while (0)

#define TEST_PASS(message)                \
    do {                                  \
        printf("✅ PASS: %s\n", message); \
        return 1;                         \
    } while (0)

/* Test 1: ARP_CountCombinations */
static int test_count_combinations(void) {
    printf("\n--- Testing ARP_CountCombinations ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Test various range types */
    struct {
        const char *range;
        size_t expected;
    } test_cases[] = {
        {"AA", 6},
        {"AK", 16},
        {"AKs", 4},
        {"AKo", 12},
        {"AA-KK", 12},
        {"AK-AJ", 48},
        {"AA, KK, QQ", 18},
        {NULL, 0}
    };

    for (int i = 0; test_cases[i].range != NULL; i++) {
        size_t count = ARP_CountCombinations(test_cases[i].range, dead,
                                            game_holdem);

        printf("   %s: %zu combos", test_cases[i].range, count);

        if (count == test_cases[i].expected) {
            printf(" ✓\n");
        } else {
            printf(" ✗ (expected %zu)\n", test_cases[i].expected);
            TEST_ASSERT(0, "Count mismatch");
        }
    }

    /* Test with dead cards */
    StdDeck_CardMask_SET(dead,
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

    size_t count_with_dead = ARP_CountCombinations("AA", dead, game_holdem);
    TEST_ASSERT(count_with_dead == 3,
                "AA with dead As should have 3 combos");

    printf("   AA with dead As: %zu combos ✓\n", count_with_dead);

    TEST_PASS("Count combinations tests");
}

/* Test 2: ARP_CloneRange */
static int test_clone_range(void) {
    printf("\n--- Testing ARP_CloneRange ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Create original range */
    arp_range_t original;
    TEST_ASSERT(ARP_ParseRange("AA, KK, QQ", dead, game_holdem, &original),
                "Should parse original range");

    printf("   Original range: %zu hands\n", original.count);

    /* Clone it */
    arp_range_t clone;
    TEST_ASSERT(ARP_CloneRange(&original, &clone),
                "Should clone range successfully");

    printf("   Cloned range: %zu hands\n", clone.count);

    /* Verify clone */
    TEST_ASSERT(clone.count == original.count,
                "Clone should have same count");
    TEST_ASSERT(clone.game_type == original.game_type,
                "Clone should have same game type");
    TEST_ASSERT(clone.has_weights == original.has_weights,
                "Clone should have same weight flag");

    /* Verify hands match */
    for (size_t i = 0; i < original.count; i++) {
        bool found = false;
        for (size_t j = 0; j < clone.count; j++) {
            if (StdDeck_CardMask_EQUAL(original.hands[i], clone.hands[j])) {
                found = true;
                break;
            }
        }
        TEST_ASSERT(found, "All original hands should be in clone");
    }

    printf("   All hands verified ✓\n");

    /* Test independence - modify clone shouldn't affect original */
    size_t original_count = original.count;
    ARP_FreeRange(&clone);

    TEST_ASSERT(original.count == original_count,
                "Freeing clone shouldn't affect original");

    ARP_FreeRange(&original);

    TEST_PASS("Clone range tests");
}

/* Test 3: ARP_RangesEqual */
static int test_ranges_equal(void) {
    printf("\n--- Testing ARP_RangesEqual ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Create two identical ranges */
    arp_range_t range1, range2;
    TEST_ASSERT(ARP_ParseRange("AA, KK", dead, game_holdem, &range1),
                "Should parse range1");
    TEST_ASSERT(ARP_ParseRange("AA, KK", dead, game_holdem, &range2),
                "Should parse range2");

    TEST_ASSERT(ARP_RangesEqual(&range1, &range2),
                "Identical ranges should be equal");
    printf("   Identical ranges: Equal ✓\n");

    ARP_FreeRange(&range2);

    /* Create different range */
    TEST_ASSERT(ARP_ParseRange("AA, QQ", dead, game_holdem, &range2),
                "Should parse different range");

    TEST_ASSERT(!ARP_RangesEqual(&range1, &range2),
                "Different ranges should not be equal");
    printf("   Different ranges: Not equal ✓\n");

    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);

    /* Test same range, different order */
    TEST_ASSERT(ARP_ParseRange("AA, KK", dead, game_holdem, &range1),
                "Should parse range1");
    TEST_ASSERT(ARP_ParseRange("KK, AA", dead, game_holdem, &range2),
                "Should parse range2 (different order)");

    TEST_ASSERT(ARP_RangesEqual(&range1, &range2),
                "Same hands different order should be equal");
    printf("   Different order: Equal ✓\n");

    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);

    TEST_PASS("Ranges equal tests");
}

/* Test 4: ARP_IntersectRanges */
static int test_intersect_ranges(void) {
    printf("\n--- Testing ARP_IntersectRanges ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Create overlapping ranges */
    arp_range_t range1, range2, intersection;

    TEST_ASSERT(ARP_ParseRange("AA, KK, QQ", dead, game_holdem, &range1),
                "Should parse range1");
    TEST_ASSERT(ARP_ParseRange("KK, QQ, JJ", dead, game_holdem, &range2),
                "Should parse range2");

    printf("   Range1: %zu hands (AA, KK, QQ)\n", range1.count);
    printf("   Range2: %zu hands (KK, QQ, JJ)\n", range2.count);

    /* Compute intersection */
    TEST_ASSERT(ARP_IntersectRanges(&range1, &range2, &intersection),
                "Should compute intersection");

    printf("   Intersection: %zu hands\n", intersection.count);

    /* Should have KK and QQ (12 combos) */
    TEST_ASSERT(intersection.count == 12,
                "Intersection should have 12 hands (KK + QQ)");

    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);
    ARP_FreeRange(&intersection);

    /* Test disjoint ranges */
    TEST_ASSERT(ARP_ParseRange("AA, KK", dead, game_holdem, &range1),
                "Should parse range1");
    TEST_ASSERT(ARP_ParseRange("QQ, JJ", dead, game_holdem, &range2),
                "Should parse range2");

    TEST_ASSERT(ARP_IntersectRanges(&range1, &range2, &intersection),
                "Should compute intersection");

    printf("   Disjoint intersection: %zu hands\n", intersection.count);
    TEST_ASSERT(intersection.count == 0,
                "Disjoint ranges should have empty intersection");

    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);
    ARP_FreeRange(&intersection);

    TEST_PASS("Intersect ranges tests");
}

/* Test 5: ARP_ContainsHand */
static int test_contains_hand(void) {
    printf("\n--- Testing ARP_ContainsHand ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Create range */
    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AA, KK", dead, game_holdem, &range),
                "Should parse range");

    /* Create specific hands to test */
    StdDeck_CardMask aces, queens;

    /* AsAh - should be in range */
    StdDeck_CardMask_RESET(aces);
    StdDeck_CardMask_SET(aces,
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(aces,
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    TEST_ASSERT(ARP_ContainsHand(&range, aces),
                "AsAh should be in AA range");
    printf("   AsAh in range: Yes ✓\n");

    /* QsQh - should NOT be in range */
    StdDeck_CardMask_RESET(queens);
    StdDeck_CardMask_SET(queens,
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(queens,
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    TEST_ASSERT(!ARP_ContainsHand(&range, queens),
                "QsQh should NOT be in range");
    printf("   QsQh in range: No ✓\n");

    ARP_FreeRange(&range);

    /* Test with large range */
    TEST_ASSERT(ARP_ParseRange("20%", dead, game_holdem, &range),
                "Should parse 20% range");

    printf("   Testing large range (20%%, %zu hands)...\n", range.count);

    /* AA should be in top 20% */
    TEST_ASSERT(ARP_ContainsHand(&range, aces),
                "AA should be in top 20%");
    printf("   AA in top 20%%: Yes ✓\n");

    /* 72o probably not in top 20% */
    StdDeck_CardMask lowhand;
    StdDeck_CardMask_RESET(lowhand);
    StdDeck_CardMask_SET(lowhand,
        StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(lowhand,
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));

    bool contains_low = ARP_ContainsHand(&range, lowhand);
    printf("   7s2h in top 20%%: %s ✓\n", contains_low ? "Yes" : "No");

    ARP_FreeRange(&range);

    TEST_PASS("Contains hand tests");
}

static int test_import_export_complete(void) {
    printf("\n--- Testing Complete Import/Export ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Create test range */
    arp_range_t original;
    TEST_ASSERT(ARP_ParseRange("AA, KK, AKs", dead, game_holdem, &original),
                "Should parse original range");

    printf("   Original range: %zu hands\n", original.count);

    /* Export to temporary file */
    FILE *f = fopen("/tmp/test_range_complete.txt", "w");
    TEST_ASSERT(f != NULL, "Should open file for writing");

    int exported = ARP_ExportRange(&original, f, game_holdem);
    fclose(f);

    TEST_ASSERT(exported > 0, "Should export hands");
    printf("   Exported %d hands ✓\n", exported);

    /* Import from file */
    f = fopen("/tmp/test_range_complete.txt", "r");
    TEST_ASSERT(f != NULL, "Should open file for reading");

    arp_range_t imported;
    int success = ARP_ImportRange(f, &imported);
    fclose(f);

    TEST_ASSERT(success, "Import should succeed");
    printf("   Imported %zu hands ✓\n", imported.count);

    /* Verify counts match */
    TEST_ASSERT(imported.count == original.count,
                "Imported count should match original");

    /* Verify all hands are present */
    for (size_t i = 0; i < original.count; i++) {
        TEST_ASSERT(ARP_ContainsHand(&imported, original.hands[i]),
                    "All original hands should be in imported range");
    }

    printf("   All hands verified ✓\n");

    ARP_FreeRange(&original);
    ARP_FreeRange(&imported);

    /* Clean up */
    remove("/tmp/test_range_complete.txt");

    TEST_PASS("Complete import/export tests");
}

/* Main test runner */
int main(void) {
    printf("🧪 Advanced Range Parser Utility Tests\n");
    printf("=======================================\n");

    int tests_passed = 0;
    int total_tests = 0;

    /* Run all tests */
    total_tests++;
    if (test_count_combinations())
        tests_passed++;

    total_tests++;
    if (test_clone_range())
        tests_passed++;

    total_tests++;
    if (test_ranges_equal())
        tests_passed++;

    total_tests++;
    if (test_intersect_ranges())
        tests_passed++;

    total_tests++;
    if (test_contains_hand())
        tests_passed++;

    total_tests++;
    if (test_import_export_complete())
        tests_passed++;

    /* Summary */
    printf("\n📊 Test Results\n");
    printf("===============\n");
    printf("Tests passed: %d/%d\n", tests_passed, total_tests);

    if (tests_passed == total_tests) {
        printf("🎉 All utility tests passed!\n");
        return 0;
    } else {
        printf("❌ Some utility tests failed!\n");
        return 1;
    }
}
