/**
 * @file poker_variants_example.c
 * @brief Examples of hand evaluation for different poker variants
 *
 * This file demonstrates the modern API for evaluating hands
 * across multiple poker game variants.
 *
 * Compile:
 *   gcc -o poker_variants poker_variants_example.c -lpoker_eval
 *
 * @copyright Copyright (C) 2025 poker-eval contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/range.h>
#include <poker_eval/equity.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/eval_context.h>

/* Helper to print equity results */
static void print_equity_result(const char *game_name,
                                const char *range1,
                                const char *range2,
                                const pe_equity_result_multi_t *result)
{
    printf("\n=== %s ===\n", game_name);
    printf("  %s vs %s\n", range1, range2);
    printf("  Player 1: %.2f%% (win: %.2f%%, tie: %.2f%%)\n",
           result->results[0].equity * 100.0,
           result->results[0].win_prob * 100.0,
           result->results[0].tie_prob * 100.0);
    printf("  Player 2: %.2f%% (win: %.2f%%, tie: %.2f%%)\n",
           result->results[1].equity * 100.0,
           result->results[1].win_prob * 100.0,
           result->results[1].tie_prob * 100.0);
    printf("  Samples: %ld, Exact: %s\n",
           result->samples, result->exact ? "yes" : "no");
}

/* Helper to print Hi/Lo equity results */
static void print_hilo_result(const char *game_name,
                              const char *range1,
                              const char *range2,
                              const pe_equity_result_multi_t *result)
{
    printf("\n=== %s (Hi/Lo) ===\n", game_name);
    printf("  %s vs %s\n", range1, range2);
    printf("  Player 1 Hi: %.2f%%, Lo: %.2f%%, Scoop: %.2f%%\n",
           result->hilo_results[0].equity_hi * 100.0,
           result->hilo_results[0].equity_lo * 100.0,
           result->hilo_results[0].scoop_prob * 100.0);
    printf("  Player 2 Hi: %.2f%%, Lo: %.2f%%, Scoop: %.2f%%\n",
           result->hilo_results[1].equity_hi * 100.0,
           result->hilo_results[1].equity_lo * 100.0,
           result->hilo_results[1].scoop_prob * 100.0);
}

