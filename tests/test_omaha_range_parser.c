/*
 * Copyright (C) 2024
 *           Poker-eval contributors
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/distributions/plo_integration.h>
#include <poker_eval/range/AdvancedRangeParser.h>

/* Function prototypes */
static void test_plo_pattern_recognition(void);
static void test_percentage_ranges(void);
static void test_validation(void);
static void test_complex_ranges(void);
static void test_omaha_hand_list_integration(void);
static void test_game_type_detection(void);
static void test_error_handling(void);
static void test_plo_pattern_addition(void);
static void test_omaha_operators(void);

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(condition, message)    \
    do                                     \
    {                                      \
        tests_run++;                       \
        if (condition)                     \
        {                                  \
            tests_passed++;                \
            printf("PASS: %s\n", message); \
        }                                  \
        else                               \
        {                                  \
            printf("FAIL: %s\n", message); \
        }                                  \
    } while (0)

/* Test basic PLO pattern recognition */
static void test_plo_pattern_recognition(void)
{
    printf("\n=== Testing PLO Pattern Recognition ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    /* Test valid PLO patterns */
    arp_range_t range;

    /* Test AAxxds pattern */
    int result = ARP_ParseOmahaRange("AAxxds", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse AAxxds pattern");
    TEST_ASSERT(range.is_omaha == true, "Range marked as Omaha");
    TEST_ASSERT(range.count > 0, "AAxxds generates hands");
    if (result)
        ARP_FreeRange(&range);

    /* Test specific hand */
    result = ARP_ParseOmahaRange("AsKdQhJc", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse specific 4-card hand");
    TEST_ASSERT(range.count == 1, "Specific hand generates exactly 1 hand");
    if (result)
        ARP_FreeRange(&range);

    /* Test rundown */
    result = ARP_ParseOmahaRange("JT98r", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse rundown pattern");
    TEST_ASSERT(range.count > 0, "Rundown generates hands");
    if (result)
        ARP_FreeRange(&range);
}

/* Test percentage ranges */
static void test_percentage_ranges(void)
{
    printf("\n=== Testing Percentage Ranges ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;

    /* Test 5% range */
    int result = ARP_GetOmahaTopPercentage(0.05f, game_omaha, dead_cards, &range);
    TEST_ASSERT(result == 1, "Get top 5% Omaha range");
    TEST_ASSERT(range.is_omaha == true, "5% range marked as Omaha");
    TEST_ASSERT(range.count > 0, "5% range generates hands");
    if (result)
        ARP_FreeRange(&range);

    /* Test 20% range */
    result = ARP_GetOmahaTopPercentage(0.20f, game_omaha, dead_cards, &range);
    TEST_ASSERT(result == 1, "Get top 20% Omaha range");
    TEST_ASSERT(range.count > 0, "20% range generates hands");
    if (result)
        ARP_FreeRange(&range);

    /* Test percentage string parsing */
    result = ARP_ParseOmahaRange("20%", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse 20% string");
    TEST_ASSERT(range.is_percentage == true, "Range marked as percentage");
    if (result)
        ARP_FreeRange(&range);
}

/* Test validation */
static void test_validation(void)
{
    printf("\n=== Testing Validation ===\n");

    char error_buffer[256];

    /* Valid patterns */
    int valid = ARP_ValidateOmahaRangeString("AAxxds", error_buffer, sizeof(error_buffer));
    TEST_ASSERT(valid == 1, "AAxxds is valid");

    valid = ARP_ValidateOmahaRangeString("JT98r", error_buffer, sizeof(error_buffer));
    TEST_ASSERT(valid == 1, "JT98r is valid");

    valid = ARP_ValidateOmahaRangeString("20%", error_buffer, sizeof(error_buffer));
    TEST_ASSERT(valid == 1, "20% is valid");

    /* Invalid patterns */
    valid = ARP_ValidateOmahaRangeString("XYZ123", error_buffer, sizeof(error_buffer));
    TEST_ASSERT(valid == 0, "XYZ123 is invalid");

    valid = ARP_ValidateOmahaRangeString("", error_buffer, sizeof(error_buffer));
    TEST_ASSERT(valid == 0, "Empty string is invalid");
}

/* Test complex ranges */
static void test_complex_ranges(void)
{
    printf("\n=== Testing Complex Ranges ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;

    /* Test comma-separated patterns */
    int result = ARP_ParseOmahaRange("AAxxds, KKxxds", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse comma-separated patterns");
    TEST_ASSERT(range.count > 0, "Complex range generates hands");
    if (result)
        ARP_FreeRange(&range);

    /* Test mixed patterns */
    result = ARP_ParseOmahaRange("AAxx, JT98r", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse mixed patterns");
    TEST_ASSERT(range.count > 0, "Mixed range generates hands");
    if (result)
        ARP_FreeRange(&range);
}

/* Test integration with OmahaHandList */
static void test_omaha_hand_list_integration(void)
{
    printf("\n=== Testing OmahaHandList Integration ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;
    int result = ARP_ParseOmahaRange("AAxxds", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse range for integration test");

    if (result)
    {
        OmahaHandList hand_list;
        int init_result = OmahaHandList_Init(&hand_list, (int)range.count);
        TEST_ASSERT(init_result == 1, "Initialize OmahaHandList");

        if (init_result)
        {
            int convert_result = ARP_ToOmahaHandList(&range, &hand_list);
            TEST_ASSERT(convert_result == 1, "Convert range to OmahaHandList");
            TEST_ASSERT(hand_list.count == (int)range.count, "Hand counts match");

            OmahaHandList_Free(&hand_list);
        }

        ARP_FreeRange(&range);
    }
}

/* Test game type detection */
static void test_game_type_detection(void)
{
    printf("\n=== Testing Game Type Detection ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;

    /* Test Omaha game types */
    int result = ARP_ParseOmahaRange("AAxx", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse with game_omaha");
    TEST_ASSERT(range.game_type == game_omaha, "Game type stored correctly");
    if (result)
        ARP_FreeRange(&range);

    result = ARP_ParseOmahaRange("AAxx", dead_cards, game_omaha8, &range);
    TEST_ASSERT(result == 1, "Parse with game_omaha8");
    TEST_ASSERT(range.game_type == game_omaha8, "Game type stored correctly");
    if (result)
        ARP_FreeRange(&range);

    /* Test non-Omaha game type should fail */
    result = ARP_ParseOmahaRange("AAxx", dead_cards, game_holdem, &range);
    TEST_ASSERT(result == 0, "Non-Omaha game type rejected");
}

/* Test error handling */
static void test_error_handling(void)
{
    printf("\n=== Testing Error Handling ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;

    /* Test NULL inputs */
    int result = ARP_ParseOmahaRange(NULL, dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 0, "NULL range string rejected");

    result = ARP_ParseOmahaRange("AAxx", dead_cards, game_omaha, NULL);
    TEST_ASSERT(result == 0, "NULL result pointer rejected");

    /* Test invalid PLO pattern */
    result = ARP_ParseOmahaRange("INVALID", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 0, "Invalid PLO pattern rejected");
}

/* Test PLO pattern addition */
static void test_plo_pattern_addition(void)
{
    printf("\n=== Testing PLO Pattern Addition ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;
    int result = ARP_ParseOmahaRange("AAxxds", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Initialize range for pattern addition");

    if (result)
    {
        size_t initial_count = range.count;

        /* Add another pattern */
        int add_result = ARP_AddPLOPattern("KKxxds", dead_cards, &range);
        TEST_ASSERT(add_result == 1, "Add PLO pattern to existing range");
        TEST_ASSERT(range.count > initial_count, "Hand count increased");

        ARP_FreeRange(&range);
    }
}

/* Test Phase 2 operators (+, -, !) with Omaha patterns */
static void test_omaha_operators(void)
{
    printf("\n=== Testing Omaha Operators ===\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;

    /* Test Addition (+) with PLO patterns */
    int result = ARP_ParseOmahaRange("AAxxds + KKxxds", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse AAxxds + KKxxds");
    TEST_ASSERT(range.count > 0, "AAxxds + KKxxds generates hands");
    if (result)
        ARP_FreeRange(&range);

    /* Test Subtraction (-)  */
    result = ARP_ParseOmahaRange("AAxxds - AAxxss", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse AAxxds - AAxxss");
    if (result)
        ARP_FreeRange(&range);

    /* Test Exclusion (!) */
    result = ARP_ParseOmahaRange("!AAxx", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse !AAxx (complement)");
    if (result)
        ARP_FreeRange(&range);

    /* Test Complex Expression with parentheses */
    result = ARP_ParseOmahaRange("(AAxxds, KKxxds) - (AAxxss)", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse (AAxxds, KKxxds) - (AAxxss)");
    TEST_ASSERT(range.count > 0, "Complex expression generates hands");
    if (result)
        ARP_FreeRange(&range);

    /* Test Operator with Percentage */
    result = ARP_ParseOmahaRange("10% - AAxx", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse 10% - AAxx");
    TEST_ASSERT(range.count > 0, "Subtracting AAxx from 10% leaves hands");
    if (result)
        ARP_FreeRange(&range);

    /* Test Mixed rundowns with operators */
    result = ARP_ParseOmahaRange("JT98ds + JT98r", dead_cards, game_omaha, &range);
    TEST_ASSERT(result == 1, "Parse JT98ds + JT98r");
    TEST_ASSERT(range.count > 0, "Union of rundowns generates hands");
    if (result)
        ARP_FreeRange(&range);
}

int main(void)
{
    printf("Omaha Range Parser Test Suite\n");
    printf("=============================\n");

    /* Run all tests */
    test_plo_pattern_recognition();
    test_percentage_ranges();
    test_validation();
    test_complex_ranges();
    test_omaha_hand_list_integration();
    test_game_type_detection();
    test_error_handling();
    test_plo_pattern_addition();
    test_omaha_operators();

    /* Print results */
    printf("\n=== Test Results ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    printf("Success rate: %.1f%%\n",
           tests_run > 0 ? (float)tests_passed / (float)tests_run * 100.0f : 0.0f);

    if (tests_passed == tests_run)
    {
        printf("\nAll tests PASSED! ✓\n");
        return 0;
    }
    else
    {
        printf("\nSome tests FAILED! ✗\n");
        return 1;
    }
}
