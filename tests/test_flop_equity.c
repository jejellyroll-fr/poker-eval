#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <poker_eval/equity/flop_equity.h>

/* Test 1: Flopped flush equity */
static void test_flopped_flush(void)
{
    printf("Test 1: Flopped flush... ");

    /* Pocket: Ah Kh, Flop: Qh Jh Th (straight flush!) */
    StdDeck_CardMask pocket, flop;
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(flop);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));

    flop_equity_input_t input = {
        .pocket = pocket,
        .flop = flop,
        .n_opponents = 1,
        .n_samples = 0  /* Exhaustive */
    };

    flop_equity_result_t result;
    flop_calc_equity(&input, &result);

    /* Should have ~100% equity */
    assert(result.equity >= 0.99);
    assert(result.prob_flush >= 0.99);

    printf("PASSED (equity: %.2f%%)\n", result.equity * 100);
}

/* Test 2: Flush draw + Overcards */
static void test_flush_draw_overcards(void)
{
    printf("Test 2: Flush draw + Overcards... ");

    /* Pocket: Ah Kh, Flop: Qh 7h 2c */
    /* Flush outs: 9 hearts (gives Flush) */
    /* Pair outs: 6 (A or K) (gives Top Pair) */
    /* Board Pair outs: 9 (Q, 7, 2) (gives One Pair / Two Pair) */
    /* Overlap: 2h is in Flush and Board Pair sets. */
    /* Unique improving cards: 9 + 6 + 8 = 23 cards. */
    /* Turn improvement: 23/47 = 48.936% */

    StdDeck_CardMask pocket, flop;
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(flop);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));

    flop_equity_input_t input = {
        .pocket = pocket,
        .flop = flop,
        .n_opponents = 1,
        .n_samples = 0
    };

    flop_equity_result_t result;
    flop_calc_equity(&input, &result);

    assert(result.prob_flush_draw > 0.0);

    /* Verify turn improvement is exactly 23/47 */
    double expected = 23.0 / 47.0;
    assert(fabs(result.turn_improvement - expected) < 0.001);

    printf("PASSED (turn: %.2f%%, expected: %.2f%%)\n",
           result.turn_improvement * 100, expected * 100);
}

/* Test 3: Gutshot (Pure Draw check) */
static void test_gutshot(void)
{
    printf("Test 3: Gutshot... ");

    /* Pocket: 5s 6s, Flop: 8d 9c 2h */
    /* Gutshot needs 7. 4 outs (7s, 7c, 7h, 7d). */
    /* Check draw_outs field specifically. */

    StdDeck_CardMask pocket, flop;
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(flop);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES));

    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));

    flop_equity_input_t input = {
        .pocket = pocket,
        .flop = flop,
        .n_opponents = 1,
        .n_samples = 0
    };

    flop_equity_result_t result;
    flop_calc_equity(&input, &result);

    /* draw_outs should be 4 (straight outs) + 0 (flush outs) */
    assert(result.draw_outs == 4);
    assert(result.prob_gutshot > 0.0);

    printf("PASSED (outs: %d)\n", result.draw_outs);
}

/* Test 4: Monte Carlo basic sanity */
static void test_monte_carlo(void)
{
    printf("Test 4: Monte Carlo (sanity)... ");

    /* Same hand as Test 2 */
    StdDeck_CardMask pocket, flop;
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(flop);

    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));

    flop_equity_input_t input = {
        .pocket = pocket,
        .flop = flop,
        .n_opponents = 1,
        .n_samples = 10000 /* MC */
    };

    flop_equity_result_t result;
    flop_calc_equity(&input, &result);

    /* MC should be close to Exhaustive equity */
    /* We don't have exhaustive equity value handy, but let's assume > 50% */
    assert(result.equity > 0.50);

    /* MC turn improvement should approximate exact 23/47 within tolerance */
    double expected = 23.0 / 47.0;
    assert(fabs(result.turn_improvement - expected) < 0.015);

    printf("PASSED (equity: %.2f%%)\n", result.equity * 100);
}

int main(void)
{
    printf("=== Flop Equity Tests (Phase 3) ===\n\n");

    test_flopped_flush();
    test_flush_draw_overcards();
    test_gutshot();
    test_monte_carlo();

    printf("\n✅ All flop equity tests PASSED (4/4)\n");
    return 0;
}
