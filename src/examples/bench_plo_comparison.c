/*
 * bench_plo_comparison.c - Comparative benchmark: PLO4 vs PLO5 vs PLO6
 *
 * Measures evaluation speed for different Omaha variants:
 * - PLO4: 60 combinations (baseline)
 * - PLO5: 100 combinations (~67% more)
 * - PLO6: 150 combinations (~150% more)
 *
 * Expected results:
 * - PLO5: ~1.67x slower than PLO4
 * - PLO6: ~2.50x slower than PLO4
 *
 * Usage: ./bench_plo_comparison [iterations]
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

#define DEFAULT_ITERATIONS 10000

/* Helper: Parse cards from string */
static void parse_cards(const char *str, StdDeck_CardMask *mask)
{
    StdDeck_CardMask_RESET(*mask);
    char rank, suit;
    int i = 0;

    while (str[i] != '\0')
    {
        if (str[i] == ' ' || str[i] == ',')
        {
            i++;
            continue;
        }

        rank = str[i++];
        int r = StdDeck_Rank_2;

        switch (rank)
        {
        case 'A':
        case 'a':
            r = StdDeck_Rank_ACE;
            break;
        case 'K':
        case 'k':
            r = StdDeck_Rank_KING;
            break;
        case 'Q':
        case 'q':
            r = StdDeck_Rank_QUEEN;
            break;
        case 'J':
        case 'j':
            r = StdDeck_Rank_JACK;
            break;
        case 'T':
        case 't':
            r = StdDeck_Rank_TEN;
            break;
        case '9':
            r = StdDeck_Rank_9;
            break;
        case '8':
            r = StdDeck_Rank_8;
            break;
        case '7':
            r = StdDeck_Rank_7;
            break;
        case '6':
            r = StdDeck_Rank_6;
            break;
        case '5':
            r = StdDeck_Rank_5;
            break;
        case '4':
            r = StdDeck_Rank_4;
            break;
        case '3':
            r = StdDeck_Rank_3;
            break;
        case '2':
            r = StdDeck_Rank_2;
            break;
        default:
            continue;
        }

        if (str[i] == '\0')
            break;

        suit = str[i++];
        int s = StdDeck_Suit_HEARTS;

        switch (suit)
        {
        case 'h':
        case 'H':
            s = StdDeck_Suit_HEARTS;
            break;
        case 'd':
        case 'D':
            s = StdDeck_Suit_DIAMONDS;
            break;
        case 'c':
        case 'C':
            s = StdDeck_Suit_CLUBS;
            break;
        case 's':
        case 'S':
            s = StdDeck_Suit_SPADES;
            break;
        default:
            continue;
        }

        int card = StdDeck_MAKE_CARD(r, s);
        StdDeck_CardMask_SET(*mask, card);
    }
}

