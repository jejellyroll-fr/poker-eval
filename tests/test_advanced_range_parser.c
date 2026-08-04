/*
 * Unit tests for Advanced Range Parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/deck/deck_std.h>

/* Test helper macros */
#define TEST_ASSERT(condition, message)       \
    do                                        \
    {                                         \
        if (!(condition))                     \
        {                                     \
            printf("❌ FAIL: %s\n", message); \
            return 0;                         \
        }                                     \
    } while (0)

#define TEST_PASS(message)                \
    do                                    \
    {                                     \
        printf("✅ PASS: %s\n", message); \
        return 1;                         \
    } while (0)

/* Test validation function */
static int test_validation(void)
{
    printf("\n--- Testing Range Validation ---\n");

    /* Valid ranges */
    TEST_ASSERT(ARP_ValidateRangeString("AA", NULL, 0), "AA should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AKs", NULL, 0), "AKs should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AKo", NULL, 0), "AKo should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AK", NULL, 0), "AK should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AA-TT", NULL, 0), "AA-TT should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AsKh", NULL, 0), "AsKh should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("20%", NULL, 0), "20% should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AA, KK, QQ", NULL, 0), "AA, KK, QQ should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AKo{50%}", NULL, 0), "AKo{50%} should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AA, !KK", NULL, 0), "AA, !KK should be valid");

    /* Invalid ranges */
    TEST_ASSERT(!ARP_ValidateRangeString("XX", NULL, 0), "XX should be invalid");
    TEST_ASSERT(!ARP_ValidateRangeString("A", NULL, 0), "A should be invalid");
    TEST_ASSERT(!ARP_ValidateRangeString("AK-", NULL, 0), "AK- should be invalid");
    TEST_ASSERT(!ARP_ValidateRangeString("", NULL, 0), "Empty string should be invalid");

    TEST_PASS("Range validation tests");
}

