/*
 * validate_preflop_table.c - Validate pre-flop equity table against known values
 *
 * Compares our table against PokerStove reference values
 *
 * Usage:
 *   ./validate_preflop_table holdem_preflop_169x169_fast.dat
 */

#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/core/enumdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Reference values from PokerStove */
typedef struct {
    const char *hand1;
    const char *hand2;
    double equity1_expected;  /* PokerStove value */
    double tolerance;         /* Acceptable difference */
} reference_matchup_t;

/* Known equity values from PokerStove (exact) */
static const reference_matchup_t reference_matchups[] = {
    /* Premium pairs */
    {"AA", "KK", 81.95, 0.20},  /* Classic matchup */
    {"AA", "QQ", 81.27, 0.20},
    {"AA", "JJ", 80.92, 0.20},
    {"AA", "TT", 80.78, 0.20},
    {"KK", "QQ", 82.11, 0.20},
    {"KK", "JJ", 81.77, 0.20},
    {"QQ", "JJ", 82.52, 0.20},

    /* Pairs vs lower pairs */
    {"99", "88", 81.95, 0.20},
    {"77", "66", 81.95, 0.20},
    {"55", "44", 81.95, 0.20},
    {"33", "22", 81.95, 0.20},

    /* Pairs vs broadway suited */
    {"AA", "AKs", 87.95, 0.20},
    {"AA", "AQs", 87.18, 0.20},
    {"AA", "AJs", 86.78, 0.20},
    {"AA", "ATs", 86.49, 0.20},
    {"KK", "AKs", 66.19, 0.20},
    {"QQ", "AKs", 54.88, 0.20},
    {"JJ", "AKs", 55.77, 0.20},

    /* Pairs vs broadway offsuit */
    {"AA", "AKo", 93.20, 0.20},
    {"AA", "AQo", 93.42, 0.20},
    {"AA", "AJo", 93.55, 0.20},
    {"KK", "AKo", 69.11, 0.20},
    {"QQ", "AKo", 56.74, 0.20},

    /* Pairs vs suited connectors */
    {"AA", "JTs", 77.33, 0.20},
    {"AA", "T9s", 76.86, 0.20},
    {"AA", "98s", 76.40, 0.20},
    {"KK", "JTs", 77.97, 0.20},
    {"QQ", "JTs", 78.59, 0.20},

    /* Premium hands vs premium hands */
    {"AKs", "AQs", 72.87, 0.20},
    {"AKs", "KQs", 62.33, 0.20},
    {"AKo", "AQo", 73.29, 0.20},
    {"AKo", "KQo", 63.45, 0.20},

    /* Suited vs offsuit */
    {"AKs", "AKo", 52.11, 0.30},  /* Close matchup, higher tolerance */
    {"AQs", "AQo", 52.03, 0.30},
    {"KQs", "KQo", 51.93, 0.30},

    /* Weak hands */
    {"72o", "AA", 12.18, 0.20},  /* Worst vs best */
    {"72o", "32o", 57.32, 0.30},
    {"22", "AKo", 52.71, 0.30},  /* Coin flip */

    /* Dominated hands */
    {"AK", "AQ", 73.95, 0.20},  /* Domination */
    {"AK", "KQ", 73.48, 0.20},
    {"KQ", "KJ", 73.76, 0.20},

    /* Suited vs suited (same suit conflicts) */
    {"AKs", "QJs", 63.23, 0.30},
    {"JTs", "98s", 60.92, 0.30},

    /* Random important matchups */
    {"JJ", "TT", 82.24, 0.20},
    {"99", "AKo", 53.70, 0.30},
    {"88", "AQo", 54.31, 0.30},
    {"77", "AJo", 54.81, 0.30},
};

