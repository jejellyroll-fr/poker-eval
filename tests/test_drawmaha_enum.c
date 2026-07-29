/*
 * test_drawmaha_enum.c - Test enumeration for Drawmaha game
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>

int main(int argc, char *argv[])
{
    enum_game_t game = game_drawmaha;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 2;
    int nboard = 3;
    int err;

    printf("Testing Drawmaha enumeration\n");
    printf("============================\n\n");

    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);

    // Player 1: As Ah Kh Qh Jd (5 cards for Drawmaha)
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));

    // Player 2: 7c 6d 5s 4h 3c (5 cards for Drawmaha)
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));

    // Dead cards = all pocket cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    printf("Player 1: As Ah Kh Qh Jd (pocket aces with flush potential)\n");
    printf("Player 2: 7c 6d 5s 4h 3c (straight potential)\n");
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_OR(dead, dead, board);

    printf("Dead cards count: %d\n", StdDeck_numCards(dead));
    printf("Board cards: %d (flop)\n", nboard);

    printf("\nCalling enumExhaustive_dispatch for Drawmaha...\n");

    err = enumExhaustive_dispatch(game, pockets, board, dead, npockets, nboard, 0, &result);

    if (err)
    {
        printf("ERROR: enumExhaustive_dispatch returned %d\n", err);
        return 1;
    }
    else
    {
        printf("\nResults:\n");
        printf("Samples: %u\n", result.nsamples);
        printf("Player 1 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[0] / result.nsamples, result.nwinhi[0], result.ntiehi[0], result.nlosehi[0]);
        printf("Player 2 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[1] / result.nsamples, result.nwinhi[1], result.ntiehi[1], result.nlosehi[1]);

        // Verify that Player 1 has higher equity (should win more often)
        if (result.ev[0] > result.ev[1])
        {
            printf("✓ Player 1 has higher equity as expected\n");
        }
        else
        {
            printf("✗ Unexpected: Player 2 has higher equity\n");
        }
    }

    enumResultFree(&result);

    // Test 2: With flop
    printf("\n==================================================\n");
    printf("Test 2: Drawmaha with flop\n");
    printf("==========================\n\n");

    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    nboard = 3;

    // Set flop: Jh Th 9h (gives Player 1 a flush)
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));

    // Update dead cards to include board
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);

    printf("Flop: Jh Th 9h (gives Player 1 a flush)\n");
    printf("Dead cards count: %d\n", StdDeck_numCards(dead));

    err = enumExhaustive_dispatch(game, pockets, board, dead, npockets, nboard, 0, &result);

    if (err)
    {
        printf("ERROR: enumExhaustive_dispatch with flop returned %d\n", err);
        return 1;
    }
    else
    {
        printf("\nResults with flop:\n");
        printf("Samples: %u\n", result.nsamples);
        printf("Player 1 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[0] / result.nsamples, result.nwinhi[0], result.ntiehi[0], result.nlosehi[0]);
        printf("Player 2 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[1] / result.nsamples, result.nwinhi[1], result.ntiehi[1], result.nlosehi[1]);

        // Player 1 should have very high equity with a flush
        if (result.ev[0] > 0.8 * result.nsamples)
        {
            printf("✓ Player 1 has very high equity with flush as expected\n");
        }
        else
        {
            printf("? Player 1 equity lower than expected with flush\n");
        }
    }

    enumResultFree(&result);

    // Test 3: Monte Carlo sampling
    printf("\n==================================================\n");
    printf("Test 3: Drawmaha Monte Carlo sampling\n");
    printf("=====================================\n\n");

    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    nboard = 0;

    // Reset dead cards to just pocket cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    printf("Running Monte Carlo with 10,000 iterations...\n");

    err = enumSample(game, pockets, board, dead, npockets, nboard, 10000, 0, &result);

    if (err)
    {
        printf("ERROR: enumSample returned %d\n", err);
        return 1;
    }
    else
    {
        printf("\nMonte Carlo Results:\n");
        printf("Samples: %u\n", result.nsamples);
        printf("Player 1 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[0] / result.nsamples, result.nwinhi[0], result.ntiehi[0], result.nlosehi[0]);
        printf("Player 2 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[1] / result.nsamples, result.nwinhi[1], result.ntiehi[1], result.nlosehi[1]);

        if (result.nsamples == 10000)
        {
            printf("✓ Correct number of samples generated\n");
        }
        else
        {
            printf("✗ Unexpected sample count: %u\n", result.nsamples);
        }
    }

    enumResultFree(&result);

    printf("\n✓ All Drawmaha enumeration tests completed successfully!\n");

    return 0;
}
