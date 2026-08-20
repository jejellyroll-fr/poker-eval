/*
 * Unit tests for the generalized draw optimizer (pe_compute_draw_optima_fn).
 *
 * The function maximizes an arbitrary client-supplied value function over the
 * 32 discard masks; these tests pin the documented spot-check where the
 * paytable objective and the equity objective disagree (4-to-a-royal vs made
 * flush), cross-check the enumeration against closed forms, and prove
 * equivalence with the legacy pe_compute_draw_optima when the value function
 * is the hand strength.
 *
 * Run via CTest (registered with add_unity_test). A failing assertion aborts
 * the process with a non-zero exit code, which CTest reports as a failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/equity/draw_optimizer.h>

static StdDeck_CardMask make_hand(int c0, int c1, int c2, int c3, int c4)
{
    StdDeck_CardMask h;
    StdDeck_CardMask_RESET(h);
    StdDeck_CardMask_SET(h, c0);
    StdDeck_CardMask_SET(h, c1);
    StdDeck_CardMask_SET(h, c2);
    StdDeck_CardMask_SET(h, c3);
    StdDeck_CardMask_SET(h, c4);
    return h;
}

/* Value = 1 if the hand is unchanged (all five original cards), else 0:
 * maximizing means keeping everything. */
typedef struct {
    StdDeck_CardMask original;
} match_ctx_t;

static double value_same_hand(StdDeck_CardMask hand, void *vctx)
{
    match_ctx_t *ctx = (match_ctx_t *)vctx;
    return StdDeck_CardMask_EQUAL(hand, ctx->original) ? 1.0 : 0.0;
}

/* Value = 1 if the hand shares no card with the original deal (all five cards
 * drawn from the pool), else 0: maximizing means discarding everything. */
static double value_disjoint_hand(StdDeck_CardMask hand, void *vctx)
{
    match_ctx_t *ctx = (match_ctx_t *)vctx;
    for (int c = 0; c < StdDeck_N_CARDS; c++)
        if (StdDeck_CardMask_CARD_IS_SET(hand, c) &&
            StdDeck_CardMask_CARD_IS_SET(ctx->original, c))
            return 0.0;
    return 1.0;
}

/* Value = 1 if the hand contains the queen of spades, else 0. */
static double value_has_queen_spades(StdDeck_CardMask hand, void *ctx)
{
    (void)ctx;
    int qs = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    return StdDeck_CardMask_CARD_IS_SET(hand, qs) ? 1.0 : 0.0;
}

static double value_always_negative(StdDeck_CardMask hand, void *ctx)
{
    (void)hand;
    (void)ctx;
    return -1.0;
}

/* Value = hand strength: used to prove equivalence with the legacy API. */
typedef struct {
    enum_game_t game;
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
} strength_ctx_t;

static double value_hand_strength(StdDeck_CardMask hand, void *vctx)
{
    strength_ctx_t *ctx = (strength_ctx_t *)vctx;
    double s;
    if (pe_draw_hand_strength(ctx->game, hand, ctx->board, ctx->dead, &s) != 0)
        return 0.0;
    return s;
}

/* 1. Keep-all value function: mask 0 (keep everything) wins. */
static void test_keep_all_value_fn(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    match_ctx_t ctx = { hand };
    pe_draw_result_t res;
    int rc = pe_compute_draw_optima_fn(hand, board, dead, value_same_hand, &ctx,
                                       &res);
    assert(rc == 0);
    assert(res.optimal_mask == 0);
    assert(res.max_equity == 1.0);
    for (int m = 1; m < 32; m++)
        assert(res.options[m].expected_equity == 0.0);
    printf("  keep-all value fn ok\n");
}

/* 2. Disjoint value function: discard-all (mask 31) wins. */
static void test_discard_all_value_fn(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    match_ctx_t ctx = { hand };
    pe_draw_result_t res;
    int rc = pe_compute_draw_optima_fn(hand, board, dead, value_disjoint_hand,
                                       &ctx, &res);
    assert(rc == 0);
    assert(res.optimal_mask == 31);
    assert(res.max_equity == 1.0);
    for (int m = 0; m < 31; m++)
        assert(res.options[m].expected_equity == 0.0);
    printf("  discard-all value fn ok\n");
}