/* Benchmark PLO4 evaluation */
static double benchmark_plo4(uint32_t iterations)
{
    StdDeck_CardMask hole, board;
    HandVal hival;

    /* Hole: As Ah Ks Kh (4 cards) */
    parse_cards("AsAhKsKh", &hole);
    /* Board: Js Tc 9d 8s 7c */
    parse_cards("JsTc9d8s7c", &board);

    clock_t start = clock();

    for (uint32_t i = 0; i < iterations; i++)
    {
        StdDeck_OmahaHi_EVAL(hole, board, &hival);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    return elapsed;
}

/* Benchmark PLO5 evaluation */
static double benchmark_plo5(uint32_t iterations)
{
    StdDeck_CardMask hole, board;
    HandVal hival;

    /* Hole: As Ah Ks Kh Qh (5 cards) */
    parse_cards("AsAhKsKhQh", &hole);
    /* Board: Js Tc 9d 8s 7c */
    parse_cards("JsTc9d8s7c", &board);

    clock_t start = clock();

    for (uint32_t i = 0; i < iterations; i++)
    {
        StdDeck_OmahaHi_EVAL(hole, board, &hival);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    return elapsed;
}

/* Benchmark PLO6 evaluation */
static double benchmark_plo6(uint32_t iterations)
{
    StdDeck_CardMask hole, board;
    HandVal hival;

    /* Hole: As Ah Ks Kh Qh Jh (6 cards) */
    parse_cards("AsAhKsKhQhJh", &hole);
    /* Board: Js Tc 9d 8s 7c */
    parse_cards("JsTc9d8s7c", &board);

    clock_t start = clock();

    for (uint32_t i = 0; i < iterations; i++)
    {
        StdDeck_OmahaHi_EVAL(hole, board, &hival);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    return elapsed;
}

/* Benchmark Big-O (PLO5 Hi/Lo) */
static double benchmark_bigo(uint32_t iterations)
{
    StdDeck_CardMask hole, board;
    HandVal hival;
    LowHandVal loval;

    /* Hole: As 2h 3c 4d 5h (low potential) */
    parse_cards("As2h3c4d5h", &hole);
    /* Board: 6s 7d 8c Kh Qs */
    parse_cards("6s7d8cKhQs", &board);

    clock_t start = clock();

    for (uint32_t i = 0; i < iterations; i++)
    {
        StdDeck_OmahaHiLow8_EVAL(hole, board, &hival, &loval);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    return elapsed;
}

/* Benchmark PLO6 Hi/Lo */
static double benchmark_plo6_hilo(uint32_t iterations)
{
    StdDeck_CardMask hole, board;
    HandVal hival;
    LowHandVal loval;

    /* Hole: As 2h 3c 4d 5h 6s */
    parse_cards("As2h3c4d5h6s", &hole);
    /* Board: 7d 8c Kh Qs Jd */
    parse_cards("7d8cKhQsJd", &board);

    clock_t start = clock();

    for (uint32_t i = 0; i < iterations; i++)
    {
        StdDeck_OmahaHiLow8_EVAL(hole, board, &hival, &loval);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    return elapsed;
}

int main(int argc, char *argv[])
{
    uint32_t iterations = DEFAULT_ITERATIONS;

    if (argc > 1)
    {
        iterations = (uint32_t)atoi(argv[1]);
        if (iterations == 0)
            iterations = DEFAULT_ITERATIONS;
    }

    printf("=== PLO Comparative Benchmark ===\n");
    printf("Iterations: %u\n\n", iterations);

    /* Benchmark PLO4 */
    printf("Running PLO4 benchmark (60 combos)...\n");
    double time_plo4 = benchmark_plo4(iterations);
    double rate_plo4 = (double)iterations / time_plo4;
    printf("  Time: %.3f s\n", time_plo4);
    printf("  Rate: %.0f evals/sec\n", rate_plo4);
    printf("  Per eval: %.3f µs\n", (time_plo4 / iterations) * 1000000);
    printf("\n");

    /* Benchmark PLO5 */
    printf("Running PLO5 benchmark (100 combos)...\n");
    double time_plo5 = benchmark_plo5(iterations);
    double rate_plo5 = (double)iterations / time_plo5;
    double slowdown_plo5 = time_plo5 / time_plo4;
    printf("  Time: %.3f s\n", time_plo5);
    printf("  Rate: %.0f evals/sec\n", rate_plo5);
    printf("  Per eval: %.3f µs\n", (time_plo5 / iterations) * 1000000);
    printf("  Slowdown vs PLO4: %.2fx\n", slowdown_plo5);
    printf("  Expected: ~1.67x (100/60)\n");
    printf("\n");

    /* Benchmark PLO6 */
    printf("Running PLO6 benchmark (150 combos)...\n");
    double time_plo6 = benchmark_plo6(iterations);
    double rate_plo6 = (double)iterations / time_plo6;
    double slowdown_plo6 = time_plo6 / time_plo4;
    printf("  Time: %.3f s\n", time_plo6);
    printf("  Rate: %.0f evals/sec\n", rate_plo6);
    printf("  Per eval: %.3f µs\n", (time_plo6 / iterations) * 1000000);
    printf("  Slowdown vs PLO4: %.2fx\n", slowdown_plo6);
    printf("  Expected: ~2.50x (150/60)\n");
    printf("\n");

    /* Benchmark Big-O (PLO5 Hi/Lo) */
    printf("Running Big-O benchmark (100 combos Hi+Lo)...\n");
    double time_bigo = benchmark_bigo(iterations);
    double rate_bigo = (double)iterations / time_bigo;
    double slowdown_bigo = time_bigo / time_plo5;
    printf("  Time: %.3f s\n", time_bigo);
    printf("  Rate: %.0f evals/sec\n", rate_bigo);
    printf("  Per eval: %.3f µs\n", (time_bigo / iterations) * 1000000);
    printf("  Slowdown vs PLO5 Hi: %.2fx\n", slowdown_bigo);
    printf("  Expected: ~1.3-1.5x (Hi+Lo overhead)\n");
    printf("\n");

    /* Benchmark PLO6 Hi/Lo */
    printf("Running PLO6 Hi/Lo benchmark (150 combos Hi+Lo)...\n");
    double time_plo6_hilo = benchmark_plo6_hilo(iterations);
    double rate_plo6_hilo = (double)iterations / time_plo6_hilo;
    double slowdown_plo6_hilo = time_plo6_hilo / time_plo6;
    printf("  Time: %.3f s\n", time_plo6_hilo);
    printf("  Rate: %.0f evals/sec\n", rate_plo6_hilo);
    printf("  Per eval: %.3f µs\n", (time_plo6_hilo / iterations) * 1000000);
    printf("  Slowdown vs PLO6 Hi: %.2fx\n", slowdown_plo6_hilo);
    printf("  Expected: ~1.3-1.5x (Hi+Lo overhead)\n");
    printf("\n");

    /* Summary table */
    printf("=== Summary ===\n");
    printf("%-15s | %8s | %10s | %10s | %10s\n",
           "Variant", "Combos", "Time (s)", "Rate/sec", "vs PLO4");
    printf("----------------+----------+------------+------------+------------\n");
    printf("%-15s | %8d | %10.3f | %10.0f | %10.2fx\n",
           "PLO4", 60, time_plo4, rate_plo4, 1.0);
    printf("%-15s | %8d | %10.3f | %10.0f | %10.2fx\n",
           "PLO5", 100, time_plo5, rate_plo5, slowdown_plo5);
    printf("%-15s | %8d | %10.3f | %10.0f | %10.2fx\n",
           "PLO6", 150, time_plo6, rate_plo6, slowdown_plo6);
    printf("%-15s | %8s | %10.3f | %10.0f | %10.2fx\n",
           "Big-O", "100+Lo", time_bigo, rate_bigo, time_bigo / time_plo4);
    printf("%-15s | %8s | %10.3f | %10.0f | %10.2fx\n",
           "PLO6 Hi/Lo", "150+Lo", time_plo6_hilo, rate_plo6_hilo, time_plo6_hilo / time_plo4);
    printf("\n");

    /* Validation */
    printf("=== Validation ===\n");
    double expected_plo5 = 100.0 / 60.0;
    double expected_plo6 = 150.0 / 60.0;
    double error_plo5 = ((slowdown_plo5 - expected_plo5) / expected_plo5) * 100.0;
    double error_plo6 = ((slowdown_plo6 - expected_plo6) / expected_plo6) * 100.0;

    printf("PLO5 slowdown: %.2fx (expected: %.2fx, error: %.1f%%)\n",
           slowdown_plo5, expected_plo5, error_plo5);
    printf("PLO6 slowdown: %.2fx (expected: %.2fx, error: %.1f%%)\n",
           slowdown_plo6, expected_plo6, error_plo6);

    if (fabs(error_plo5) < 10.0 && fabs(error_plo6) < 10.0)
    {
        printf("\n✅ Performance matches expectations (<10%% error)\n");
    }
    else
    {
        printf("\n⚠️  Performance deviates from expectations (>10%% error)\n");
    }

    return 0;
}
