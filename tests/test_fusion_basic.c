/*
 * test_fusion_basic.c - Basic test for Fusion poker hand evaluation
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_fusion.h>

int main(int argc, char *argv[])
{
    StdDeck_CardMask pocket, board;
    HandVal result;
    int err;

    printf("Testing basic Fusion poker hand evaluation\n");
    printf("==========================================\n\n");

    // Test 1: Preflop - 2 hole cards, no board
    printf("Test 1: Preflop (2 hole cards)\n");
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    printf("Pocket: As Ah (2 cards)\n");
    printf("Board:  (none)\n");
    printf("Expected: Valid preflop evaluation\n");

    err = Fusion_EvaluateHand(pocket, board, &result, NULL);

    if (err == 0)
    {
        printf("✓ Successfully evaluated preflop hand\n");
        printf("Hand value: %d\n", result);
    }
    else
    {
        printf("✗ Error evaluating preflop hand: %d\n", err);
    }

    printf("\n");

    // Test 2: Flop - 3 hole cards, 3 board cards
    printf("Test 2: Flop (3 hole cards)\n");
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));

    printf("Pocket: As Ah Kc (3 cards)\n");
    printf("Board:  Kh Qs Jc\n");
    printf("Expected: Valid flop evaluation\n");

    err = Fusion_EvaluateHand(pocket, board, &result, NULL);

    if (err == 0)
    {
        printf("✓ Successfully evaluated flop hand\n");
        printf("Hand value: %d\n", result);
    }
    else
    {
        printf("✗ Error evaluating flop hand: %d\n", err);
    }

    printf("\n");

    // Test 3: Turn - 4 hole cards, 4 board cards
    printf("Test 3: Turn (4 hole cards)\n");
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS));

    printf("Pocket: As Ah Kc Qd (4 cards)\n");
    printf("Board:  Kh Qs Jc Td\n");
    printf("Expected: Valid turn evaluation (use Omaha rules)\n");

    err = Fusion_EvaluateHand(pocket, board, &result, NULL);

    if (err == 0)
    {
        printf("✓ Successfully evaluated turn hand\n");
        printf("Hand value: %d\n", result);
    }
    else
    {
        printf("✗ Error evaluating turn hand: %d\n", err);
    }

    printf("\n");

    // Test 4: River - 4 hole cards, 5 board cards
    printf("Test 4: River (4 hole cards)\n");
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));

    printf("Pocket: As Ah Kc Qd (4 cards)\n");
    printf("Board:  Kh Qs Jc Td 9h\n");
    printf("Expected: Valid river evaluation (use Omaha rules)\n");

    err = Fusion_EvaluateHand(pocket, board, &result, NULL);

    if (err == 0)
    {
        printf("✓ Successfully evaluated river hand\n");
        printf("Hand value: %d\n", result);
    }
    else
    {
        printf("✗ Error evaluating river hand: %d\n", err);
    }

    printf("\n✓ Basic Fusion evaluation tests completed!\n");

    return 0;
}
