#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/core/enumdefs.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define NUM_SAMPLES 100000 /* 100k samples for decent accuracy */

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
        return 1;
    }

    const char *output_file = argv[1];
    printf("Generating pre-flop equity table (169x169 matchups)...\n");
    printf("Samples per matchup: %d\n", NUM_SAMPLES);

    preflop_lookup_table_t *table = preflop_lookup_table_create(game_holdem);
    if (!table)
    {
        fprintf(stderr, "Failed to create table\n");
        return 1;
    }

    time_t start_time = time(NULL);
    int total_matchups = PREFLOP_NUM_CANONICAL_HANDS * PREFLOP_NUM_CANONICAL_HANDS;

    int idx;
#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (idx = 0; idx < total_matchups; idx++)
    {
        int h1 = idx / PREFLOP_NUM_CANONICAL_HANDS;
        int h2 = idx % PREFLOP_NUM_CANONICAL_HANDS;

        if (h1 == h2)
        {
            preflop_lookup_table_set(table, h1, h2, 0.5);
            continue;
        }

        /* Create ranges with single hands (thread-local) */
        preflop_range_t r1, r2;
        preflop_range_init(&r1);
        preflop_range_init(&r2);
        preflop_range_add(&r1, h1);
        preflop_range_add(&r2, h2);

        preflop_equity_input_t input = {
            .range1 = r1,
            .range2 = r2,
            .num_samples = NUM_SAMPLES,
            .lookup_table = NULL
        };

        preflop_equity_result_t result;
        if (preflop_equity_calculate(&input, &result) == 0)
        {
            preflop_lookup_table_set(table, h1, h2, result.equity1);
        }

        if (idx % 1000 == 0
#ifdef _OPENMP
            && omp_get_thread_num() == 0
#endif
           )
        {
            printf("Progress: %d/%d (%.1f%%)\r", idx, total_matchups, (float)idx * 100.0 / total_matchups);
            fflush(stdout);
        }
    }

    printf("\nSaving to %s...\n", output_file);
    if (preflop_lookup_table_save(table, output_file) != 0)
    {
        fprintf(stderr, "Failed to save table to %s\n", output_file);
        preflop_lookup_table_free(table);
        return 1;
    }

    preflop_lookup_table_free(table);

    time_t end_time = time(NULL);
    printf("Done in %ld seconds.\n", (long)(end_time - start_time));

    return 0;
}
