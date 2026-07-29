#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/range/AdvancedRangeParser.h>

static double monotonic_random(double t) {
    static unsigned long long state = 0x12345678ULL;
    state = state * 6364136223846793005ULL + 1ULL;
    double r = (double)((state >> 11) & 0xFFFFFFFFFFFFULL) / (double)(1ULL << 53);
    return r * t;
}

static void build_player_range(const char *expr, PlayerRange *out_range) {
    arp_range_t parsed;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    if (!ARP_ParseRange(expr, dead, game_holdem, &parsed)) {
        fprintf(stderr, "Failed to parse range: %s\n", expr);
        exit(EXIT_FAILURE);
    }
    if (!ARP_ToPlayerRange(&parsed, out_range)) {
        fprintf(stderr, "Failed to convert range: %s\n", expr);
        exit(EXIT_FAILURE);
    }
}

static double benchmark_equity(
    const char *range_expr[],
    const double invested[],
    int num_players,
    int iterations,
    int use_montecarlo,
    int cards_to_deal
) {
    PlayerRange *player_ranges = calloc((size_t)num_players, sizeof(PlayerRange));
    if (!player_ranges) {
        fprintf(stderr, "Allocation failed\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_players; ++i) {
        build_player_range(range_expr[i], &player_ranges[i]);
    }

    double *stacks = calloc((size_t)num_players, sizeof(double));
    double *invested_copy = calloc((size_t)num_players, sizeof(double));
    if (!stacks || !invested_copy) {
        fprintf(stderr, "Allocation failed\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < num_players; ++i) {
        invested_copy[i] = invested[i];
    }

    MultiwayPotState state = {player_ranges, stacks, invested_copy, num_players};
    MultiwayEquityOptions options;
    options.use_montecarlo = use_montecarlo;
    options.iterations = use_montecarlo ? iterations : 0;
    options.orderflag = 0;

    MultiwayEquityResult result;
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    clock_t start = clock();
    for (int iter = 0; iter < iterations; ++iter) {
        StdDeck_CardMask board_sample = board;
        if (use_montecarlo) {
            StdDeck_CardMask_RESET(board_sample);
            int drawn = 0;
            while (drawn < 5) {
                int card = (int)(monotonic_random(52.0));
                StdDeck_CardMask card_mask;
                StdDeck_CardMask_RESET(card_mask);
                StdDeck_CardMask_SET(card_mask, card);
                if (!StdDeck_CardMask_ANY_SET(board_sample, card_mask)) {
                    StdDeck_CardMask_OR(board_sample, board_sample, card_mask);
                    ++drawn;
                }
            }
        }

        memset(&result, 0, sizeof(result));
        int matchups = CalculateMultiwayEquity(
            game_holdem,
            &state,
            board_sample,
            dead,
            cards_to_deal,
            &options,
            &result
        );

        if (matchups <= 0) {
            fprintf(stderr, "Warning: No matchups for expression set\n");
        }
    }
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    free(player_ranges);
    free(stacks);
    free(invested_copy);
    return elapsed;
}

int main(void) {
    const char *range_sets[][3] = {
        {"AA", "KK", "QQ"},
        {"TT+", "AKo{50%}", "AKs{50%}"},
        {"(AA,KK) - !AKs", "AQs+", "JJ-99"}
    };

    const double invested[][3] = {
        {100.0, 100.0, 60.0},
        {80.0, 60.0, 60.0},
        {120.0, 80.0, 80.0}
    };

    const int players_per_test[] = {3, 3, 3};
    const int num_tests = 3;

    int mc_iterations = 2000;
    int exhaustive_iterations = 50;
    int cards_to_deal[3] = {0, 0, 2};

    printf("Multiway Equity Benchmark\n");
    printf("=========================\n");

    for (int t = 0; t < num_tests; ++t) {
        printf("\nTest %d\n", t + 1);
        printf("--------\n");

        double time_exhaustive = benchmark_equity(
            range_sets[t],
            invested[t],
            players_per_test[t],
            exhaustive_iterations,
            0,
            cards_to_deal[t]
        );
        printf("Exhaustive: %.3f s (%d iterations)\n",
               time_exhaustive, exhaustive_iterations);

        double time_mc = benchmark_equity(
            range_sets[t],
            invested[t],
            players_per_test[t],
            mc_iterations,
            1,
            cards_to_deal[t]
        );
        printf("Monte Carlo: %.3f s (%d iterations)\n",
               time_mc, mc_iterations);
    }

    printf("\nBenchmark complete.\n");
    return 0;
}
