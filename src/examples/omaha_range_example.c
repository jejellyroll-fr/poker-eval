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
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/distributions/plo_integration.h>
#include <poker_eval/equity/RangeEquity.h>

/* Function prototypes */
static void print_range_summary(const char *range_string, const arp_range_t *range);
static void test_plo_patterns(void);
static void test_percentage_ranges(void);
static void test_complex_ranges(void);
static void test_validation(void);
static void test_equity_integration(void);
static void test_operators(void);

/* Helper function to print a range summary */
static void print_range_summary(const char *range_string, const arp_range_t *range)
{
    printf("=== Range: %s ===\n", range_string);
    printf("Number of hands: %zu\n", range->count);
    printf("Game type: %s\n", range->is_omaha ? "Omaha" : "Hold'em");

    if (range->is_percentage)
    {
        printf("Percentage used: %.2f%%\n", range->percentage_used * 100.0f);
    }

    printf("First few hands:\n");
    for (size_t i = 0; i < range->count && i < 5; i++)
    {
        printf("  Hand %zu: [CardMask representation]\n", i + 1);
    }
    printf("\n");
}

/* Test basic PLO patterns */
static void test_plo_patterns(void)
{
    printf("=== Testing PLO Patterns ===\n\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    const char *test_patterns[] = {
        "AAxxds",   // Pocket aces double-suited
        "KKxxss",   // Pocket kings single-suited
        "JT98r",    // Jack-Ten-Nine-Eight rainbow
        "AKQJds",   // Broadway double-suited
        "AsKdQhJc", // Specific four-card hand
        "AAxx",     // Pocket aces any suits
        NULL};

    for (int i = 0; test_patterns[i] != NULL; i++)
    {
        arp_range_t range;
        if (ARP_ParseOmahaRange(test_patterns[i], dead_cards, game_omaha, &range))
        {
            print_range_summary(test_patterns[i], &range);
            ARP_FreeRange(&range);
        }
        else
        {
            printf("Failed to parse pattern: %s\n", test_patterns[i]);
        }
    }
}

/* Test percentage ranges */
static void test_percentage_ranges(void)
{
    printf("=== Testing Percentage Ranges ===\n\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    float percentages[] = {0.05f, 0.10f, 0.20f, 0.0f}; // 5%, 10%, 20%

    for (int i = 0; percentages[i] > 0.0f; i++)
    {
        arp_range_t range;
        if (ARP_GetOmahaTopPercentage(percentages[i], game_omaha, dead_cards, &range))
        {
            char desc[64];
            snprintf(desc, sizeof(desc), "Top %.1f%%", percentages[i] * 100.0f);
            print_range_summary(desc, &range);
            ARP_FreeRange(&range);
        }
        else
        {
            printf("Failed to get top %.1f%% range\n", percentages[i] * 100.0f);
        }
    }
}

/* Test complex range expressions */
static void test_complex_ranges(void)
{
    printf("=== Testing Complex Range Expressions ===\n\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    const char *complex_ranges[] = {
        "AAxxds, KKxxds, QQxxds", // Premium pairs double-suited
        "JT98r, JT98ds, JT98ss",  // Rundown in different suits
        "20%",                    // Top 20% as percentage
        "AAxx, KKxx, AKQJds",     // Mixed patterns
        NULL};

    for (int i = 0; complex_ranges[i] != NULL; i++)
    {
        arp_range_t range;
        if (ARP_ParseOmahaRange(complex_ranges[i], dead_cards, game_omaha, &range))
        {
            print_range_summary(complex_ranges[i], &range);
            ARP_FreeRange(&range);
        }
        else
        {
            printf("Failed to parse complex range: %s\n", complex_ranges[i]);
        }
    }
}

/* Test validation */
static void test_validation(void)
{
    printf("=== Testing Range Validation ===\n\n");

    const char *test_ranges[] = {
        "AAxxds", // Valid
        "KKxxss", // Valid
        "XYZ123", // Invalid
        "AAxxzz", // Invalid suit
        "JT98r",  // Valid
        "JT98x",  // Invalid (incomplete)
        NULL};

    for (int i = 0; test_ranges[i] != NULL; i++)
    {
        char error_buffer[256];
        int valid = ARP_ValidateOmahaRangeString(test_ranges[i], error_buffer, sizeof(error_buffer));

        printf("Range '%s': %s", test_ranges[i], valid ? "VALID" : "INVALID");
        if (!valid)
        {
            printf(" - %s", error_buffer);
        }
        printf("\n");
    }
    printf("\n");
}

/* Test integration with equity calculations */
static void test_equity_integration(void)
{
    printf("=== Testing Equity Integration ===\n\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    /* Parse two ranges */
    arp_range_t range1, range2;

    if (!ARP_ParseOmahaRange("AAxxds", dead_cards, game_omaha, &range1))
    {
        printf("Failed to parse range 1\n");
        return;
    }

    if (!ARP_ParseOmahaRange("KKxxds", dead_cards, game_omaha, &range2))
    {
        printf("Failed to parse range 2\n");
        ARP_FreeRange(&range1);
        return;
    }

    printf("Range 1 (AAxxds): %zu hands\n", range1.count);
    printf("Range 2 (KKxxds): %zu hands\n", range2.count);

    /* Convert to OmahaHandList for further processing */
    OmahaHandList list1, list2;

    if (OmahaHandList_Init(&list1, (int)range1.count) &&
        OmahaHandList_Init(&list2, (int)range2.count))
    {
        if (ARP_ToOmahaHandList(&range1, &list1) &&
            ARP_ToOmahaHandList(&range2, &list2))
        {
            printf("Successfully converted ranges to OmahaHandList\n");
            printf("List 1: %d hands\n", list1.count);
            printf("List 2: %d hands\n", list2.count);
        }

        OmahaHandList_Free(&list1);
        OmahaHandList_Free(&list2);
    }

    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);
    printf("\n");
}

/* Test operators (basic implementation) */
static void test_operators(void)
{
    printf("=== Testing Operators (Future Implementation) ===\n\n");

    /* Note: Full operator support (+, -, !) will be implemented in Phase 2
     * This is a placeholder to show the intended functionality
     */

    printf("Planned operator syntax:\n");
    printf("  AAxxds + KKxxds     - Union of two patterns\n");
    printf("  20%% - AAxx          - Top 20%% minus all pocket aces\n");
    printf("  JT98r, !JT98ds      - Rainbow rundown but exclude double-suited\n");
    printf("  (Implementation pending in Phase 2)\n\n");
}

int main(void)
{
    printf("Omaha Range Parser Example\n");
    printf("==========================\n\n");

    /* Test different aspects of the Omaha range parser */
    test_plo_patterns();
    test_percentage_ranges();
    test_complex_ranges();
    test_validation();
    test_equity_integration();
    test_operators();

    printf("Example completed successfully!\n");
    printf("\nNext steps:\n");
    printf("- Implement operator support (+, -, !)\n");
    printf("- Add comprehensive Omaha hand rankings\n");
    printf("- Optimize performance for large ranges\n");
    printf("- Add more PLO pattern variations\n");

    return 0;
}
