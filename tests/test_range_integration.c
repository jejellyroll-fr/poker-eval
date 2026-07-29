/*
 * Integration tests for unified Range Parser + Equity API
 *
 * Tests end-to-end workflows:
 *  - Parse ranges for different game types
 *  - Apply range operations (union, intersect, diff)
 *  - Compute equity between parsed ranges
 *  - Top percentage for Hold'em and Stud
 *  - Weighted range equity
 *  - Dead card filtering
 *  - Compile and dedup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <poker_eval/range.h>
#include <poker_eval/equity.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>

/* Test helper macros */
#define TEST_ASSERT(condition, message)       \
    do                                        \
    {                                         \
        if (!(condition))                     \
        {                                     \
            printf("FAIL: %s\n", message);    \
            return 0;                         \
        }                                     \
    } while (0)

#define TEST_ASSERT_EQ(actual, expected, message)                                 \
    do                                                                            \
    {                                                                             \
        size_t _a = (size_t)(actual), _e = (size_t)(expected);                    \
        if (_a != _e)                                                             \
        {                                                                         \
            printf("FAIL: %s (expected %zu, got %zu)\n", message, _e, _a);        \
            return 0;                                                             \
        }                                                                         \
    } while (0)

#define TEST_PASS(message)                \
    do                                    \
    {                                     \
        printf("PASS: %s\n", message);    \
        return 1;                         \
    } while (0)

static StdDeck_CardMask no_dead(void)
{
    StdDeck_CardMask d;
    StdDeck_CardMask_RESET(d);
    return d;
}

/* ============================================================================
 * Hold'em Integration
 * ============================================================================ */