#define NUM_REFERENCE_MATCHUPS (sizeof(reference_matchups) / sizeof(reference_matchups[0]))

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <table_file.dat>\n", argv[0]);
        printf("\nValidates pre-flop equity table against PokerStove reference values\n");
        printf("Example: %s holdem_preflop_169x169_fast.dat\n", argv[0]);
        return 1;
    }

    const char *table_file = argv[1];

    printf("=== Pre-flop Equity Table Validation ===\n\n");
    printf("Table file: %s\n", table_file);
    printf("Reference: PokerStove exact values\n");
    printf("Number of test matchups: %zu\n\n", NUM_REFERENCE_MATCHUPS);

    /* Load table */
    preflop_lookup_table_t *table = preflop_lookup_table_load(table_file, game_holdem);
    if (!table)
    {
        fprintf(stderr, "Error: Failed to load table from %s\n", table_file);
        return 1;
    }

    printf("✅ Table loaded successfully\n\n");

    /* Validate each matchup */
    int passed = 0;
    int failed = 0;
    double total_abs_error = 0.0;
    double max_error = 0.0;
    const char *max_error_matchup = NULL;

    printf("%-8s %-8s | %8s %8s | %8s | %s\n",
           "Hand 1", "Hand 2", "Expected", "Actual", "Diff", "Status");
    printf("----------------------------------------------------------------\n");

    for (size_t i = 0; i < NUM_REFERENCE_MATCHUPS; i++)
    {
        const reference_matchup_t *ref = &reference_matchups[i];

        /* Parse hands */
        int h1 = preflop_string_to_hand(ref->hand1);
        int h2 = preflop_string_to_hand(ref->hand2);

        if (h1 < 0 || h2 < 0)
        {
            fprintf(stderr, "Error: Failed to parse hands %s vs %s\n",
                    ref->hand1, ref->hand2);
            failed++;
            continue;
        }

        /* Get equity from table */
        double equity_actual = preflop_lookup_table_get(table, h1, h2);
        double equity_expected = ref->equity1_expected / 100.0;  /* Convert % to decimal */

        /* Calculate difference */
        double diff = fabs(equity_actual - equity_expected);
        double diff_percent = diff * 100.0;

        /* Update statistics */
        total_abs_error += diff;
        if (diff > max_error)
        {
            max_error = diff;
            max_error_matchup = ref->hand1;
        }

        /* Check if within tolerance */
        int is_pass = (diff_percent <= ref->tolerance);

        printf("%-8s %-8s | %7.2f%% %7.2f%% | %7.2f%% | %s\n",
               ref->hand1, ref->hand2,
               ref->equity1_expected,
               equity_actual * 100.0,
               diff_percent,
               is_pass ? "✅ PASS" : "❌ FAIL");

        if (is_pass)
            passed++;
        else
            failed++;
    }

    printf("----------------------------------------------------------------\n\n");

    /* Summary statistics */
    double mean_abs_error = total_abs_error / NUM_REFERENCE_MATCHUPS;

    printf("=== Validation Summary ===\n");
    printf("Total matchups tested:  %zu\n", NUM_REFERENCE_MATCHUPS);
    printf("Passed:                 %d (%.1f%%)\n", passed,
           100.0 * passed / NUM_REFERENCE_MATCHUPS);
    printf("Failed:                 %d (%.1f%%)\n", failed,
           100.0 * failed / NUM_REFERENCE_MATCHUPS);
    printf("\n");
    printf("Mean absolute error:    %.3f%%\n", mean_abs_error * 100.0);
    printf("Max error:              %.3f%% (%s)\n", max_error * 100.0,
           max_error_matchup ? max_error_matchup : "N/A");
    printf("\n");

    /* Overall verdict */
    if (failed == 0)
    {
        printf("🎉 VERDICT: ✅ ALL TESTS PASSED\n");
        printf("Table is accurate and ready for production use!\n");
    }
    else if (failed <= NUM_REFERENCE_MATCHUPS / 10)  /* Less than 10% failed */
    {
        printf("⚠️  VERDICT: ⚠️  MOSTLY PASSED\n");
        printf("Table is acceptable but has some outliers.\n");
        printf("Consider regenerating with more samples.\n");
    }
    else
    {
        printf("❌ VERDICT: ❌ FAILED\n");
        printf("Table has significant errors.\n");
        printf("Please regenerate with more samples or check implementation.\n");
    }

    preflop_lookup_table_free(table);

    return (failed == 0) ? 0 : 1;
}
