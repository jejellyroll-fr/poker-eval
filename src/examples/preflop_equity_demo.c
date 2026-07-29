/*
 * preflop_equity_demo.c - Pre-Flop Equity Demonstration
 *
 * Demonstrates:
 * - Hand canonicalization
 * - Range parsing
 * - Equity calculation
 * - Lookup table usage
 *
 * Usage:
 *   ./preflop_equity_demo "AA,KK,QQ" "JJ+,AKs"
 */

#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/core/enumdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_hand(preflop_hand_t hand)
{
    char str[8];
    preflop_hand_to_string(hand, str);
    printf("%s", str);
}

static void print_range(const preflop_range_t *range)
{
    int count = 0;
    for (int h = 0; h < PREFLOP_NUM_CANONICAL_HANDS && count < 10; h++)
    {
        if (preflop_range_contains(range, h))
        {
            if (count > 0)
                printf(", ");
            print_hand(h);
            count++;
        }
    }
    if (range->num_hands > 10)
    {
        printf(", ... (%d total)", range->num_hands);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("Usage: %s <range1> <range2>\n", argv[0]);
        printf("\nExamples:\n");
        printf("  %s \"AA,KK,QQ\" \"JJ,TT,99\"\n", argv[0]);
        printf("  %s \"AA,AKs,AKo\" \"KK,AQs\"\n", argv[0]);
        return 1;
    }

    const char *range1_str = argv[1];
    const char *range2_str = argv[2];

    printf("=== Pre-Flop Equity Demo ===\n\n");

    /* Parse ranges */
    printf("Parsing ranges...\n");
    preflop_range_t range1, range2;

    if (preflop_range_parse(range1_str, &range1) < 0)
    {
        fprintf(stderr, "Error parsing range 1: %s\n", range1_str);
        return 1;
    }

    if (preflop_range_parse(range2_str, &range2) < 0)
    {
        fprintf(stderr, "Error parsing range 2: %s\n", range2_str);
        return 1;
    }

    printf("Range 1: ");
    print_range(&range1);
    printf("\n");

    printf("Range 2: ");
    print_range(&range2);
    printf("\n\n");

    /* Count combinations */
    int combos1 = preflop_range_count_combinations(&range1);
    int combos2 = preflop_range_count_combinations(&range2);

    printf("Range 1: %d canonical hands, %d combinations\n", range1.num_hands, combos1);
    printf("Range 2: %d canonical hands, %d combinations\n", range2.num_hands, combos2);
    printf("Total matchups: %d × %d = %d\n\n",
           range1.num_hands, range2.num_hands,
           range1.num_hands * range2.num_hands);

    /* Calculate equity (placeholder for now) */
    printf("Calculating equity...\n");

    preflop_equity_input_t input = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0 /* Exhaustive */
    };

    preflop_equity_result_t result;
    int ret = preflop_equity_calculate(&input, &result);
    if (ret < 0)
    {
        fprintf(stderr, "Error calculating equity\n");
        return 1;
    }

    printf("\n=== Results ===\n");
    printf("Range 1 equity: %.2f%%\n", result.equity1 * 100);
    printf("Range 2 equity: %.2f%%\n", result.equity2 * 100);
    printf("Tie equity:     %.2f%%\n", result.tie_equity * 100);
    printf("Total boards evaluated: %llu\n", (unsigned long long)result.num_matchups);

    preflop_equity_result_free(&result);

    return 0;
}
