/*
 * preflop_with_table_demo.c - Demonstrate lookup table speedup
 *
 * Compares exhaustive calculation vs table lookup
 */

#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/core/enumdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("Usage: %s <range1> <range2> [table.dat]\n", argv[0]);
        printf("\nExamples:\n");
        printf("  %s \"AA,KK\" \"QQ,JJ\" holdem_preflop_169x169.dat\n", argv[0]);
        return 1;
    }

    const char *range1_str = argv[1];
    const char *range2_str = argv[2];
    const char *table_file = (argc >= 4) ? argv[3] : NULL;

    printf("=== Pre-flop Equity: Exhaustive vs Lookup Table ===\n\n");

    /* Parse ranges */
    preflop_range_t range1, range2;
    preflop_range_parse(range1_str, &range1);
    preflop_range_parse(range2_str, &range2);

    int combos1 = preflop_range_count_combinations(&range1);
    int combos2 = preflop_range_count_combinations(&range2);

    printf("Range 1: %s (%d canonical hands = %d combos)\n",
           range1_str, range1.num_hands, combos1);
    printf("Range 2: %s (%d canonical hands = %d combos)\n\n",
           range2_str, range2.num_hands, combos2);

    /* Load table if provided */
    preflop_lookup_table_t *table = NULL;
    if (table_file)
    {
        printf("Loading lookup table from %s...\n", table_file);
        table = preflop_lookup_table_load(table_file, game_holdem);
        if (!table)
        {
            fprintf(stderr, "Warning: Failed to load table, will use exhaustive calculation\n");
        }
        else
        {
            printf("✅ Table loaded successfully\n\n");
        }
    }

    /* Method 1: EXHAUSTIVE (slow) */
    printf("=== Method 1: Exhaustive Calculation ===\n");
    clock_t start = clock();

    preflop_equity_input_t input_exhaustive = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = NULL  /* Force exhaustive */
    };

    preflop_equity_result_t result_exhaustive;
    preflop_equity_calculate(&input_exhaustive, &result_exhaustive);

    clock_t end = clock();
    double time_exhaustive = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("Equity 1: %.2f%%\n", result_exhaustive.equity1 * 100);
    printf("Equity 2: %.2f%%\n", result_exhaustive.equity2 * 100);
    printf("Time: %.3f seconds\n", time_exhaustive);
    printf("Boards evaluated: %llu\n\n", (unsigned long long)result_exhaustive.num_matchups);

    /* Method 2: LOOKUP TABLE (fast) */
    if (table)
    {
        printf("=== Method 2: Lookup Table ===\n");
        start = clock();

        preflop_equity_input_t input_table = {
            .range1 = range1,
            .range2 = range2,
            .num_samples = 0,
            .lookup_table = table  /* Use table */
        };

        preflop_equity_result_t result_table;
        preflop_equity_calculate(&input_table, &result_table);

        end = clock();
        double time_table = ((double)(end - start)) / CLOCKS_PER_SEC;

        printf("Equity 1: %.2f%%\n", result_table.equity1 * 100);
        printf("Equity 2: %.2f%%\n", result_table.equity2 * 100);
        printf("Time: %.6f seconds\n", time_table);
        printf("Lookups: %d × %d = %d\n\n", range1.num_hands, range2.num_hands, 
               range1.num_hands * range2.num_hands);

        /* Compare results */
        printf("=== Comparison ===\n");
        double diff = fabs(result_exhaustive.equity1 - result_table.equity1);
        printf("Accuracy: %.4f%% difference\n", diff * 100);
        printf("Speedup: %.0fx faster\n", time_exhaustive / time_table);
        printf("\n");

        preflop_equity_result_free(&result_table);
    }
    else
    {
        printf("(No lookup table available - run ./generate_table.sh first)\n\n");
    }

    /* Cleanup */
    preflop_equity_result_free(&result_exhaustive);
    if (table)
        preflop_lookup_table_free(table);

    return 0;
}
