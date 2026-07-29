#include <poker_eval/range.h>
#include <poker_eval/equity.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void test_holdem_range(void) {
    pe_range_t *range;
    pe_status_t status;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    printf("Testing Hold'em Range Parsing...\n");
    status = pe_range_parse(game_holdem, "AA, KK", dead, NULL, &range);
    if (status != PE_STATUS_OK) {
        printf("Failed to parse AA, KK\n");
        exit(1);
    }
    printf("Parsed AA, KK: count=%zu\n", range->count);
    assert(range->count == 12); // 6 combos of AA + 6 combos of KK
    pe_range_free(range);

    status = pe_range_parse(game_holdem, "20%", dead, NULL, &range);
    if (status != PE_STATUS_OK) {
        printf("Failed to parse 20%%\n");
        exit(1);
    }
    printf("Parsed 20%%: count=%zu\n", range->count);
    pe_range_free(range);
}

static void test_stud_range(void) {
    pe_range_t *range;
    pe_status_t status;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    printf("Testing Stud Range Parsing (New Syntax)...\n");
    // Test (AA)K
    status = pe_range_parse(game_7stud, "(AA)K", dead, NULL, &range);
    if (status != PE_STATUS_OK) {
        printf("Failed to parse (AA)K\n");
        exit(1);
    }
    printf("Parsed (AA)K: count=%zu\n", range->count);
    pe_range_free(range);

    // Test A23 (Triplet syntax)
    status = pe_range_parse(game_7stud, "A23", dead, NULL, &range);
    if (status != PE_STATUS_OK) {
        printf("Failed to parse A23\n");
        exit(1);
    }
    printf("Parsed A23: count=%zu\n", range->count);
    // A23 means (A2)3 -> 4*4*4 = 64 combos? Order matters? Stud is (h1 h2) u1.
    // If A23 means ranks A,2,3...
    // hole cards A,2 (16 combos) upcard 3 (4 combos) => 64.
    assert(range->count == 64);
    pe_range_free(range);
}

static void test_equity_calc(void) {
    printf("Testing Equity Calculation...\n");
    pe_range_t *r1, *r2;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    pe_range_parse(game_holdem, "KK", dead, NULL, &r2);

    const pe_range_t *ranges[] = {r1, r2};
    pe_equity_result_multi_t result;
    memset(&result, 0, sizeof(result));

    pe_equity_opts_t opts = {0};
    opts.is_monte_carlo = 1;
    opts.iterations = 30000;
    pe_status_t status = pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
    if (status != PE_STATUS_OK) {
        printf("Equity calculation failed (status=%d)\n", status);
        exit(1);
    }

    printf("AA vs KK Equity: P1=%.3f P2=%.3f\n",
           result.results[0].equity, result.results[1].equity);
    assert(result.results[0].equity > 0.80);
    assert(result.results[1].equity < 0.20);

    pe_range_free(r1);
    pe_range_free(r2);
}

int main(void) {
    test_holdem_range();
    test_stud_range();
    test_equity_calc();
    printf("All tests passed!\n");
    return 0;
}
