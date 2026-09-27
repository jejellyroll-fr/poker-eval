/*
 * bench_plo_hand_features.c - throughput of the PLO hand-feature engine
 * (issue #238).
 *
 * For PLO4, PLO5 and PLO6 on flop, turn and river boards: how long a board
 * takes to prepare, how many hands per second pe_hand_features_compute_batch
 * classifies, and how many strategy rows per second
 * pe_strategy_bucket_aggregate groups. Hands and boards are drawn once, before
 * the clock starts, into buffers reused for every board, so the timed loop
 * allocates nothing.
 *
 *   bench_plo_hand_features [hands_per_board] [boards]
 */

#include <poker_eval/solver/pe_hand_features.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t rng_next(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static double now_seconds(void)
{
    const clock_t ticks = clock();
    return (double)ticks / (double)CLOCKS_PER_SEC;
}

/* Deal a board of `board_n` cards and `count` hands of `hole_n` cards that
   avoid it (hands may share cards with each other, as a range does). */
static mask_t deal(uint64_t *seed, int board_n, int hole_n, size_t count,
                   mask_t *holes)
{
    mask_t board = MASK_EMPTY;
    while (mask_popcount(board) < (unsigned)board_n)
        board = mask_set(board, (int)(rng_next(seed) % 52u));
    for (size_t i = 0; i < count; ++i)
    {
        mask_t h = MASK_EMPTY;
        while (mask_popcount(h) < (unsigned)hole_n)
        {
            int c = (int)(rng_next(seed) % 52u);
            if (!mask_is_set(board, c))
                h = mask_set(h, c);
        }
        holes[i] = h;
    }
    return board;
}

int main(int argc, char **argv)
{
    size_t hands = argc > 1 ? (size_t)strtoul(argv[1], NULL, 10) : 20000u;
    int boards = argc > 2 ? atoi(argv[2]) : 20;
    static const int streets[3] = {3, 4, 5};
    static const char *const street_names[3] = {"flop", "turn", "river"};
    static const double freq[3] = {0.25, 0.5, 0.25};
    uint64_t seed = 0x238ull;
    int failed = 0;

    if (hands == 0 || boards <= 0)
    {
        fprintf(stderr, "usage: %s [hands_per_board] [boards]\n", argv[0]);
        return 2;
    }

    mask_t *holes = malloc(hands * sizeof(*holes));
    pe_hand_features_t *features = malloc(hands * sizeof(*features));
    pe_strategy_row_t *rows = malloc(hands * sizeof(*rows));
    pe_strategy_bucket_t buckets[512];
    if (!holes || !features || !rows)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    for (size_t i = 0; i < hands; ++i)
    {
        rows[i].weight = 1.0;
        rows[i].freq = freq;
        rows[i].ev = NULL;
    }

    printf("bench_plo_hand_features: %zu hands x %d boards per case\n", hands,
           boards);
    printf("%-6s %-6s %14s %16s %16s\n", "game", "street", "board_prep_us",
           "hands_per_sec", "rows_per_sec");

    for (int hole_n = 4; hole_n <= 6; ++hole_n)
    {
        for (int st = 0; st < 3; ++st)
        {
            double prep = 0.0, classify = 0.0, aggregate = 0.0;
            for (int b = 0; b < boards; ++b)
            {
                pe_hf_board_t board;
                size_t count = 0;
                mask_t board_mask = deal(&seed, streets[st], hole_n, hands, holes);
                double t0 = now_seconds();
                if (pe_hf_board_prepare(board_mask, &board) != PE_SOLVER_OK)
                    failed = 1;
                double t1 = now_seconds();
                if (pe_hand_features_compute_batch(&board, holes, hands,
                                                   features, NULL) != PE_SOLVER_OK)
                    failed = 1;
                double t2 = now_seconds();
                for (size_t i = 0; i < hands; ++i)
                    rows[i].hand = holes[i];
                double t3 = now_seconds();
                if (pe_strategy_bucket_aggregate(&board, rows, hands, 3,
                                                 PE_HF_BUCKET_MADE_AND_DRAWS,
                                                 buckets, 512, &count) !=
                    PE_SOLVER_OK)
                    failed = 1;
                double t4 = now_seconds();
                prep += t1 - t0;
                classify += t2 - t1;
                aggregate += t4 - t3;
            }
            double total = (double)hands * (double)boards;
            printf("PLO%-3d %-6s %14.1f %16.0f %16.0f\n", hole_n,
                   street_names[st], 1e6 * prep / boards,
                   classify > 0.0 ? total / classify : 0.0,
                   aggregate > 0.0 ? total / aggregate : 0.0);
        }
    }

    free(holes);
    free(features);
    free(rows);
    if (failed)
    {
        fprintf(stderr, "bench_plo_hand_features: a call failed\n");
        return 1;
    }
    return 0;
}
