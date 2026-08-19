/*
 * Unit tests for the Generic Draw Decision & Equity Optimizer
 * (pe_compute_draw_optima / pe_draw_hand_strength).
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

/* Ascending-order card indices of a 5-card hand. */
static void hand_cards(StdDeck_CardMask h, int out[5])
{
    int n = 0;
    for (int c = 0; c < StdDeck_N_CARDS; c++)
        if (StdDeck_CardMask_CARD_IS_SET(h, c))
            out[n++] = c;
}

static StdDeck_CardMask full_deck(void)
{
    StdDeck_CardMask d;
    StdDeck_CardMask_RESET(d);
    for (int c = 0; c < StdDeck_N_CARDS; c++)
        StdDeck_CardMask_SET(d, c);
    return d;
}

/* 1. Basic invariants for a high 5-card draw hand. */
static void test_basic_invariants(void)
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

    pe_draw_result_t res;
    int rc = pe_compute_draw_optima(game_5draw, hand, board, dead, &res);
    assert(rc == 0);

    assert(res.num_options == 32);
    int best = 0;
    for (int m = 0; m < 32; m++) {
        double e = res.options[m].expected_equity;
        assert(e >= 0.0 && e <= 1.0);
        assert(res.options[m].discard_mask == m);
        assert(res.options[m].cards_drawn ==
               ((m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) +
                ((m >> 3) & 1) + ((m >> 4) & 1)));
        if (e > res.options[best].expected_equity)
            best = m;
    }
    assert(res.optimal_mask == best);
    assert(res.max_equity == res.options[best].expected_equity);
    assert(res.max_equity >= 0.0 && res.max_equity <= 1.0);
    printf("  basic invariants ok (optimal mask=%d, max_equity=%.6f)\n",
           res.optimal_mask, res.max_equity);
}

/* 2. A made royal flush cannot be improved: keep everything (mask 0) is best. */
static void test_made_royal_keeps_all(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    pe_draw_result_t res;
    int rc = pe_compute_draw_optima(game_5draw, hand, board, dead, &res);
    assert(rc == 0);
    assert(res.optimal_mask == 0);

    /* Every 1-card discard must be strictly worse than keeping the royal. */
    for (int p = 0; p < 5; p++) {
        int mask = 1 << p;
        assert(res.options[mask].expected_equity < res.options[0].expected_equity);
    }
    printf("  made royal flush -> keep all (mask 0) ok\n");
}

/* 3. Four to a flush: discarding the single offsuit card dominates. */
static void test_four_to_flush_draws_offsuit(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS)); /* offsuit */

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    pe_draw_result_t res;
    int rc = pe_compute_draw_optima(game_5draw, hand, board, dead, &res);
    assert(rc == 0);

    /* Find the discard mask that removes exactly the offsuit card. */
    int hc[5];
    hand_cards(hand, hc);
    int off_pos = -1;
    for (int p = 0; p < 5; p++) {
        if (StdDeck_Suit_CLUBS == StdDeck_SUIT(hc[p]) &&
            StdDeck_Rank_2 == StdDeck_RANK(hc[p])) {
            off_pos = p;
            break;
        }
    }
    assert(off_pos >= 0);
    int off_mask = 1 << off_pos;

    assert(res.options[off_mask].expected_equity > res.options[0].expected_equity);
    assert(res.optimal_mask == off_mask);

    /* Among all single-card discards, discarding the offsuit is best. */
    for (int p = 0; p < 5; p++) {
        int mask = 1 << p;
        if (mask == off_mask)
            continue;
        assert(res.options[off_mask].expected_equity >=
               res.options[mask].expected_equity);
    }
    printf("  4-to-a-flush -> discard offsuit (mask %d) ok\n", off_mask);
}

/* 4. Exact cross-check: with a tiny available pool the optimizer's average must
 *    match an independent brute-force recomputation using pe_draw_hand_strength. */