/* Value = 800 for a royal flush, 6 for any other flush, else 0. */
static double royal_value(StdDeck_CardMask h, void *ctx)
{
    (void)ctx;
    int hearts = (int)StdDeck_CardMask_HEARTS(h);
    int n = 0;
    for (int r = 0; r < StdDeck_Rank_COUNT; r++)
        n += (hearts >> r) & 1;
    if (n != 5)
        return 0.0;
    int royal = 1;
    for (int r = StdDeck_Rank_TEN; r <= StdDeck_Rank_ACE; r++)
        royal &= (hearts >> r) & 1;
    return royal ? 800.0 : 6.0;
}

/* 3. Documented spot-check: A K Q J hearts + 9 hearts is a made flush and a
 *    4-to-a-royal. Under a paytable objective (royal=800, flush=6) the 9 is
 *    discarded for one card at the royal: EV = (800 + 7*6)/47 = 842/47 (the
 *    10 of hearts completes the royal, the 7 remaining hearts a flush). The
 *    equity objective keeps the made flush instead, so the two disagree. */
static void test_paytable_objective_spot_check(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    pe_draw_result_t res;
    int rc = pe_compute_draw_optima_fn(hand, board, dead, royal_value, NULL,
                                       &res);
    assert(rc == 0);

    /* Discarding the 9 of hearts draws one card at the royal. The mask bit
     * positions follow ascending card order, so find the 9 of hearts first. */
    int hc[5];
    int nh = 0;
    for (int c = 0; c < StdDeck_N_CARDS; c++)
        if (StdDeck_CardMask_CARD_IS_SET(hand, c))
            hc[nh++] = c;
    assert(nh == 5);
    int off_mask = -1;
    for (int p = 0; p < 5; p++)
        if (StdDeck_RANK(hc[p]) == StdDeck_Rank_9 &&
            StdDeck_SUIT(hc[p]) == StdDeck_Suit_HEARTS)
            off_mask = 1 << p;
    assert(off_mask > 0);
    assert(res.optimal_mask == off_mask);
    double expected = (800.0 + 6.0 * 7.0) / 47.0; /* 842/47 ~ 17.915 */
    assert(res.options[off_mask].expected_equity >= expected - 1e-9);
    assert(res.options[off_mask].expected_equity <= expected + 1e-9);
    assert(res.options[0].expected_equity >= 6.0 - 1e-12);
    assert(res.options[0].expected_equity <= 6.0 + 1e-12);

    /* The legacy equity objective must disagree (keeps the made flush). */
    pe_draw_result_t eq;
    assert(pe_compute_draw_optima(game_5draw, hand, board, dead, &eq) == 0);
    assert(eq.optimal_mask == 0);
    assert(eq.optimal_mask != res.optimal_mask);
    printf("  paytable vs equity objective spot-check ok (EV=%.9f)\n",
           res.options[off_mask].expected_equity);
}

/* 4. Exact cross-check against a closed form with a tiny pool: the dead mask
 *    restricts the draw pool to three cards, so every option's EV is analytic
 *    (hypergeometric probability of drawing the queen of spades). */
