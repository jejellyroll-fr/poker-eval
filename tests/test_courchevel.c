/*
 * test_courchevel.c -- Test suite for Courchevel poker variant
 *
 * Courchevel is an Omaha variant with 5 hole cards where one flop card
 * is revealed before the first betting round (pre-flop).
 *
 * Rules:
 * - 5 hole cards dealt to each player
 * - One flop card revealed pre-flop (before first betting round)
 * - Must use exactly 2 hole cards and 3 board cards (like Omaha)
 * - Available as Hi-only and Hi/Lo 8-or-better variants
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>

/* Helper to convert string to card mask */
static StdDeck_CardMask str_to_mask(const char *str)
{
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);

    while (*str)
    {
        if (*str == ' ')
        {
            str++;
            continue;
        }

        int rank = -1, suit = -1;

        /* Parse rank */
        switch (*str)
        {
        case 'A':
        case 'a':
            rank = StdDeck_Rank_ACE;
            break;
        case 'K':
        case 'k':
            rank = StdDeck_Rank_KING;
            break;
        case 'Q':
        case 'q':
            rank = StdDeck_Rank_QUEEN;
            break;
        case 'J':
        case 'j':
            rank = StdDeck_Rank_JACK;
            break;
        case 'T':
        case 't':
            rank = StdDeck_Rank_TEN;
            break;
        case '9':
            rank = StdDeck_Rank_9;
            break;
        case '8':
            rank = StdDeck_Rank_8;
            break;
        case '7':
            rank = StdDeck_Rank_7;
            break;
        case '6':
            rank = StdDeck_Rank_6;
            break;
        case '5':
            rank = StdDeck_Rank_5;
            break;
        case '4':
            rank = StdDeck_Rank_4;
            break;
        case '3':
            rank = StdDeck_Rank_3;
            break;
        case '2':
            rank = StdDeck_Rank_2;
            break;
        default:
            str++;
            continue;
        }
        str++;

        /* Parse suit */
        switch (*str)
        {
        case 's':
        case 'S':
            suit = StdDeck_Suit_SPADES;
            break;
        case 'h':
        case 'H':
            suit = StdDeck_Suit_HEARTS;
            break;
        case 'd':
        case 'D':
            suit = StdDeck_Suit_DIAMONDS;
            break;
        case 'c':
        case 'C':
            suit = StdDeck_Suit_CLUBS;
            break;
        default:
            continue;
        }
        str++;

        if (rank >= 0 && suit >= 0)
        {
            int card = StdDeck_MAKE_CARD(rank, suit);
            StdDeck_CardMask_SET(mask, card);
        }
    }

    return mask;
}

/* Test 1: Basic Courchevel evaluation with full board */
static int test_courchevel_basic_eval(void)
{
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    enum_result_t result;

    /* Player 1: AsKsQsJs5s (flush draw, high cards) */
    pockets[0] = str_to_mask("AsKsQsJs5s");

    /* Player 2: AhAd8h8d2c (two pair, aces and eights) */
    pockets[1] = str_to_mask("AhAd8h8d2c");

    /* Board: Ts9s3d4c7h (Player 1 has nut flush with A-K-Q-T-9 using Ks from hand) */
    /* Actually needs 2 hole + 3 board: As Ks from hand + Ts 9s 3s? No, board has Ts9s only */
    /* Let's use a board with 3 spades: Ts9s2s4c7h */
    board = str_to_mask("Ts9s2s4c7h");

    StdDeck_CardMask_RESET(dead);

    memset(&result, 0, sizeof(result));

    int nboard = StdDeck_numCards(board);
    int err = enumExhaustive(game_courchevel, pockets, board, dead, 2, nboard, 0, &result);
    if (err != 0)
    {
        fprintf(stderr, "enumExhaustive failed with error %d\n", err);
        return 1;
    }

    /* Player 1 should win with nut flush (As Ks + Ts 9s 2s = A-high flush) */
    /* Using 2 hole cards (As Ks) + 3 board cards (Ts 9s 2s) */
    if (result.ev[0] < 0.99)
    {
        fprintf(stderr, "Expected Player 1 to win (EV=%.4f)\n", result.ev[0]);
        return 2;
    }

    enumResultFree(&result);
    return 0;
}