/* Texas Hold'em example */
static int example_holdem(void)
{
    printf("\n========================================\n");
    printf("  TEXAS HOLD'EM EXAMPLES\n");
    printf("========================================\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_range_t *r1 = NULL, *r2 = NULL;
    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {.iterations = 50000};

    /* Example 1: Premium vs Premium */
    pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
    
    const pe_range_t *ranges[] = {r1, r2};
    pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
    print_equity_result("Hold'em: AA vs KK", "AA", "KK", &result);
    
    pe_range_free(r1);
    pe_range_free(r2);

    /* Example 2: Pair vs Two Overcards */
    pe_range_parse(game_holdem, "JJ", dead, NULL, &r1);
    pe_range_parse(game_holdem, "AKs", dead, NULL, &r2);
    
    const pe_range_t *ranges2[] = {r1, r2};
    pe_equity_preflop(game_holdem, ranges2, 2, &opts, &result);
    print_equity_result("Hold'em: JJ vs AKs", "JJ", "AKs", &result);
    
    pe_range_free(r1);
    pe_range_free(r2);

    /* Example 3: Range vs Range */
    pe_range_parse(game_holdem, "TT+,AQs+,AKo", dead, NULL, &r1);
    pe_range_parse(game_holdem, "22-99,A2s-AJs,KQs", dead, NULL, &r2);
    
    const pe_range_t *ranges3[] = {r1, r2};
    pe_equity_preflop(game_holdem, ranges3, 2, &opts, &result);
    print_equity_result("Hold'em: Top 5% vs Medium", "TT+,AQs+,AKo", "22-99,A2s-AJs,KQs", &result);
    
    pe_range_free(r1);
    pe_range_free(r2);

    return 0;
}

/* Omaha (PLO) example */
static int example_omaha(void)
{
    printf("\n========================================\n");
    printf("  OMAHA (PLO) EXAMPLES\n");
    printf("========================================\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_range_t *r1 = NULL, *r2 = NULL;
    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {.iterations = 30000};

    /* Example 1: AAxx vs KKxx */
    pe_range_parse(game_omaha, "AAxx", dead, NULL, &r1);
    pe_range_parse(game_omaha, "KKxx", dead, NULL, &r2);
    
    if (r1 && r2) {
        const pe_range_t *ranges[] = {r1, r2};
        pe_equity_preflop(game_omaha, ranges, 2, &opts, &result);
        print_equity_result("Omaha: AAxx vs KKxx", "AAxx", "KKxx", &result);
    }
    
    pe_range_free(r1);
    pe_range_free(r2);
    r1 = r2 = NULL;

    /* Example 2: Rundown vs Premium Pair */
    pe_range_parse(game_omaha, "AAxx", dead, NULL, &r1);
    /* Note: Rundown ranges may need specific syntax */
    pe_range_parse(game_omaha, "KKxx", dead, NULL, &r2);
    
    if (r1 && r2) {
        const pe_range_t *ranges[] = {r1, r2};
        pe_equity_preflop(game_omaha, ranges, 2, &opts, &result);
        print_equity_result("Omaha: AAxx vs KKxx", "AAxx", "KKxx", &result);
    }
    
    pe_range_free(r1);
    pe_range_free(r2);

    return 0;
}

/* Omaha Hi/Lo example */
static int example_omaha8(void)
{
    printf("\n========================================\n");
    printf("  OMAHA HI/LO EXAMPLES\n");
    printf("========================================\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_range_t *r1 = NULL, *r2 = NULL;
    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {.iterations = 30000};

    /* Example: Good low hand vs High-only hand */
    pe_range_parse(game_omaha8, "AA23", dead, NULL, &r1);
    pe_range_parse(game_omaha8, "KKQJ", dead, NULL, &r2);
    
    if (r1 && r2) {
        const pe_range_t *ranges[] = {r1, r2};
        pe_equity_preflop(game_omaha8, ranges, 2, &opts, &result);
        print_hilo_result("Omaha Hi/Lo", "AA23", "KKQJ", &result);
    }
    
    pe_range_free(r1);
    pe_range_free(r2);

    return 0;
}

/* Short Deck (6+) Hold'em example */
static int example_shortdeck(void)
{
    printf("\n========================================\n");
    printf("  SHORT DECK (6+) HOLD'EM EXAMPLES\n");
    printf("========================================\n");

    printf("  Note: In Short Deck:\n");
    printf("  - Cards 2-5 are removed (36-card deck)\n");
    printf("  - Flush beats Full House\n");
    printf("  - A-6-7-8-9 is the lowest straight\n");
    printf("\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_range_t *r1 = NULL, *r2 = NULL;
    pe_equity_result_multi_t result;
    pe_equity_opts_t opts = {.iterations = 50000};

    /* In short deck, pocket pairs run closer together */
    pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
    
    if (r1 && r2) {
        const pe_range_t *ranges[] = {r1, r2};
        /* Note: For true short deck, use game_shortdeck if available */
        pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
        print_equity_result("Short Deck: AA vs KK (simulated)", "AA", "KK", &result);
        printf("  (In true short deck, equity runs closer)\n");
    }
    
    pe_range_free(r1);
    pe_range_free(r2);

    return 0;
}

/* Razz (7-Card Stud Low) example */
static int example_razz(void)
{
    printf("\n========================================\n");
    printf("  RAZZ (7-CARD STUD LOW) EXAMPLES\n");
    printf("========================================\n");

    printf("  Note: In Razz:\n");
    printf("  - Lowest hand wins\n");
    printf("  - Aces are low\n");
    printf("  - Straights and flushes don't count\n");
    printf("  - Best hand: A-2-3-4-5 (the wheel)\n");
    printf("\n");

    /* Razz specific range parsing would need game_razz support */
    printf("  Example hands:\n");
    printf("  - (A2)3 vs (45)6: Strong low vs Medium low\n");
    printf("  - Starting with 3 low cards is ideal\n");
    printf("\n");

    return 0;
}

/* 7-Card Stud example */
static int example_stud(void)
{
    printf("\n========================================\n");
    printf("  7-CARD STUD EXAMPLES\n");
    printf("========================================\n");

    printf("  Note: In 7-Card Stud:\n");
    printf("  - No community cards\n");
    printf("  - Each player gets 7 cards (3 down, 4 up)\n");
    printf("  - Best 5-card hand wins\n");
    printf("\n");

    printf("  Example starting hands:\n");
    printf("  - (AA)K: Rolled-up aces (very strong)\n");
    printf("  - (KK)A: Split kings with ace kicker\n");
    printf("  - (JT)9s: Three to a straight flush\n");
    printf("\n");

    return 0;
}

/* Badugi example */
static int example_badugi(void)
{
    printf("\n========================================\n");
    printf("  BADUGI EXAMPLES\n");
    printf("========================================\n");

    printf("  Note: In Badugi:\n");
    printf("  - 4-card lowball game\n");
    printf("  - One card per suit (rainbow)\n");
    printf("  - Aces are low\n");
    printf("  - Best hand: A-2-3-4 rainbow (badugi)\n");
    printf("\n");

    printf("  Hand rankings (best to worst):\n");
    printf("  1. 4-card badugi (all different suits)\n");
    printf("  2. 3-card hand (one pair of suits)\n");
    printf("  3. 2-card hand (two pairs of suits)\n");
    printf("  4. 1-card hand (three+ of same suit)\n");
    printf("\n");

    return 0;
}

/* Main function */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("=============================================\n");
    printf("  POKER VARIANTS EXAMPLE\n");
    printf("  Demonstrating the Modern pe_* API\n");
    printf("=============================================\n");

    /* Run examples for each variant */
    example_holdem();
    example_omaha();
    example_omaha8();
    example_shortdeck();
    example_razz();
    example_stud();
    example_badugi();

    printf("\n=============================================\n");
    printf("  All examples completed!\n");
    printf("=============================================\n");

    return 0;
}