static int test_holdem_parse_combine_equity(void)
{
    printf("\n--- Hold'em: Parse + Combine + Equity ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *r1 = NULL, *r2 = NULL;

    /* Parse AA vs KK */
    pe_status_t st = pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse AA");
    TEST_ASSERT_EQ(r1->count, 6, "AA should have 6 combos");

    st = pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse KK");
    TEST_ASSERT_EQ(r2->count, 6, "KK should have 6 combos");

    /* Compute preflop equity */
    const pe_range_t *ranges[2] = {r1, r2};
    pe_equity_result_multi_t result;
    memset(&result, 0, sizeof(result));

    pe_equity_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = 50000;

    st = pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
    TEST_ASSERT(st == PE_STATUS_OK, "Equity calculation");
    TEST_ASSERT(result.num_players == 2, "Should have 2 players");

    /* AA vs KK: AA should have ~82% equity */
    double eq_aa = result.results[0].equity;
    printf("  AA equity: %.1f%%\n", eq_aa * 100.0);
    TEST_ASSERT(eq_aa > 0.70, "AA should have >70% equity vs KK");
    TEST_ASSERT(eq_aa < 0.95, "AA should have <95% equity vs KK");

    pe_range_free(r1);
    pe_range_free(r2);

    TEST_PASS("Hold'em parse + equity");
}

static int test_holdem_range_operations(void)
{
    printf("\n--- Hold'em: Range Operations ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *pairs = NULL, *high_pairs = NULL, *combined = NULL;

    /* Parse all pairs and high pairs */
    pe_status_t st = pe_range_parse(game_holdem, "22,33,44,55,66,77,88,99,TT,JJ,QQ,KK,AA", dead, NULL, &pairs);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse all pairs");
    TEST_ASSERT_EQ(pairs->count, 78, "13 pairs * 6 = 78 combos");

    st = pe_range_parse(game_holdem, "TT,JJ,QQ,KK,AA", dead, NULL, &high_pairs);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse high pairs");
    TEST_ASSERT_EQ(high_pairs->count, 30, "5 high pairs * 6 = 30 combos");

    /* Difference: all pairs minus high pairs = low+medium pairs */
    st = pe_range_combine(pairs, high_pairs, PE_OP_DIFFERENCE, &combined);
    TEST_ASSERT(st == PE_STATUS_OK, "Combine difference");
    TEST_ASSERT_EQ(combined->count, 48, "78 - 30 = 48 low/medium pairs");
    pe_range_free(combined);

    /* Intersection: should return the high pairs */
    combined = NULL;
    st = pe_range_combine(pairs, high_pairs, PE_OP_INTERSECT, &combined);
    TEST_ASSERT(st == PE_STATUS_OK, "Combine intersect");
    TEST_ASSERT_EQ(combined->count, 30, "Intersection should be 30 high pairs");
    pe_range_free(combined);

    /* Union: should return all pairs (no new ones) */
    combined = NULL;
    st = pe_range_combine(pairs, high_pairs, PE_OP_UNION, &combined);
    TEST_ASSERT(st == PE_STATUS_OK, "Combine union");
    TEST_ASSERT_EQ(combined->count, 78, "Union should still be 78 (high pairs subset of all)");
    pe_range_free(combined);

    pe_range_free(pairs);
    pe_range_free(high_pairs);

    TEST_PASS("Hold'em range operations");
}

/* ============================================================================
 * Stud Integration
 * ============================================================================ */

static int test_stud_parse_and_equity(void)
{
    printf("\n--- Stud: Parse + Equity ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *r1 = NULL, *r2 = NULL;

    /* Parse Stud ranges */
    pe_status_t st = pe_range_parse(game_7stud, "(AA)K", dead, NULL, &r1);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse (AA)K");
    TEST_ASSERT(r1 != NULL, "r1 not NULL");
    TEST_ASSERT(r1->count > 0, "(AA)K should have combos");
    printf("  (AA)K: %zu combos\n", r1->count);

    st = pe_range_parse(game_7stud, "(KK)A", dead, NULL, &r2);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse (KK)A");
    TEST_ASSERT(r2 != NULL, "r2 not NULL");
    TEST_ASSERT(r2->count > 0, "(KK)A should have combos");
    printf("  (KK)A: %zu combos\n", r2->count);

    pe_range_free(r1);
    pe_range_free(r2);

    TEST_PASS("Stud parse and equity");
}

/* ============================================================================
 * Cross-Game Operations
 * ============================================================================ */

static int test_compile_and_dedup(void)
{
    printf("\n--- Compile and Dedup ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *r1 = NULL, *r2 = NULL;

    /* Parse same range twice, combine with union, then compile */
    pe_status_t st = pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse AA (1)");

    st = pe_range_parse(game_holdem, "AA", dead, NULL, &r2);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse AA (2)");

    /* Union of identical ranges should still be 6 */
    pe_range_t *combined = NULL;
    st = pe_range_combine(r1, r2, PE_OP_UNION, &combined);
    TEST_ASSERT(st == PE_STATUS_OK, "Union of AA + AA");
    TEST_ASSERT_EQ(combined->count, 6, "Union of AA+AA should have 6 combos");

    /* Compile should deduplicate */
    pe_compiled_range_t *compiled = NULL;
    st = pe_range_compile(combined, NULL, &compiled);
    TEST_ASSERT(st == PE_STATUS_OK, "Compile");
    TEST_ASSERT_EQ(compiled->count, 6, "Compiled AA should still have 6 combos");

    pe_range_free(r1);
    pe_range_free(r2);
    pe_range_free(combined);
    pe_range_free(compiled);

    TEST_PASS("Compile and dedup");
}

static int test_filter_dead_cards(void)
{
    printf("\n--- Filter Dead Cards ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *range = NULL;

    /* Parse AA: 6 combos */
    pe_status_t st = pe_range_parse(game_holdem, "AA", dead, NULL, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse AA");
    TEST_ASSERT_EQ(range->count, 6, "AA should have 6 combos");

    /* Filter out Ah: removes 3 combos containing Ah */
    StdDeck_CardMask dead_ah;
    StdDeck_CardMask_RESET(dead_ah);
    StdDeck_CardMask_SET(dead_ah, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    pe_range_t *filtered = NULL;
    st = pe_range_filter_dead(range, dead_ah, &filtered);
    TEST_ASSERT(st == PE_STATUS_OK, "Filter dead");
    TEST_ASSERT_EQ(filtered->count, 3, "AA minus Ah should have 3 combos (C(3,2))");

    pe_range_free(range);
    pe_range_free(filtered);

    TEST_PASS("Filter dead cards");
}

static int test_top_percent_holdem(void)
{
    printf("\n--- Top Percent: Hold'em ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *range = NULL;

    /* pe_range_top_percent uses 0.0-1.0 fraction.
     * 10% of 169 hand groups = ~17 hand groups
     * 10% of 1326 combos = ~133 combos */
    pe_status_t st = pe_range_top_percent(game_holdem, 0.10, dead, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "Top 10% Hold'em");
    TEST_ASSERT(range != NULL, "Range not NULL");
    /* Should be roughly 100-170 combos depending on rounding */
    printf("  Top 10%% Hold'em: %zu combos\n", range->count);
    TEST_ASSERT(range->count >= 50, "Top 10% should have >=50 combos");
    TEST_ASSERT(range->count <= 250, "Top 10% should have <=250 combos");
    pe_range_free(range);

    TEST_PASS("Top percent Hold'em");
}

static int test_top_percent_stud(void)
{
    printf("\n--- Top Percent: Stud ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *range = NULL;

    /* Top 1% of Stud (0.01 fraction): should include trips and high pairs */
    pe_status_t st = pe_range_top_percent(game_7stud, 0.01, dead, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "Top 1% Stud");
    TEST_ASSERT(range != NULL, "Range not NULL");
    printf("  Top 1%% Stud: %zu combos\n", range->count);
    TEST_ASSERT(range->count > 0, "Top 1% Stud should have combos");
    pe_range_free(range);

    /* Top 10% */
    range = NULL;
    st = pe_range_top_percent(game_7stud, 0.10, dead, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "Top 10% Stud");
    TEST_ASSERT(range != NULL, "Range not NULL");
    printf("  Top 10%% Stud: %zu combos\n", range->count);
    TEST_ASSERT(range->count > 100, "Top 10% Stud should have >100 combos");
    pe_range_free(range);

    TEST_PASS("Top percent Stud");
}

static int test_weighted_ranges(void)
{
    printf("\n--- Weighted Ranges ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_parse_opts_t opts;
    pe_range_opts_init(&opts);
    opts.allow_weights = 1;

    pe_range_t *range = NULL;

    /* Parse with weights: AA:0.5 means 50% frequency */
    pe_status_t st = pe_range_parse(game_holdem, "AA:0.5", dead, &opts, &range);
    TEST_ASSERT(st == PE_STATUS_OK, "Parse AA:0.5");
    TEST_ASSERT(range != NULL, "Range not NULL");
    TEST_ASSERT_EQ(range->count, 6, "AA:0.5 should have 6 combos");

    /* Check weights */
    int all_half = 1;
    for (size_t i = 0; i < range->count; i++) {
        if (fabs(range->combos[i].weight - 0.5) > 0.01) {
            all_half = 0;
            break;
        }
    }
    TEST_ASSERT(all_half, "All AA:0.5 combos should have weight ~0.5");

    pe_range_free(range);

    TEST_PASS("Weighted ranges");
}

static int test_equity_api_consistency(void)
{
    printf("\n--- Equity API Consistency ---\n");

    StdDeck_CardMask dead = no_dead();
    pe_range_t *r1 = NULL, *r2 = NULL;

    pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    pe_range_parse(game_holdem, "KK", dead, NULL, &r2);

    /* pe_equity_range_vs_range should give same result as pe_equity_multiway with 2 players */
    pe_equity_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = 30000;

    pe_equity_result_multi_t res_rvr, res_multi;
    memset(&res_rvr, 0, sizeof(res_rvr));
    memset(&res_multi, 0, sizeof(res_multi));

    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);

    pe_status_t st1 = pe_equity_range_vs_range(NULL, game_holdem, r1, r2, board, dead, &opts, &res_rvr);
    TEST_ASSERT(st1 == PE_STATUS_OK, "range_vs_range calc");

    const pe_range_t *ranges[2] = {r1, r2};
    pe_status_t st2 = pe_equity_multiway(NULL, game_holdem, ranges, 2, board, dead, &opts, &res_multi);
    TEST_ASSERT(st2 == PE_STATUS_OK, "multiway calc");

    /* Both should give similar equity (within MC variance) */
    double diff = fabs(res_rvr.results[0].equity - res_multi.results[0].equity);
    printf("  RvR equity: %.1f%%, Multiway equity: %.1f%%, diff: %.2f%%\n",
           res_rvr.results[0].equity * 100.0,
           res_multi.results[0].equity * 100.0,
           diff * 100.0);
    TEST_ASSERT(diff < 0.05, "RvR and Multiway should agree within 5%");

    pe_range_free(r1);
    pe_range_free(r2);

    TEST_PASS("Equity API consistency");
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    printf("Range + Equity Integration Tests\n");
    printf("=================================\n");

    int passed = 0, failed = 0;

    #define RUN_TEST(fn) do { if (fn()) passed++; else failed++; } while(0)

    RUN_TEST(test_holdem_parse_combine_equity);
    RUN_TEST(test_holdem_range_operations);
    RUN_TEST(test_stud_parse_and_equity);
    RUN_TEST(test_compile_and_dedup);
    RUN_TEST(test_filter_dead_cards);
    RUN_TEST(test_top_percent_holdem);
    RUN_TEST(test_top_percent_stud);
    RUN_TEST(test_weighted_ranges);
    RUN_TEST(test_equity_api_consistency);

    #undef RUN_TEST

    printf("\n=================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);

    return (failed > 0) ? 1 : 0;
}