/* Test 2: Courchevel with flop (nboard=3) */
static int test_courchevel_flop(void)
{
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    enum_result_t result;

    /* Player 1: AsKsQsJs5s (strong flush potential) */
    pockets[0] = str_to_mask("AsKsQsJs5s");

    /* Player 2: AhKhQhJh5h (same structure in hearts) */
    pockets[1] = str_to_mask("AhKhQhJh5h");

    /* Board: Flop with 2 spades - Ts 9s 2c (helps Player 1) */
    board = str_to_mask("Ts9s2c");

    StdDeck_CardMask_RESET(dead);

    memset(&result, 0, sizeof(result));

    /* This should enumerate the remaining 2 community cards */
    int nboard = StdDeck_numCards(board);
    int err = enumExhaustive(game_courchevel, pockets, board, dead, 2, nboard, 0, &result);
    if (err != 0)
    {
        fprintf(stderr, "enumExhaustive with flop failed with error %d\n", err);
        return 1;
    }

    /* Player 1 should have edge with 2 spades on board (flush draw) */
    /* With As Ks Qs Js from hand, needs 1 more spade for flush */
    /* Both players have similar hands, but P1 has flush draw */
    if (result.ev[0] < 0.45)
    {
        fprintf(stderr, "Expected Player 1 to have reasonable equity (EV=%.4f)\n", result.ev[0]);
        return 2;
    }

    enumResultFree(&result);
    return 0;
}

/* Test 3: Courchevel Hi/Lo with qualifying low */
static int test_courchevel8_hilo(void)
{
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    enum_result_t result;

    /* Player 1: As2d3h4c5s (wheel + nut low) */
    pockets[0] = str_to_mask("As2d3h4c5s");

    /* Player 2: KhKdQhQd9c (high only, no low) */
    pockets[1] = str_to_mask("KhKdQhQd9c");

    /* Board: 6d7c8s - need to enumerate turn+river */
    /* P1 can make straights and lows, P2 only high hands */
    board = str_to_mask("6d7c8s");

    /* Dead cards = all pocket cards */
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    memset(&result, 0, sizeof(result));

    int nboard = StdDeck_numCards(board);
    int err = enumExhaustive(game_courchevel8, pockets, board, dead, 2, nboard, 0, &result);
    if (err != 0)
    {
        fprintf(stderr, "enumExhaustive Hi/Lo failed with error %d\n", err);
        return 1;
    }

    /* Player 1 should have significant edge:
     * - When low qualifies, P1 wins low, often wins high too (straights)
     * - When no low, still competes for high with straights/pairs
     */
    if (result.ev[0] < 0.55)
    {
        fprintf(stderr, "Expected Player 1 to have advantage (EV=%.4f)\n", result.ev[0]);
        return 2;
    }

    enumResultFree(&result);
    return 0;
}

/* Test 4: Monte Carlo sampling for Courchevel */
static int test_courchevel_monte_carlo(void)
{
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    enum_result_t result;

    /* Player 1: AsKsQsJs5s */
    pockets[0] = str_to_mask("AsKsQsJs5s");

    /* Player 2: AhAd8h8d2c */
    pockets[1] = str_to_mask("AhAd8h8d2c");

    /* Flop with 3 cards - Monte Carlo will enumerate turn+river */
    board = str_to_mask("Ts9s2d");
    
    /* Dead cards = all pocket cards to avoid conflicts */
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    memset(&result, 0, sizeof(result));

    /* Monte Carlo with 10000 iterations */
    int nboard = StdDeck_numCards(board);
    int err = enumSample(game_courchevel, pockets, board, dead, 2, nboard, 10000, 0, &result);
    if (err != 0)
    {
        fprintf(stderr, "enumSample failed with error %d\n", err);
        return 1;
    }

    /* Verify we got reasonable results */
    if (result.nsamples < 9000)
    {
        fprintf(stderr, "Not enough samples: %d\n", result.nsamples);
        return 2;
    }

    /* Both players should have non-trivial equity */
    if (result.ev[0] < 0.20 || result.ev[1] < 0.20)
    {
        fprintf(stderr, "Unexpected equity distribution: P1=%.2f%% P2=%.2f%%\n",
                result.ev[0] * 100, result.ev[1] * 100);
        return 3;
    }

    enumResultFree(&result);
    return 0;
}

