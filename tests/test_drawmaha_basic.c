/*
 * test_drawmaha_basic.c - Basic test for Drawmaha hand evaluation
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/eval_drawmaha.h>
#include <poker_eval/games/rules_drawmaha.h>

int main(int argc, char *argv[])
{
    StdDeck_CardMask pocket, board;
    HandVal result;
    int err;

    printf("Testing basic Drawmaha hand evaluation\n");
    printf("======================================\n\n");

    // Initialize
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);

    // Test 1: Pocket with pair of aces (5 cards required for Drawmaha)
    printf("Test 1: Pocket Aces\n");
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));

    // Board: K Q J T 9 (gives straight)
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));

    printf("Pocket: As Ah Kc Qd Js (5 cards)\n");
    printf("Board:  Kh Qs Jc Td 9h\n");
    printf("Expected: Straight (A-K-Q-J-T)\n");

    err = Drawmaha_EvaluateHand(pocket, board, &result, NULL);

    if (err == 0)
    {
        printf("Hand value: %d\n", result);
        printf("Hand type: %d\n", HandVal_HANDTYPE(result));

        if (HandVal_HANDTYPE(result) == HandType_STRAIGHT)
        {
            printf("✓ Correctly identified as straight\n");
        }
        else
        {
            printf("? Got hand type %d (expected %d for straight)\n",
                   HandVal_HANDTYPE(result), HandType_STRAIGHT);
        }
    }
    else
    {
        printf("✗ Error evaluating hand: %d\n", err);
    }

    printf("\n");

    // Test 2: Flush
    printf("Test 2: Flush\n");
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);

    // Pocket: mix for flush draw (5 cards required)
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_DIAMONDS));

    // Board: more hearts for flush
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));

    printf("Pocket: Ah Kh Qh 7c 6d (5 cards)\n");
    printf("Board:  Jh Th 9h 5s 4s\n");
    printf("Expected: Flush (hearts)\n");

    err = Drawmaha_EvaluateHand(pocket, board, &result, NULL);

    if (err == 0)
    {
        printf("Hand value: %d\n", result);
        printf("Hand type: %d\n", HandVal_HANDTYPE(result));

        if (HandVal_HANDTYPE(result) == HandType_FLUSH ||
            HandVal_HANDTYPE(result) == HandType_STFLUSH)
        {
            printf("✓ Correctly identified as flush or straight flush\n");
        }
        else
        {
            printf("? Got hand type %d (expected %d for flush)\n",
                   HandVal_HANDTYPE(result), HandType_FLUSH);
        }
    }
    else
    {
        printf("✗ Error evaluating hand: %d\n", err);
    }

    printf("\n");

    // Test 3: Two pair
    printf("Test 3: Two Pair\n");
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);

    // Pocket: pair of kings (5 cards required)
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));

    // Board: pair of queens
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));

    printf("Pocket: Ks Kh 7c 6d 2s (5 cards)\n");
    printf("Board:  Qc Qd As 5h 4s\n");
    printf("Expected: Two Pair (Kings and Queens)\n");

    err = Drawmaha_EvaluateHand(pocket, board, &result, NULL);

    if (err == 0)
    {
        printf("Hand value: %d\n", result);
        printf("Hand type: %d\n", HandVal_HANDTYPE(result));

        if (HandVal_HANDTYPE(result) == HandType_TWOPAIR)
        {
            printf("✓ Correctly identified as two pair\n");
        }
        else
        {
            printf("? Got hand type %d (expected %d for two pair)\n",
                   HandVal_HANDTYPE(result), HandType_TWOPAIR);
        }
    }
    else
    {
        printf("✗ Error evaluating hand: %d\n", err);
    }

    printf("\n✓ Basic Drawmaha evaluation tests completed!\n");

    return 0;
}
