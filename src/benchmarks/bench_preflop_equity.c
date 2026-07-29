#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/equity/preflop_equity.h>
#include <poker_eval/core/poker_defs.h>

#define N_ITERATIONS 10

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void) {
    printf("Benchmarking Pre-flop Equity Calculation...\n");
    printf("Iterations: %d\n", N_ITERATIONS);

    preflop_range_t range1, range2;
    preflop_range_init(&range1);
    preflop_range_init(&range2);

    preflop_range_parse("AKo", &range1);
    preflop_range_parse("22+", &range2);

    preflop_equity_input_t input;
    input.range1 = range1;
    input.range2 = range2;
    input.num_samples = 0;
    input.lookup_table = NULL;

    preflop_equity_result_t result;
    // We don't need the matrix, so we can ensure it's not allocated/requested if the function supports it,
    // or we might need to free it if it does.
    // Based on header, `equity_matrix` is `double **`, probably NULL by default or allocated if requested?
    // The function `preflop_equity_calculate` usually initializes result.
    // But since we are calling it in a loop, we should check if it re-initializes.
    // However, for benchmark speed, we might want to avoid allocation.
    // Let's assume for now we just pass it.

    double start = get_time_sec();

    volatile double total_equity = 0.0;
    for (int i = 0; i < N_ITERATIONS; i++) {
        preflop_equity_calculate(&input, &result);
        total_equity += result.equity1;
    }

    double end = get_time_sec();
    double duration = end - start;

    printf("Total time: %.4f seconds\n", duration);
    printf("Average time per calc: %.2f microseconds\n", (duration / N_ITERATIONS) * 1e6);
    printf("Calculations per second: %.0f\n", N_ITERATIONS / duration);
    printf("Ignore result: %f\n", total_equity);

    return 0;
}