static void test_exact_enumeration_small_pool(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));

    /* Available pool is forced to exactly three cards via the dead mask. */
    int s0 = StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);
    int s1 = StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);
    int s2 = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    StdDeck_CardMask union_hs = hand;
    StdDeck_CardMask_SET(union_hs, s0);
    StdDeck_CardMask_SET(union_hs, s1);
    StdDeck_CardMask_SET(union_hs, s2);

    StdDeck_CardMask dead;
    StdDeck_CardMask_NOT(dead, union_hs); /* everything except hand + S */

    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);

    pe_draw_result_t res;
    int rc = pe_compute_draw_optima(game_5draw, hand, board, dead, &res);
    assert(rc == 0);

    int hc[5];
    hand_cards(hand, hc);

    /* k = 0: EV(keep all) must equal the single-hand strength. */
    double full_strength;
    assert(pe_draw_hand_strength(game_5draw, hand, board, dead, &full_strength) == 0);
    assert(res.options[0].expected_equity == full_strength);

    /* For each single-discard mask, recompute the average over S and compare. */
    int pool[3] = { s0, s1, s2 };
    for (int p = 0; p < 5; p++) {
        int mask = 1 << p;
        (void)mask;
        StdDeck_CardMask kept = hand;
        StdDeck_CardMask_UNSET(kept, hc[p]);

        double expected = 0.0;
        for (int t = 0; t < 3; t++) {
            StdDeck_CardMask res_h = kept;
            StdDeck_CardMask_SET(res_h, pool[t]);
            double s;
            assert(pe_draw_hand_strength(game_5draw, res_h, board, dead, &s) == 0);
            expected += s;
        }
        expected /= 3.0;

        assert(res.options[mask].expected_equity >= expected - 1e-12);
        assert(res.options[mask].expected_equity <= expected + 1e-12);
    }
    printf("  exact enumeration cross-check ok\n");
}

/* 5. Drawmaha no longer returns placeholder 0.5 values. */
static void test_drawmaha_not_placeholder(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));

    /* Flop (plus two spares) for the Omaha half. */
    StdDeck_CardMask board = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    pe_draw_result_t res;
    int rc = pe_compute_draw_optima(game_drawmaha, hand, board, dead, &res);
    assert(rc == 0);

    int all_half = 1;
    int in_range = 1;
    for (int m = 0; m < 32; m++) {
        double e = res.options[m].expected_equity;
        if (e < 0.0 || e > 1.0)
            in_range = 0;
        if (e != 0.5)
            all_half = 0;
    }
    assert(in_range);
    assert(!all_half);
    assert(res.max_equity >= 0.0 && res.max_equity <= 1.0);
    printf("  drawmaha produced varied equities (max=%.6f) ok\n",
           res.max_equity);
}

/* 6. Other draw games return valid results (smoke test). */
static void test_other_draw_games(void)
{
    StdDeck_CardMask hand = make_hand(
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enum_game_t games[] = { game_lowball, game_lowball27, game_27_triple_draw,
                             game_a5_triple_draw, game_badugi, game_5draw8 };
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++) {
        pe_draw_result_t res;
        int rc = pe_compute_draw_optima(games[i], hand, board, dead, &res);
        assert(rc == 0);
        assert(res.num_options == 32);
        for (int m = 0; m < 32; m++)
            assert(res.options[m].expected_equity >= 0.0 &&
                   res.options[m].expected_equity <= 1.0);
    }
    printf("  low/badugi draw games ok\n");
}

/* 7. Error handling: unsupported game and malformed inputs. */
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

    assert(pe_compute_draw_optima(game_holdem, hand, board, dead, NULL) != 0);
    assert(pe_compute_draw_optima(game_holdem, hand, board, dead,
                                  &(pe_draw_result_t){0}) != 0);

    /* Wrong card count. */
    StdDeck_CardMask four;
    StdDeck_CardMask_RESET(four);
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(four, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    assert(pe_compute_draw_optima(game_5draw, four, board, dead,
                                  &(pe_draw_result_t){0}) != 0);

    double s;
    assert(pe_draw_hand_strength(game_holdem, hand, board, dead, &s) != 0);
    assert(pe_draw_hand_strength(game_5draw, four, board, dead, &s) != 0);
    assert(pe_draw_hand_strength(game_5draw, hand, board, dead, NULL) != 0);
    printf("  error handling ok\n");
}

int main(void)
{
    printf("=== Draw Optimizer test suite ===\n");
    test_basic_invariants();
    test_made_royal_keeps_all();
    test_four_to_flush_draws_offsuit();
    test_exact_enumeration_small_pool();
    test_drawmaha_not_placeholder();
    test_other_draw_games();
    test_error_handling();
    printf("=== All Draw Optimizer tests passed ===\n");
    return 0;
}
