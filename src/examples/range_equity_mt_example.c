/*
 * range_equity_mt_example.c - Simple example of using multithreaded range equity
 *
 * This example shows how to use the multithreaded API for faster calculations
 * with large ranges.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/RangeEquity_MT.h>

/* Function prototypes */
static void printHand(StdDeck_CardMask hand);
static int generateAK(StdDeck_CardMask *hands);
static int generatePairs(StdDeck_CardMask *hands, int rank);

// Helper to print a hand
static void printHand(StdDeck_CardMask hand)
{
    int first = 1;
    for (int card = 0; card < 52; card++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(hand, card))
        {
            if (!first)
                printf(" ");
            first = 0;
            int r = StdDeck_RANK(card);
            int s = StdDeck_SUIT(card);
            printf("%c%c", StdDeck_rankChars[r], StdDeck_suitChars[s]);
        }
    }
}

// Generate all AK combinations (suited and offsuit)
static int generateAK(StdDeck_CardMask *hands)
{
    int count = 0;
    for (int suitA = 0; suitA < 4; suitA++)
    {
        for (int suitK = 0; suitK < 4; suitK++)
        {
            StdDeck_CardMask_RESET(hands[count]);
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, suitA));
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(StdDeck_Rank_KING, suitK));
            count++;
        }
    }
    return count;
}

// Generate pocket pairs
static int generatePairs(StdDeck_CardMask *hands, int rank)
{
    int count = 0;
    for (int suit1 = 0; suit1 < 4; suit1++)
    {
        for (int suit2 = suit1 + 1; suit2 < 4; suit2++)
        {
            StdDeck_CardMask_RESET(hands[count]);
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit1));
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit2));
            count++;
        }
    }
    return count;
}

