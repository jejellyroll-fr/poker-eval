/*
 * test_transition_distribution.c
 *
 * ISSUE-06 (#162): validates the board transition & turn/river hand
 * improvement probability matrix:
 *   - flush draw turn/river odds:  9/47  ~= 19.1489%,  ~= 34.9676%
 *   - open-ended straight draw:    8/47  ~= 17.0213%,  ~= 31.4524%
 *   - the turn -> river transition matrix rows are proper probability
 *     distributions (sum to 1).
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/distributions/transition_distribution.h>

/* Build a card mask from a "rank,suit" where rank 0='2'..12='A' and
 * suit 0..3. Used so flush draws share a suit index. */
static StdDeck_CardMask card(int rank, int suit) {
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    StdDeck_CardMask_SET(m, StdDeck_MAKE_CARD(rank, suit));
    return m;
}
static StdDeck_CardMask add(StdDeck_CardMask a, StdDeck_CardMask b) {
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    StdDeck_CardMask_OR(m, m, a);
    StdDeck_CardMask_OR(m, m, b);
    return m;
}

/* AKhh on Qh 7h 2c : a flush draw (4 to a flush). */
static StdDeck_CardMask flush_draw_hand(void) {
    StdDeck_CardMask h = card(12, 0); /* Ah */
    h = add(h, card(11, 0));          /* Kh */
    return h;
}
static StdDeck_CardMask flush_draw_board(void) {
    StdDeck_CardMask b = card(10, 0); /* Qh */
    b = add(b, card(5, 0));           /* 7h */
    b = add(b, card(0, 1));           /* 2c */
    return b;
}

/* 9h 8h on 7c 6c 2d : open-ended straight draw (needs a 5 or a T). */
static StdDeck_CardMask oesd_hand(void) {
    StdDeck_CardMask h = card(7, 0);  /* 9h */
    h = add(h, card(6, 0));           /* 8h */
    return h;
}
static StdDeck_CardMask oesd_board(void) {
    StdDeck_CardMask b = card(5, 1);  /* 7c */
    b = add(b, card(4, 1));           /* 6c */
    b = add(b, card(0, 2));           /* 2d */
    return b;
}

static void test_flush_draw_odds(void) {
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    pe_transition_result_t r;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_transition_distribution(
        game_holdem, flush_draw_hand(), flush_draw_board(), dead, &r));

    /* 9 outs of 47 on the turn. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 9.0 / 47.0, r.stats.prob_turn_flush);
    /* 1 - (38/47)*(37/46) by the river. */
    double river = 1.0 - (38.0 / 47.0) * (37.0 / 46.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, river, r.stats.prob_river_flush);

    /* As percentages per the acceptance criteria. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 19.1489, r.stats.prob_turn_flush * 100.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 34.9676, r.stats.prob_river_flush * 100.0);

    /* Starting board (4 hearts + 1 off-suit) is just a high card. */
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, r.start_category);
}

static void test_open_ended_straight_draw_odds(void) {
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    pe_transition_result_t r;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_transition_distribution(
        game_holdem, oesd_hand(), oesd_board(), dead, &r));

    /* 8 outs of 47 on the turn. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 8.0 / 47.0, r.stats.prob_turn_straight);
    /* 1 - (39/47)*(38/46) by the river. */
    double river = 1.0 - (39.0 / 47.0) * (38.0 / 46.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, river, r.stats.prob_river_straight);

    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 17.0213, r.stats.prob_turn_straight * 100.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 31.4524, r.stats.prob_river_straight * 100.0);
}

static void test_transition_matrix_is_probability_distribution(void) {
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    pe_transition_result_t r;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_transition_distribution(
        game_holdem, flush_draw_hand(), flush_draw_board(), dead, &r));

    for (int i = 0; i < PE_TRANSITION_MAX_CATEGORY; i++) {
        double row_sum = 0.0;
        int nonzero = 0;
        for (int j = 0; j < PE_TRANSITION_MAX_CATEGORY; j++) {
            TEST_ASSERT_TRUE(r.transition_matrix[i][j] >= 0.0);
            row_sum += r.transition_matrix[i][j];
            if (r.transition_matrix[i][j] > 0.0) {
                nonzero++;
            }
        }
        if (nonzero > 0) {
            TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, row_sum);
        }
    }
}

static void test_category_names(void) {
    TEST_ASSERT_EQUAL_STRING("High Card",      pe_transition_category_name(StdRules_HandType_NOPAIR));
    TEST_ASSERT_EQUAL_STRING("Pair",           pe_transition_category_name(StdRules_HandType_ONEPAIR));
    TEST_ASSERT_EQUAL_STRING("Straight Flush", pe_transition_category_name(StdRules_HandType_STFLUSH));
    TEST_ASSERT_NULL(pe_transition_category_name(99));
}

static void test_invalid_args(void) {
    StdDeck_CardMask hand, board, dead;
    pe_transition_result_t r;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    /* No hero hand. */
    TEST_ASSERT_EQUAL_INT(-1, pe_compute_transition_distribution(
        game_holdem, hand, board, dead, &r));
    /* NULL output. */
    TEST_ASSERT_EQUAL_INT(-1, pe_compute_transition_distribution(
        game_holdem, flush_draw_hand(), flush_draw_board(), dead, NULL));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_flush_draw_odds);
    RUN_TEST(test_open_ended_straight_draw_odds);
    RUN_TEST(test_transition_matrix_is_probability_distribution);
    RUN_TEST(test_category_names);
    RUN_TEST(test_invalid_args);
    return UNITY_END();
}