/* Test 5: Courchevel Hi/Lo Monte Carlo */
static int test_courchevel8_monte_carlo(void)
{
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    enum_result_t result;

    /* Player 1: As2s3h4h5c (nut low draws) */
    pockets[0] = str_to_mask("As2s3h4h5c");

    /* Player 2: KhKdQsQd9c (high only) */
    pockets[1] = str_to_mask("KhKdQsQd9c");

    /* Flop with low cards */
    board = str_to_mask("6d7c8s");

    /* Dead cards = all pocket cards to avoid conflicts */
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    memset(&result, 0, sizeof(result));

    /* Monte Carlo with 10000 iterations */
    int nboard = StdDeck_numCards(board);
    int err = enumSample(game_courchevel8, pockets, board, dead, 2, nboard, 10000, 0, &result);
    if (err != 0)
    {
        fprintf(stderr, "enumSample Hi/Lo failed with error %d\n", err);
        return 1;
    }

    /* Player 1 with low draws and straight draw should have reasonable equity */
    if (result.ev[0] < 0.30)
    {
        fprintf(stderr, "Player 1 should have better equity with low draws\n");
        return 2;
    }

    enumResultFree(&result);
    return 0;
}

/* Test 6: Verify game parameters */
static int test_courchevel_params(void)
{
    enum_gameparams_t *params;

    /* Courchevel Hi */
    params = enumGameParams(game_courchevel);
    if (!params)
    {
        fprintf(stderr, "No params for game_courchevel\n");
        return 1;
    }

    if (params->minpocket != 5 || params->maxpocket != 5)
    {
        fprintf(stderr, "Wrong pocket size: min=%d max=%d\n",
                params->minpocket, params->maxpocket);
        return 2;
    }

    if (params->maxboard != 5)
    {
        fprintf(stderr, "Wrong max board: %d\n", params->maxboard);
        return 3;
    }

    if (params->haslopot != 0)
    {
        fprintf(stderr, "Courchevel Hi should not have lo pot\n");
        return 4;
    }

    /* Courchevel Hi/Lo */
    params = enumGameParams(game_courchevel8);
    if (!params)
    {
        fprintf(stderr, "No params for game_courchevel8\n");
        return 5;
    }

    if (params->haslopot != 1)
    {
        fprintf(stderr, "Courchevel8 should have lo pot\n");
        return 6;
    }

    printf("  Game params verified: 5 hole cards, 5 board cards\n");
    return 0;
}

int main(void)
{
    int result;
    int passed = 0;
    int failed = 0;

    printf("Running Courchevel tests...\n\n");

    /* Test 1: Basic evaluation */
    result = test_courchevel_basic_eval();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Basic Courchevel evaluation (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Basic Courchevel evaluation\n");
        passed++;
    }

    /* Test 2: Flop test */
    result = test_courchevel_flop();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Flop test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Flop test\n");
        passed++;
    }

    /* Test 3: Hi/Lo evaluation */
    result = test_courchevel8_hilo();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Courchevel Hi/Lo test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Courchevel Hi/Lo test\n");
        passed++;
    }

    /* Test 4: Monte Carlo */
    result = test_courchevel_monte_carlo();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Monte Carlo test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Monte Carlo test\n");
        passed++;
    }

    /* Test 5: Hi/Lo Monte Carlo */
    result = test_courchevel8_monte_carlo();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Hi/Lo Monte Carlo test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Hi/Lo Monte Carlo test\n");
        passed++;
    }

    /* Test 6: Game parameters */
    result = test_courchevel_params();
    if (result != 0)
    {
        fprintf(stderr, "FAIL: Game parameters test (error %d)\n", result);
        failed++;
    }
    else
    {
        printf("PASS: Game parameters test\n");
        passed++;
    }

    printf("\n========================================\n");
    printf("Courchevel Tests: %d passed, %d failed\n", passed, failed);
    printf("========================================\n");

    return failed > 0 ? 1 : 0;
}
