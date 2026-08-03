#ifdef POKER_EVAL_EXPERIMENTAL

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <poker_eval/core/poker_eval_modern.h>

int main(void)
{
    poker_eval_context_t *ctx = NULL;
    poker_eval_hand_t *hand1 = NULL;
    poker_eval_hand_t *hand2 = NULL;
    poker_eval_hand_t *board = NULL;
    const poker_eval_hand_t *hands[2];
    poker_equity_result_t results[2];
    poker_eval_error_t err;
    int failures = 0;

    err = poker_eval_context_create(POKER_DECK_STANDARD, &ctx);
    if (err != POKER_EVAL_SUCCESS)
    {
        printf("FAIL: context create: %d\n", err);
        return 1;
    }

    poker_eval_hand_create(&hand1);
    poker_eval_hand_create(&hand2);
    poker_eval_hand_create(&board);

    poker_eval_hand_add_card(hand1, POKER_RANK_ACE, POKER_SUIT_SPADES);
    poker_eval_hand_add_card(hand1, POKER_RANK_ACE, POKER_SUIT_HEARTS);

    poker_eval_hand_add_card(hand2, POKER_RANK_KING, POKER_SUIT_SPADES);
    poker_eval_hand_add_card(hand2, POKER_RANK_KING, POKER_SUIT_HEARTS);

    poker_eval_hand_add_card(board, POKER_RANK_TWO, POKER_SUIT_CLUBS);
    poker_eval_hand_add_card(board, POKER_RANK_THREE, POKER_SUIT_DIAMONDS);
    poker_eval_hand_add_card(board, POKER_RANK_FOUR, POKER_SUIT_CLUBS);

    hands[0] = hand1;
    hands[1] = hand2;

    err = poker_eval_calculate_equity(ctx, hands, 2, board, NULL, results);
    if (err != POKER_EVAL_SUCCESS)
    {
        printf("FAIL: calculate_equity: %d\n", err);
        failures++;
    }
    else
    {
        printf("Exhaustive equity (AA vs KK, flop 234):\n");
        printf("  AA: win=%.4f tie=%.4f lose=%.4f (outcomes=%llu)\n",
               results[0].win_probability, results[0].tie_probability,
               results[0].lose_probability,
               (unsigned long long)results[0].total_outcomes);
        printf("  KK: win=%.4f tie=%.4f lose=%.4f (outcomes=%llu)\n",
               results[1].win_probability, results[1].tie_probability,
               results[1].lose_probability,
               (unsigned long long)results[1].total_outcomes);

        if (results[0].total_outcomes == 0)
        {
            printf("FAIL: expected non-zero outcomes\n");
            failures++;
        }
        if (fabs(results[0].win_probability +
                 results[0].tie_probability +
                 results[0].lose_probability - 1.0) > 1e-9)
        {
            printf("FAIL: AA probabilities do not sum to 1\n");
            failures++;
        }
        if (fabs(results[0].win_probability +
                 results[0].tie_probability +
                 results[0].lose_probability - 1.0) > 1e-9)
        {
            printf("FAIL: KK probabilities do not sum to 1\n");
            failures++;
        }
    }

    err = poker_eval_calculate_equity_monte_carlo(ctx, hands, 2, board, NULL,
                                                  20000, results);
    if (err != POKER_EVAL_SUCCESS)
    {
        printf("FAIL: calculate_equity_monte_carlo: %d\n", err);
        failures++;
    }
    else
    {
        printf("Monte Carlo equity (AA vs KK, flop 234):\n");
        printf("  AA: win=%.4f tie=%.4f lose=%.4f (outcomes=%llu)\n",
               results[0].win_probability, results[0].tie_probability,
               results[0].lose_probability,
               (unsigned long long)results[0].total_outcomes);
        if (results[0].total_outcomes == 0)
        {
            printf("FAIL: MC expected non-zero outcomes\n");
            failures++;
        }
    }

    poker_eval_hand_destroy(board);
    poker_eval_hand_destroy(hand2);
    poker_eval_hand_destroy(hand1);
    poker_eval_context_destroy(ctx);

    if (failures == 0)
    {
        printf("ALL PASS\n");
        return 0;
    }
    return 1;
}

#else
int main(void)
{
    printf("SKIPPED (POKER_EVAL_EXPERIMENTAL not defined)\n");
    return 0;
}
#endif