int main(void)
{
    printf("==============================================\n");
    printf("MULTITHREADED RANGE EQUITY EXAMPLE\n");
    printf("==============================================\n\n");

    // Example: Calculate QQ+ vs AK on various boards
    printf("Calculating QQ+ vs AK equity on different boards...\n\n");

    // Build ranges
    StdDeck_CardMask range1_hands[18]; // QQ, KK, AA = 6+6+6 = 18 combos
    StdDeck_CardMask range2_hands[16]; // AK = 16 combos

    int range1_count = 0;
    range1_count += generatePairs(&range1_hands[range1_count], StdDeck_Rank_QUEEN);
    range1_count += generatePairs(&range1_hands[range1_count], StdDeck_Rank_KING);
    range1_count += generatePairs(&range1_hands[range1_count], StdDeck_Rank_ACE);

    int range2_count = generateAK(range2_hands);

    printf("Range 1 (QQ+): %d combinations\n", range1_count);
    printf("Range 2 (AK): %d combinations\n\n", range2_count);

    // Set up player ranges
    PlayerRange ranges[2] = {
        {range1_hands, NULL, range1_count, (double)range1_count},
        {range2_hands, NULL, range2_count, (double)range2_count}};

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    // Test on different boards
    struct
    {
        const char *desc;
        StdDeck_CardMask board;
        int nboard;
    } test_boards[] = {
        {"Preflop", {0}, 0},
        {"Dry flop: 7h 3s 2c", {0}, 3},
        {"Wet flop: Qs Js Tc", {0}, 3},
        {"Ace high flop: As 8h 4d", {0}, 3}};

    // Set up the specific boards
    // Board 1: 7h 3s 2c
    StdDeck_CardMask_RESET(test_boards[1].board);
    StdDeck_CardMask_SET(test_boards[1].board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(test_boards[1].board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(test_boards[1].board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));

    // Board 2: Qs Js Tc
    StdDeck_CardMask_RESET(test_boards[2].board);
    StdDeck_CardMask_SET(test_boards[2].board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(test_boards[2].board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(test_boards[2].board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));

    // Board 3: As 8h 4d
    StdDeck_CardMask_RESET(test_boards[3].board);
    StdDeck_CardMask_SET(test_boards[3].board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(test_boards[3].board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(test_boards[3].board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS));

    // Calculate equity for each board
    for (int i = 0; i < 4; i++)
    {
        printf("Board: %s", test_boards[i].desc);
        if (test_boards[i].nboard > 0)
        {
            printf(" (");
            printHand(test_boards[i].board);
            printf(")");
        }
        printf("\n");

        enum_result_t result;
        enumResultAlloc(&result, 2, enum_ordering_mode_hi);

        clock_t start = clock();

        // Use the Auto version which automatically chooses ST or MT
        int matchups = CalculateEquityForRanges_Auto(
            game_holdem,
            ranges,
            2,
            test_boards[i].board,
            dead,
            5 - test_boards[i].nboard, // cards to deal
            0,                         // exhaustive
            0,                         // iterations
            0,                         // orderflag
            &result);

        clock_t end = clock();
        double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;

        if (matchups > 0)
        {
            printf("  Valid matchups: %d\n", matchups);
            printf("  QQ+ equity: %.2f%%\n", result.ev[0] * 100);
            printf("  AK equity: %.2f%%\n", result.ev[1] * 100);
            printf("  Win/Tie/Loss for QQ+: %d/%d/%d\n",
                   result.nwinhi[0], result.ntiehi[0], result.nlosehi[0]);
            printf("  Calculation time: %.3f seconds\n", cpu_time);
        }
        else
        {
            printf("  Error: No valid matchups found\n");
        }

        enumResultFree(&result);
        printf("\n");
    }

    // Example 2: Using explicit multithreading control
    printf("==============================================\n");
    printf("EXPLICIT THREAD CONTROL EXAMPLE\n");
    printf("==============================================\n\n");

    printf("Comparing different thread counts for a large calculation...\n");
    printf("Range: 22+ vs AKs-A2s, KQs-K9s (all pairs vs suited aces and kings)\n\n");

    // Build larger ranges
    StdDeck_CardMask *large_range1 = malloc(sizeof(StdDeck_CardMask) * 100);
    StdDeck_CardMask *large_range2 = malloc(sizeof(StdDeck_CardMask) * 100);

    // All pairs
    int large1_count = 0;
    for (int rank = StdDeck_Rank_2; rank <= StdDeck_Rank_ACE; rank++)
    {
        large1_count += generatePairs(&large_range1[large1_count], rank);
    }

    // Suited aces and kings
    int large2_count = 0;
    for (int suit = 0; suit < 4; suit++)
    {
        // AKs-A2s
        for (int rank = StdDeck_Rank_2; rank <= StdDeck_Rank_KING; rank++)
        {
            StdDeck_CardMask_RESET(large_range2[large2_count]);
            StdDeck_CardMask_SET(large_range2[large2_count],
                                 StdDeck_MAKE_CARD(StdDeck_Rank_ACE, suit));
            StdDeck_CardMask_SET(large_range2[large2_count],
                                 StdDeck_MAKE_CARD(rank, suit));
            large2_count++;
        }
        // KQs-K9s
        for (int rank = StdDeck_Rank_9; rank <= StdDeck_Rank_QUEEN; rank++)
        {
            StdDeck_CardMask_RESET(large_range2[large2_count]);
            StdDeck_CardMask_SET(large_range2[large2_count],
                                 StdDeck_MAKE_CARD(StdDeck_Rank_KING, suit));
            StdDeck_CardMask_SET(large_range2[large2_count],
                                 StdDeck_MAKE_CARD(rank, suit));
            large2_count++;
        }
    }

    PlayerRange large_ranges[2] = {
        {large_range1, NULL, large1_count, (double)large1_count},
        {large_range2, NULL, large2_count, (double)large2_count}};

    printf("Range 1: %d combinations\n", large1_count);
    printf("Range 2: %d combinations\n", large2_count);
    printf("Total potential matchups: %d\n\n", large1_count * large2_count);

    StdDeck_CardMask empty_board;
    StdDeck_CardMask_RESET(empty_board);

    int thread_counts[] = {1, 2, 4, 0}; // 0 = auto
    const char *labels[] = {"1 thread", "2 threads", "4 threads", "auto"};

    for (int i = 0; i < 4; i++)
    {
        printf("Testing with %s...\n", labels[i]);

        enum_result_t result;
        enumResultAlloc(&result, 2, enum_ordering_mode_hi);

        clock_t start = clock();

        int matchups;
        if (thread_counts[i] == 1)
        {
            // Use single-threaded version
            matchups = CalculateEquityForRanges(
                game_holdem, large_ranges, 2, empty_board, dead,
                5, 0, 0, 0, &result);
        }
        else
        {
            // Use multi-threaded version
            matchups = CalculateEquityForRanges_MT(
                game_holdem, large_ranges, 2, empty_board, dead,
                5, 0, 0, 0, &result, thread_counts[i]);
        }

        clock_t end = clock();
        double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;

        printf("  Time: %.3f seconds\n", cpu_time);
        printf("  Valid matchups: %d\n", matchups);
        printf("  22+ equity: %.2f%%\n", result.ev[0] * 100);
        printf("  Suited A/K equity: %.2f%%\n\n", result.ev[1] * 100);

        enumResultFree(&result);
    }

    free(large_range1);
    free(large_range2);

    printf("==============================================\n");
    printf("EXAMPLE COMPLETE\n");
    printf("==============================================\n");

    return 0;
}
