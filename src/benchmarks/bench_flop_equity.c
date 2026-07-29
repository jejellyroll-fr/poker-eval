#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/equity/flop_equity.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

#define N_ITERATIONS 100000

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void) {
    printf("Benchmarking Flop Equity Calculation...\n");
    printf("Iterations: %d\n", N_ITERATIONS);

    // Setup: Ah Kh vs random on Qh Jh Th
    StdDeck_CardMask pocket, flop;
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(flop);

    // Ah Kh
    StdDeck_CardMask_OR(pocket, pocket, StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS)));
    StdDeck_CardMask_OR(pocket, pocket, StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS)));

    // Qh Jh Th
    StdDeck_CardMask_OR(flop, flop, StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS)));
    StdDeck_CardMask_OR(flop, flop, StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS)));
    StdDeck_CardMask_OR(flop, flop, StdDeck_MASK(StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS)));

    flop_equity_input_t input;
    input.pocket = pocket;
    input.flop = flop;
    input.n_opponents = 1;
    input.n_samples = 0; // Exhaustive or heuristic

    flop_equity_result_t result;

    double start = get_time_sec();

    for (int i = 0; i < N_ITERATIONS; i++) {
        flop_calc_equity(&input, &result);
    }

    double end = get_time_sec();
    double duration = end - start;

    printf("Total time: %.4f seconds\n", duration);
    printf("Average time per calc: %.2f microseconds\n", (duration / N_ITERATIONS) * 1e6);
    printf("Calculations per second: %.0f\n", N_ITERATIONS / duration);
    printf("Last equity: %.4f\n", result.equity);

    return 0;
}