/* Test pocket pairs */
static int test_pocket_pairs(void)
{
    printf("\n--- Testing Pocket Pairs ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    /* Test single pocket pair */
    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AA", dead_cards, game_holdem, &range), "Should parse AA");
    TEST_ASSERT(range.count == 6, "AA should have 6 combinations"); /* C(4,2) = 6 */
    ARP_FreeRange(&range);

    /* Test pocket pair range */
    TEST_ASSERT(ARP_ParseRange("AA-KK", dead_cards, game_holdem, &range), "Should parse AA-KK");
    TEST_ASSERT(range.count == 12, "AA-KK should have 12 combinations"); /* 6 + 6 */
    ARP_FreeRange(&range);

    /* Test larger range */
    TEST_ASSERT(ARP_ParseRange("AA-TT", dead_cards, game_holdem, &range), "Should parse AA-TT");
    TEST_ASSERT(range.count == 30, "AA-TT should have 30 combinations"); /* 5 pairs * 6 each */
    ARP_FreeRange(&range);

    TEST_PASS("Pocket pair tests");
}

/* Test non-pair ranges */
static int test_nonpropair_ranges(void)
{
    printf("\n--- Testing Non-Pair Ranges ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    arp_range_t range;

    // Test AK-AJ (both suited and offsuit)
    if (!ARP_ParseRange("AK-AJ", dead_cards, game_holdem, &range))
    {
        char error[256];
        ARP_ValidateRangeString("AK-AJ", error, sizeof(error));
        printf("❌ Error parsing AK-AJ: %s\n", error);
        return 0;
    }
    TEST_ASSERT(range.count == 48, "AK-AJ should have 48 combinations");
    // 3 hands (AK, AQ, AJ) * 16 combos each = 48
    ARP_FreeRange(&range);

    // Test AKs-AJs (suited only)
    TEST_ASSERT(ARP_ParseRange("AKs-AJs", dead_cards, game_holdem, &range),
                "Should parse AKs-AJs");
    TEST_ASSERT(range.count == 12, "AKs-AJs should have 12 combinations");
    // 3 hands * 4 suited combos each = 12
    ARP_FreeRange(&range);

    // Test AKo-AJo (offsuit only)
    TEST_ASSERT(ARP_ParseRange("AKo-AJo", dead_cards, game_holdem, &range),
                "Should parse AKo-AJo");
    TEST_ASSERT(range.count == 36, "AKo-AJo should have 36 combinations");
    // 3 hands * 12 offsuit combos each = 36
    ARP_FreeRange(&range);

    // Test with dead cards
    StdDeck_CardMask dead_with_ace;
    StdDeck_CardMask_RESET(dead_with_ace);
    StdDeck_CardMask_SET(dead_with_ace,
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

    TEST_ASSERT(ARP_ParseRange("AK-AJ", dead_with_ace, game_holdem, &range),
                "Should parse AK-AJ with dead As");
    TEST_ASSERT(range.count == 36,
                "AK-AJ with dead As should have 36 combinations");
    // Normal: 48.
    // AK: 16 - 4 (inv As) = 12
    // AQ: 16 - 4 (inv As) = 12
    // AJ: 16 - 4 (inv As) = 12
    // Total = 36
    ARP_FreeRange(&range);

    // Test validation
    TEST_ASSERT(ARP_ValidateRangeString("AK-AJ", NULL, 0), "AK-AJ should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("KQ-KT", NULL, 0), "KQ-KT should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AKs-AJs", NULL, 0), "AKs-AJs should be valid");
    TEST_ASSERT(ARP_ValidateRangeString("AKo-AJo", NULL, 0), "AKo-AJo should be valid");

    // Invalid cases
    TEST_ASSERT(!ARP_ValidateRangeString("AK-AK", NULL, 0), "AK-AK should be invalid (same hand)");
    TEST_ASSERT(!ARP_ValidateRangeString("AK-KJ", NULL, 0), "AK-KJ should be invalid (first rank changes)");
    TEST_ASSERT(!ARP_ValidateRangeString("AKs-AJo", NULL, 0), "AKs-AJo should be invalid (suit mismatch)");

    // Combined ranges
    TEST_ASSERT(ARP_ParseRange("AA-TT + AK-AJ", dead_cards, game_holdem, &range),
                "Should parse combined range");
    // 30 pairs + 48 AK-AJ = 78
    TEST_ASSERT(range.count == 78, "Should have 78 combinations");
    ARP_FreeRange(&range);

    // Subtraction
    TEST_ASSERT(ARP_ParseRange("AK-AJ - AKo", dead_cards, game_holdem, &range),
                "Should parse with subtraction");
    // 48 - 12 = 36
    TEST_ASSERT(range.count == 36, "Should have 36 combinations");
    ARP_FreeRange(&range);

    TEST_PASS("Non-pair range tests");
}

/* Test suited/offsuit hands */
static int test_suited_offsuit(void)
{
    printf("\n--- Testing Suited/Offsuit Hands ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    /* Test suited hand */
    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AKs", dead_cards, game_holdem, &range), "Should parse AKs");
    TEST_ASSERT(range.count == 4, "AKs should have 4 combinations"); /* 4 suits */
    ARP_FreeRange(&range);

    /* Test offsuit hand */
    TEST_ASSERT(ARP_ParseRange("AKo", dead_cards, game_holdem, &range), "Should parse AKo");
    TEST_ASSERT(range.count == 12, "AKo should have 12 combinations"); /* 4*3 offsuit */
    ARP_FreeRange(&range);

    /* Test both suited and offsuit */
    TEST_ASSERT(ARP_ParseRange("AK", dead_cards, game_holdem, &range), "Should parse AK");
    TEST_ASSERT(range.count == 16, "AK should have 16 combinations"); /* 4 + 12 */
    ARP_FreeRange(&range);

    TEST_PASS("Suited/offsuit tests");
}

/* Test specific hands */
static int test_specific_hands(void)
{
    printf("\n--- Testing Specific Hands ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    /* Test specific hand */
    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AsKh", dead_cards, game_holdem, &range), "Should parse AsKh");
    TEST_ASSERT(range.count == 1, "AsKh should have 1 combination");
    ARP_FreeRange(&range);

    /* Test multiple specific hands */
    TEST_ASSERT(ARP_ParseRange("AsKh, AdQd", dead_cards, game_holdem, &range), "Should parse AsKh, AdQd");
    TEST_ASSERT(range.count == 2, "AsKh, AdQd should have 2 combinations");
    ARP_FreeRange(&range);

    TEST_PASS("Specific hand tests");
}

/* Test combinations */
static int test_combinations(void)
{
    printf("\n--- Testing Range Combinations ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    /* Test comma-separated ranges */
    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AA, KK, QQ", dead_cards, game_holdem, &range), "Should parse AA, KK, QQ");
    TEST_ASSERT(range.count == 18, "AA, KK, QQ should have 18 combinations"); /* 3 * 6 */
    ARP_FreeRange(&range);

    /* Test mixed types */
    TEST_ASSERT(ARP_ParseRange("AA, AKs", dead_cards, game_holdem, &range), "Should parse AA, AKs");
    TEST_ASSERT(range.count == 10, "AA, AKs should have 10 combinations"); /* 6 + 4 */
    ARP_FreeRange(&range);

    TEST_PASS("Range combination tests");
}

/* Test dead cards */
static int test_dead_cards(void)
{
    printf("\n--- Testing Dead Cards ---\n");

    /* Set up dead cards (As, Kh) */
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    /* Test that dead cards are excluded */
    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AA", dead_cards, game_holdem, &range), "Should parse AA with dead As");
    TEST_ASSERT(range.count == 3, "AA with dead As should have 3 combinations"); /* 6 - 3 (involving As) */
    ARP_FreeRange(&range);

    /* Test specific hand with dead card */
    TEST_ASSERT(ARP_ParseRange("AsKh", dead_cards, game_holdem, &range), "Should parse AsKh with dead cards");
    TEST_ASSERT(range.count == 0, "AsKh with dead As and Kh should have 0 combinations");
    ARP_FreeRange(&range);

    TEST_PASS("Dead card tests");
}

/* Test PlayerRange conversion */
static int test_player_range_conversion(void)
{
    printf("\n--- Testing PlayerRange Conversion ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AA, KK", dead_cards, game_holdem, &range), "Should parse AA, KK");

    PlayerRange player_range;
    TEST_ASSERT(ARP_ToPlayerRange(&range, &player_range), "Should convert to PlayerRange");
    TEST_ASSERT(player_range.count == range.count, "PlayerRange count should match");
    TEST_ASSERT(player_range.hand_masks == range.hands, "PlayerRange should point to same hands");
    TEST_ASSERT(player_range.weights == NULL, "Uniform range should have NULL weights pointer");

    ARP_FreeRange(&range);

    TEST_PASS("PlayerRange conversion tests");
}

/* Test utility functions */
static int test_utilities(void)
{
    printf("\n--- Testing Utility Functions ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AA", dead_cards, game_holdem, &range), "Should parse AA");

    /* Test range to string */
    char buffer[256];
    int len = ARP_RangeToString(&range, buffer, sizeof(buffer));
    TEST_ASSERT(len > 0, "Should generate string representation");
    TEST_ASSERT(strstr(buffer, "6 hands") != NULL, "Should mention 6 hands");

    /* Test percentage calculation */
    float percentage = ARP_GetRangePercentage(&range, game_holdem);
    TEST_ASSERT(percentage > 0.0f && percentage < 1.0f, "Should return valid percentage");

    ARP_FreeRange(&range);

    TEST_PASS("Utility function tests");
}

/* Test percentage ranges */
static int test_percentage_ranges(void)
{
    printf("\n--- Testing Percentage Ranges ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    /* Test small percentage */
    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("5%", dead_cards, game_holdem, &range), "Should parse 5%");
    TEST_ASSERT(range.is_percentage, "Should be marked as percentage range");
    TEST_ASSERT(range.percentage_used > 0.04f && range.percentage_used < 0.06f, "Should store correct percentage");
    TEST_ASSERT(range.count > 0, "Should have some hands");
    printf("   5%% range contains %zu hands\n", range.count);
    ARP_FreeRange(&range);

    /* Test larger percentage */
    TEST_ASSERT(ARP_ParseRange("20%", dead_cards, game_holdem, &range), "Should parse 20%");
    TEST_ASSERT(range.is_percentage, "Should be marked as percentage range");
    TEST_ASSERT(range.percentage_used > 0.19f && range.percentage_used < 0.21f, "Should store correct percentage");
    TEST_ASSERT(range.count > 0, "Should have some hands");
    printf("   20%% range contains %zu hands\n", range.count);
    ARP_FreeRange(&range);

    /* Test decimal percentage */
    TEST_ASSERT(ARP_ParseRange("5.5%", dead_cards, game_holdem, &range), "Should parse 5.5%");
    TEST_ASSERT(range.is_percentage, "Should be marked as percentage range");
    TEST_ASSERT(range.percentage_used > 0.054f && range.percentage_used < 0.056f, "Should store correct percentage");
    TEST_ASSERT(range.count > 0, "Should have some hands");
    printf("   5.5%% range contains %zu hands\n", range.count);
    ARP_FreeRange(&range);

    /* Test that larger percentages contain more hands */
    arp_range_t range5, range20;
    TEST_ASSERT(ARP_ParseRange("5%", dead_cards, game_holdem, &range5), "Should parse 5%");
    TEST_ASSERT(ARP_ParseRange("20%", dead_cards, game_holdem, &range20), "Should parse 20%");
    TEST_ASSERT(range20.count > range5.count, "20% should contain more hands than 5%");
    ARP_FreeRange(&range5);
    ARP_FreeRange(&range20);

    TEST_PASS("Percentage range tests");
}

/* Test operator expressions */
static int test_operator_expressions(void)
{
    printf("\n--- Testing Operator Expressions ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    arp_range_t range;

    /* Test Addition (+) */
    TEST_ASSERT(ARP_ParseRange("AA + KK", dead_cards, game_holdem, &range), "Should parse AA + KK");
    TEST_ASSERT(range.count == 12, "AA + KK should have 12 combinations");
    ARP_FreeRange(&range);

    /* Test Subtraction (-) */
    TEST_ASSERT(ARP_ParseRange("AA-TT - QQ", dead_cards, game_holdem, &range), "Should parse AA-TT - QQ");
    TEST_ASSERT(range.count == 24, "AA-TT - QQ should have 24 combinations"); /* 30 - 6 */
    ARP_FreeRange(&range);

    /* Test Exclusion (!) */
    TEST_ASSERT(ARP_ParseRange("!AA", dead_cards, game_holdem, &range), "Should parse !AA");
    TEST_ASSERT(range.count == 1320, "!AA should have 1320 combinations"); /* 1326 - 6 */
    ARP_FreeRange(&range);

    /* Test Complex Expression with Parentheses */
    TEST_ASSERT(ARP_ParseRange("(AA,KK) - (AKs,AQo)", dead_cards, game_holdem, &range), "Should parse (AA,KK) - (AKs,AQo)");
    TEST_ASSERT(range.count == 12, "(AA,KK) - (AKs,AQo) should have 12 combinations");
    ARP_FreeRange(&range);

    TEST_ASSERT(ARP_ParseRange("20% - (AA,KK)", dead_cards, game_holdem, &range), "Should parse 20% - (AA,KK)");
    TEST_ASSERT(range.count > 0, "Should have some hands after subtraction from percentage");
    ARP_FreeRange(&range);

    /* Test Regression: Parentheses vs Stud Pattern */
    /* (AA) should be treated as expression, not Stud pattern */
    TEST_ASSERT(ARP_ParseRange("(AA) + KK", dead_cards, game_holdem, &range), "Should parse (AA) + KK");
    TEST_ASSERT(range.count == 12, "(AA) + KK should have 12 combinations");
    ARP_FreeRange(&range);

    TEST_PASS("Operator expression tests");
}

static int test_weighted_and_exclusion(void)
{
    printf("\n--- Testing Weighted Ranges & Exclusions ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t range;
    TEST_ASSERT(ARP_ParseRange("AKo{50%}, AKs{50%}", dead_cards, game_holdem, &range),
                "Should parse weighted range");
    TEST_ASSERT(range.has_weights, "Weighted range should flag has_weights");

    double sum_total = 0.0;
    double sum_suited = 0.0;
    double sum_offsuit = 0.0;

    for (size_t i = 0; i < range.count; ++i)
    {
        double w = range.weights ? range.weights[i] : 1.0;
        sum_total += w;

        int cards[2];
        int idx = 0;
        for (int card = 0; card < StdDeck_N_CARDS && idx < 2; ++card)
        {
            if (StdDeck_CardMask_CARD_IS_SET(range.hands[i], card))
            {
                cards[idx++] = card;
            }
        }
        TEST_ASSERT(idx == 2, "Each hand should contain exactly 2 cards");

        int suit0 = StdDeck_SUIT(cards[0]);
        int suit1 = StdDeck_SUIT(cards[1]);
        if (suit0 == suit1)
            sum_suited += w;
        else
            sum_offsuit += w;
    }

    TEST_ASSERT(fabs(sum_total - 1.0) < 1e-6, "Total weight should normalize to 1");
    TEST_ASSERT(fabs(sum_suited - 0.5) < 1e-6, "Suited combos should sum to 0.5");
    TEST_ASSERT(fabs(sum_offsuit - 0.5) < 1e-6, "Offsuit combos should sum to 0.5");
    ARP_FreeRange(&range);

    /* @ weight syntax: AA@100 is equivalent to AA{100} */
    arp_range_t at_range;
    TEST_ASSERT(ARP_ParseRange("AA@100, KK@50", dead_cards, game_holdem, &at_range),
                "Should parse @ weighted range");
    TEST_ASSERT(at_range.has_weights, "@ weighted range should flag has_weights");
    TEST_ASSERT(at_range.count == 12, "@ weighted range should contain 12 combos");

    double at_aa = 0.0, at_kk = 0.0;
    for (size_t i = 0; i < at_range.count; ++i)
    {
        double w = at_range.weights[i];
        char buf[8];
        StdDeck_maskToString(at_range.hands[i], buf);
        if (buf[0] == 'A' && buf[1] == 'A') at_aa += w;
        else if (buf[0] == 'K' && buf[1] == 'K') at_kk += w;
    }
    /* AA should weigh twice KK after normalization */
    TEST_ASSERT(fabs(at_aa - 2.0 * at_kk) < 1e-9, "AA@100 should weigh double KK@50");
    ARP_FreeRange(&at_range);

    /* Mixed @ and brace weights together */
    arp_range_t mix_range;
    TEST_ASSERT(ARP_ParseRange("AA{60%}, KK@25%, AKo@15%", dead_cards, game_holdem, &mix_range),
                "Should parse mixed brace and @ weights");
    TEST_ASSERT(mix_range.has_weights, "Mixed weight range should flag has_weights");
    TEST_ASSERT(mix_range.count == 24, "Mixed weight range should contain 24 combos");
    ARP_FreeRange(&mix_range);

    /* @ weight must be rejected when followed by a non-delimiter */
    TEST_ASSERT(!ARP_ParseRange("AA@25x", dead_cards, game_holdem, &mix_range),
                "@ weight followed by junk should be rejected");

    /* Matrix export */
    arp_range_t matrix_range;
    TEST_ASSERT(ARP_ParseRange("AA@60%, KK@25%, AKo@15%", dead_cards, game_holdem, &matrix_range),
                "Should parse range for matrix export");
    FILE *mem = tmpfile();
    TEST_ASSERT(mem != NULL, "Should create temp file for matrix export");
    TEST_ASSERT(ARP_ExportRangeMatrix(&matrix_range, mem) == 1, "Matrix export should succeed");
    fflush(mem);
    rewind(mem);

    char line[256];
    int found_pair = 0, found_offsuit = 0;
    while (fgets(line, sizeof(line), mem))
    {
        /* A row diagonal should show 60.0 for AA, 25.0 for KK */
        if (strncmp(line, " A |", 4) == 0 && strstr(line, "60.0"))
            found_pair = 1;
        /* K row offsuit cell (AKo) should show 15.0 */
        if (strncmp(line, " K |", 4) == 0 && strstr(line, "15.0"))
            found_offsuit = 1;
    }
    fclose(mem);
    TEST_ASSERT(found_pair, "Matrix should show 60.0 in the A diagonal (AA)");
    TEST_ASSERT(found_offsuit, "Matrix should show 15.0 in the K row (AK offsuit)");
    ARP_FreeRange(&matrix_range);

    TEST_ASSERT(ARP_ParseRange("AA, !AsAh", dead_cards, game_holdem, &range),
                "Should parse exclusion in range");
    TEST_ASSERT(range.count == 5, "AA minus AsAh should leave 5 combos");
    ARP_FreeRange(&range);

    TEST_PASS("Weighted range and exclusion tests");
}

static int test_plo_categories(void)
{
    printf("\n--- Testing PLO Categories ---\n");

    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);

    arp_range_t pattern_range;
    TEST_ASSERT(ARP_ParseRange("AAxxds", dead_cards, game_omaha, &pattern_range),
                "Should parse AAxxds pattern");
    TEST_ASSERT(pattern_range.count > 0, "Pattern should expand to hands");

    arp_range_t category_range;
    TEST_ASSERT(ARP_ParseRange("cat:aa_ds", dead_cards, game_omaha, &category_range),
                "Should parse AA double-suited category");
    TEST_ASSERT(category_range.count > 0, "AA double-suited category should expand to hands");
    TEST_ASSERT(category_range.count == pattern_range.count,
                "AA category should match AAxxds pattern expansion");

    for (size_t i = 0; i < category_range.count; ++i)
    {
        bool found = false;
        for (size_t j = 0; j < pattern_range.count; ++j)
        {
            if (StdDeck_CardMask_EQUAL(category_range.hands[i], pattern_range.hands[j]))
            {
                found = true;
                break;
            }
        }
        TEST_ASSERT(found, "Category hand should be present in AAxxds expansion");
    }

    arp_range_t alias_range;
    TEST_ASSERT(ARP_ParseRange("CAT:AA-DS", dead_cards, game_omaha, &alias_range),
                "Alias casing should parse");
    TEST_ASSERT(alias_range.count == category_range.count,
                "Alias spelling should match base category count");

    StdDeck_CardMask dead_with_ace;
    StdDeck_CardMask_RESET(dead_with_ace);
    StdDeck_CardMask_SET(dead_with_ace, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

    arp_range_t filtered_range;
    TEST_ASSERT(ARP_ParseRange("cat:aa_ds", dead_with_ace, game_omaha, &filtered_range),
                "Category parsing should respect dead cards");
    TEST_ASSERT(filtered_range.count < category_range.count,
                "Dead ace should reduce AA double-suited combinations");

    arp_range_t unpaired_ds;
    TEST_ASSERT(ARP_ParseRange("cat:unpaired_ds", dead_cards, game_omaha, &unpaired_ds),
                "Should parse unpaired double-suited category");
    TEST_ASSERT(unpaired_ds.count > 0, "Unpaired double-suited category should produce hands");

    ARP_FreeRange(&pattern_range);
    ARP_FreeRange(&category_range);
    ARP_FreeRange(&alias_range);
    ARP_FreeRange(&filtered_range);
    ARP_FreeRange(&unpaired_ds);

    TEST_PASS("PLO category tests");
}

/*
 * Regression tests for the range dedup hash table.
 *
 * The table used to fold the four suit fields together with XOR, which is
 * invariant under suit permutation, and to give up after 20 linear probes.
 * Suit-permuted Omaha hands therefore piled onto a few buckets, overran the
 * probe limit and slipped past the duplicate check.
 */
static int test_hash_dedup(void)
{
    printf("\n--- Testing Range Dedup (hash table) ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    arp_range_t a, b, u;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&u, 0, sizeof(u));

    TEST_ASSERT(ARP_ParseOmahaRange("AAxxds", dead, game_omaha, &a), "parse AAxxds");
    TEST_ASSERT(ARP_ParseOmahaRange("KKxxds", dead, game_omaha, &b), "parse KKxxds");
    TEST_ASSERT(ARP_ParseOmahaRange("AAxxds + KKxxds", dead, game_omaha, &u),
                "parse AAxxds + KKxxds");

    /* The two sets overlap in exactly the 6 double-suited AAKK hands (one per
     * pair of suits), so the union holds 864 + 864 - 6 = 1722 hands. */
    TEST_ASSERT(a.count == 864, "AAxxds should hold 864 hands");
    TEST_ASSERT(b.count == 864, "KKxxds should hold 864 hands");
    TEST_ASSERT(u.count == 1722, "AAxxds + KKxxds should hold 1722 hands, not 1727");

    /* No hand may appear twice in the union. */
    size_t dups = 0;
    for (size_t i = 0; i < u.count; i++)
        for (size_t j = 0; j < i; j++)
            if (StdDeck_CardMask_EQUAL(u.hands[i], u.hands[j]))
            {
                dups++;
                break;
            }
    TEST_ASSERT(dups == 0, "union must not contain duplicate hands");

    /* Every hand of both operands must be reachable through the public lookup;
     * this is what returned false negatives once a probe chain got long. */
    for (size_t i = 0; i < a.count; i++)
        TEST_ASSERT(ARP_ContainsHand(&u, a.hands[i]), "union contains each AAxxds hand");
    for (size_t i = 0; i < b.count; i++)
        TEST_ASSERT(ARP_ContainsHand(&u, b.hands[i]), "union contains each KKxxds hand");

    ARP_FreeRange(&a);
    ARP_FreeRange(&b);
    ARP_FreeRange(&u);

    TEST_PASS("Hash dedup tests");
}

/*
 * The hash table stores positions into range->hands. Removing a hand shifts
 * the entries below it, so the table has to be dropped — otherwise
 * ARP_ContainsHand keeps reporting removed hands as present.
 */
static int test_contains_after_removal(void)
{
    printf("\n--- Testing ARP_ContainsHand After Removal ---\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    arp_range_t full, removed;
    memset(&full, 0, sizeof(full));
    memset(&removed, 0, sizeof(removed));

    TEST_ASSERT(ARP_ParseOmahaRange("AAxxds", dead, game_omaha, &full), "parse AAxxds");
    TEST_ASSERT(ARP_ParseOmahaRange("AAxxds - AAxxds", dead, game_omaha, &removed),
                "parse AAxxds - AAxxds");

    TEST_ASSERT(removed.count == 0, "subtracting a range from itself empties it");
    for (size_t i = 0; i < full.count; i++)
        TEST_ASSERT(!ARP_ContainsHand(&removed, full.hands[i]),
                    "removed hands must not be reported as present");

    ARP_FreeRange(&full);
    ARP_FreeRange(&removed);

    TEST_PASS("Contains-after-removal tests");
}

/* Main test runner */
int main(void)
{
    printf("🧪 Advanced Range Parser Unit Tests\n");
    printf("===================================\n");

    int tests_passed = 0;
    int total_tests = 0;

    /* Run all tests */
    total_tests++;
    if (test_validation())
        tests_passed++;
    total_tests++;
    if (test_pocket_pairs())
        tests_passed++;
    total_tests++;
    if (test_nonpropair_ranges())
        tests_passed++;
    total_tests++;
    if (test_suited_offsuit())
        tests_passed++;
    total_tests++;
    if (test_specific_hands())
        tests_passed++;
    total_tests++;
    if (test_combinations())
        tests_passed++;
    total_tests++;
    if (test_dead_cards())
        tests_passed++;
    total_tests++;
    if (test_player_range_conversion())
        tests_passed++;
    total_tests++;
    if (test_utilities())
        tests_passed++;
    total_tests++;
    if (test_percentage_ranges())
        tests_passed++;
    total_tests++;
    if (test_operator_expressions())
        tests_passed++;
    total_tests++;
    if (test_weighted_and_exclusion())
        tests_passed++;
    total_tests++;
    if (test_plo_categories())
        tests_passed++;
    total_tests++;
    if (test_hash_dedup())
        tests_passed++;
    total_tests++;
    if (test_contains_after_removal())
        tests_passed++;

    /* Summary */
    printf("\n📊 Test Results\n");
    printf("===============\n");
    printf("Tests passed: %d/%d\n", tests_passed, total_tests);

    if (tests_passed == total_tests)
    {
        printf("🎉 All tests passed!\n");
        return 0;
    }
    else
    {
        printf("❌ Some tests failed!\n");
        return 1;
    }
}

/*
 * TODO: Add tests for Phase 2 features:
 * - Extended ranges (AK-AJ)
 * - Omaha/PLO patterns
 */