static void test_exact_enumeration_small_pool(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));
    int s0 = StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);
    int s1 = StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);
    int s2 = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    StdDeck_CardMask union_hs = hand;
    StdDeck_CardMask_SET(union_hs, s0);
    StdDeck_CardMask_SET(union_hs, s1);
    StdDeck_CardMask_SET(union_hs, s2);
    StdDeck_CardMask dead;
    StdDeck_CardMask_NOT(dead, union_hs);
    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);

    pe_draw_result_t res;
    int rc = pe_compute_draw_optima_fn(hand, board, dead, value_has_queen_spades,
                                       NULL, &res);
    assert(rc == 0);

    /* P(draw includes Qs) for k draws from a 3-card pool = C(2,k-1)/C(3,k);
     * draws larger than the pool are invalid and value 0. */
    for (int m = 0; m < 32; m++) {
        int k = res.options[m].cards_drawn;
        double expected;
        if (k > 3)
            expected = 0.0;
        else if (k == 0)
            expected = 0.0;
        else if (k == 1)
            expected = 1.0 / 3.0;
        else if (k == 2)
            expected = 2.0 / 3.0;
        else
            expected = 1.0;
        assert(res.options[m].expected_equity >= expected - 1e-12);
        assert(res.options[m].expected_equity <= expected + 1e-12);
    }
    /* The best valid draw is any k=3 mask (EV 1.0); the first such mask is
     * 0b00111 = 7. */
    assert(res.optimal_mask == 7);
    assert(res.max_equity == 1.0);
    printf("  exact enumeration cross-check ok\n");
}

/* 5. Impossible masks must not beat valid masks when callback values are
 *    negative. */
static void test_impossible_masks_are_excluded(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));
    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask allowed = hand;
    StdDeck_CardMask_SET(allowed, StdDeck_MAKE_CARD(StdDeck_Rank_TEN,
                                                    StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(allowed, StdDeck_MAKE_CARD(StdDeck_Rank_JACK,
                                                    StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask dead;
    StdDeck_CardMask_NOT(dead, allowed);

    pe_draw_result_t res;
    assert(pe_compute_draw_optima_fn(hand, board, dead,
                                     value_always_negative, NULL, &res) == 0);
    assert(res.optimal_mask == 0);
    assert(res.max_equity == -1.0);
    assert(res.options[7].cards_drawn == 3);
    assert(res.options[7].expected_equity == 0.0);
    printf("  impossible masks excluded from argmax ok\n");
}

/* 5. Equivalence: with hand strength as the value function the generic entry
 *    point must reproduce the legacy pe_compute_draw_optima bit for bit. */
static void test_equivalence_with_legacy(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    strength_ctx_t sctx = { game_5draw, board, dead };

    pe_draw_result_t fn;
    int rc = pe_compute_draw_optima_fn(hand, board, dead, value_hand_strength,
                                       &sctx, &fn);
    assert(rc == 0);

    pe_draw_result_t legacy;
    assert(pe_compute_draw_optima(game_5draw, hand, board, dead, &legacy) == 0);

    assert(fn.num_options == legacy.num_options);
    for (int m = 0; m < 32; m++) {
        assert(fn.options[m].discard_mask == legacy.options[m].discard_mask);
        assert(fn.options[m].cards_drawn == legacy.options[m].cards_drawn);
        assert(fn.options[m].expected_equity ==
               legacy.options[m].expected_equity);
    }
    assert(fn.optimal_mask == legacy.optimal_mask);
    assert(fn.max_equity == legacy.max_equity);
    printf("  equivalence with legacy optimizer ok\n");
}

/* 6. Error handling: null callback, null result, wrong card count. */
static void test_error_handling(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    assert(pe_compute_draw_optima_fn(hand, board, dead, NULL, NULL,
                                     &(pe_draw_result_t){0}) != 0);
    assert(pe_compute_draw_optima_fn(hand, board, dead, value_same_hand, NULL,
                                     NULL) != 0);

    StdDeck_CardMask four;
    StdDeck_CardMask_RESET(four);
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    assert(pe_compute_draw_optima_fn(four, board, dead, value_same_hand, NULL,
                                     &(pe_draw_result_t){0}) != 0);
    printf("  error handling ok\n");
}

int main(void)
{
    printf("=== Draw Optimizer (fn) test suite ===\n");
    test_keep_all_value_fn();
    test_discard_all_value_fn();
    test_paytable_objective_spot_check();
    test_exact_enumeration_small_pool();
    test_impossible_masks_are_excluded();
    test_equivalence_with_legacy();
    test_error_handling();
    printf("=== All Draw Optimizer (fn) tests passed ===\n");
    return 0;
}
