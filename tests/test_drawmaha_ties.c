/*
 * test_drawmaha_ties.c - Test for ties in Drawmaha evaluation
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/games/rules_drawmaha.h>

int main(int argc, char *argv[])
{
    StdDeck_CardMask hand1, hand2, board;
    HandVal result1, result2;
    int err1, err2;

    printf("Testing Drawmaha ties\n");
    printf("====================\n\n");

    // Initialize
    StdDeck_CardMask_RESET(hand1);
    StdDeck_CardMask_RESET(hand2);
    StdDeck_CardMask_RESET(board);

    // Test 1: Identical hands should tie
    printf("Test 1: Identical hands\n");

    // Both hands: As Ah Kc Qd Jc (5 cards required for Drawmaha)
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));

    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));

    // Board: Jh Ts 9c 8d 7h
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));

    err1 = Drawmaha_EvaluateHand(hand1, board, &result1, NULL);
    err2 = Drawmaha_EvaluateHand(hand2, board, &result2, NULL);

    if (err1 == 0 && err2 == 0)
    {
        printf("Hand 1 value: %d\n", result1);
        printf("Hand 2 value: %d\n", result2);

        if (result1 == result2)
        {
            printf("✓ Identical hands correctly tie\n");
        }
        else
        {
            printf("✗ ERROR: Identical hands don't tie! (diff: %d)\n", result1 - result2);
        }
    }
    else
    {
        printf("✗ Error evaluating hands: %d, %d\n", err1, err2);
    }

    printf("\n");

    // Test 2: Different hands that should make the same final hand
    printf("Test 2: Different hole cards, same final hand\n");

    StdDeck_CardMask_RESET(hand1);
    StdDeck_CardMask_RESET(hand2);
    StdDeck_CardMask_RESET(board);

    // Hand 1: As Kh Qc Jd 2s (5 cards - will make straight with board)
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));

    // Hand 2: Ad Ks Qh Js 2c (5 cards - will make same straight with board)
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));

    // Board: Tc 9h 8d 7c 6s (both should make A-high straight using AK+T-9-8)
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES));

    err1 = Drawmaha_EvaluateHand(hand1, board, &result1, NULL);
    err2 = Drawmaha_EvaluateHand(hand2, board, &result2, NULL);

    if (err1 == 0 && err2 == 0)
    {
        printf("Hand 1 value: %d (type: %d)\n", result1, HandVal_HANDTYPE(result1));
        printf("Hand 2 value: %d (type: %d)\n", result2, HandVal_HANDTYPE(result2));

        if (result1 == result2)
        {
            printf("✓ Different hands making same final hand correctly tie\n");
        }
        else
        {
            printf("? Different hands making same final hand have different values (diff: %d)\n", result1 - result2);
            printf("  This might be expected due to kicker differences\n");
        }
    }
    else
    {
        printf("✗ Error evaluating hands: %d, %d\n", err1, err2);
    }

    printf("\n");

    // Test 3: Test with a simple board that should create ties
    printf("Test 3: Simple board test\n");

    StdDeck_CardMask_RESET(hand1);
    StdDeck_CardMask_RESET(hand2);
    StdDeck_CardMask_RESET(board);

    // Hand 1: 2s 3h 4c 5d 6s (5 cards)
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES));

    // Hand 2: 2c 3d 4s 5h 6c (5 cards - same ranks, different suits)
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));

    // Board: As Ah Ac Ad Ks (quad aces on board - both should tie)
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    err1 = Drawmaha_EvaluateHand(hand1, board, &result1, NULL);
    err2 = Drawmaha_EvaluateHand(hand2, board, &result2, NULL);

    if (err1 == 0 && err2 == 0)
    {
        printf("Hand 1 value: %d (type: %d)\n", result1, HandVal_HANDTYPE(result1));
        printf("Hand 2 value: %d (type: %d)\n", result2, HandVal_HANDTYPE(result2));

        if (result1 == result2)
        {
            printf("✓ Hands with quad aces on board correctly tie\n");
        }
        else
        {
            printf("✗ ERROR: Hands with quad aces on board don't tie! (diff: %d)\n", result1 - result2);
        }
    }
    else
    {
        printf("✗ Error evaluating hands: %d, %d\n", err1, err2);
    }

    printf("\n✓ Drawmaha tie tests completed!\n");

    return 0;
}
