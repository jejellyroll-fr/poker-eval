/*
 * test_range_equity_interval.c - batch-means interval for a sampled range
 *
 * pe_equity_calculate() reports a standard error for a range by treating each
 * sampled matchup as a batch: the estimator is the weighted mean of the batch
 * means, so Var = sum_m (w_m/W)^2 s_m^2/n_m.  These tests pin that estimate on
 * a spot with six matchups of unequal weight, against three independent
 * references: the exact enumeration of the same ranges, the spread of the
 * estimator over runs with different seeds, and the per-matchup variances
 * measured one matchup at a time.  A hi/lo range must still report none.
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 */

#include "unity.h"
#include <math.h>
#include <string.h>

#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity.h>
#include <poker_eval/range.h>

void setUp(void) {}
void tearDown(void) {}

static StdDeck_CardMask cards_of(const char *const *names, int n) {
    StdDeck_CardMask mask;
    int i;

    StdDeck_CardMask_RESET(mask);
    for (i = 0; i < n; ++i) {
        char buf[3];
        int card = -1;

        buf[0] = names[i][0];
        buf[1] = names[i][1];
        buf[2] = '\0';
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, StdDeck_stringToCard(buf, &card),
                                      names[i]);
        StdDeck_CardMask_SET(mask, card);
    }
    return mask;
}

/* A range of fixed pockets with explicit weights, one pocket per
 * `cards_per_hand` names.  pe_range_add_combo() is private, so the combo array
 * pe_range_create() allocates is filled directly. */
static pe_range_t *make_range(enum_game_t game,
                              const char *const *names,
                              int cards_per_hand,
                              const double *weights,
                              int count) {
    pe_range_t *range = NULL;
    double total = 0.0;
    int i;

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, pe_range_create(game, &range));
    for (i = 0; i < count; ++i) {
        range->combos[i].hand =
            cards_of(&names[i * cards_per_hand], cards_per_hand);
        range->combos[i].weight = weights[i];
        total += weights[i];
    }
    range->count = (size_t)count;
    range->total_weight = total;
    return range;
}

/*
 * Turn, one card to come, six matchups.  The hero's three pairs fare very
 * differently against the villain's kings, so the matchups carry genuinely
 * different means and variances, and the villain's second pocket is nine times
 * as likely as the first: a variance that ignored the weighting would be 1.6
 * times too small.
 */
static const char *const board_names[] = {"Ah", "Kd", "7c", "2s"};
static const char *const hero_cards[] = {
    "Qs", "Qd", "7h", "7d", "2h", "2c",
};
static const double hero_weights[] = {1.0, 1.0, 1.0};
static const char *const villain_cards[] = {
    "Ks", "Kh", "9h", "9d",
};
static const double villain_weights[] = {1.0, 9.0};

typedef struct {
    pe_range_t *hero;
    pe_range_t *villain;
    const pe_range_t *ranges[2];
    StdDeck_CardMask board;
    StdDeck_CardMask dead;
} spot_t;

static void spot_init(spot_t *spot) {
    StdDeck_CardMask_RESET(spot->dead);
    spot->board = cards_of(board_names, 4);
    spot->hero = make_range(game_holdem, hero_cards, 2, hero_weights, 3);
    spot->villain = make_range(game_holdem, villain_cards, 2, villain_weights, 2);
    spot->ranges[0] = spot->hero;
    spot->ranges[1] = spot->villain;
}

static void spot_free(spot_t *spot) {
    pe_range_free(spot->hero);
    pe_range_free(spot->villain);
}

/* The reference: the same ranges, every remaining card enumerated. */
static void test_interval_reference_is_exact(void) {
    spot_t spot;
    pe_equity_result_multi_t exact;
    int i;
    double total = 0.0;

    spot_init(&spot);
    memset(&exact, 0, sizeof(exact));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_holdem, spot.ranges, 2,
                                              spot.board, spot.dead, NULL,
                                              &exact));
    TEST_ASSERT_EQUAL_INT(1, exact.exact);
    TEST_ASSERT_EQUAL_INT(6, (int)exact.samples);
    for (i = 0; i < 2; ++i) {
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, exact.standard_error[i]);
        total += exact.results[i].equity;
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, total);
    spot_free(&spot);
}

/*
 * Coverage: over runs with different seeds, the exact equity has to fall
 * inside the reported 95% interval about as often as the interval claims.  A
 * variance divided by the wrong power of the weight, or that forgot to divide
 * by the sample count, misses most of the time.
 */
static void test_interval_covers_the_exact_equity(void) {
    enum { RUNS = 40, ITERATIONS = 6000 };
    spot_t spot;
    pe_equity_opts_t opts;
    pe_equity_result_multi_t exact;
    double sum[2] = {0.0, 0.0};
    double sum_squares[2] = {0.0, 0.0};
    double sum_error[2] = {0.0, 0.0};
    int inside[2] = {0, 0};
    int run, i;

    spot_init(&spot);
    memset(&exact, 0, sizeof(exact));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_holdem, spot.ranges, 2,
                                              spot.board, spot.dead, NULL,
                                              &exact));
    TEST_ASSERT_EQUAL_INT(1, exact.exact);

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = ITERATIONS;

    for (run = 0; run < RUNS; ++run) {
        pe_equity_result_multi_t sampled;

        pe_rng_seed_current(100u + (unsigned int)run);
        memset(&sampled, 0, sizeof(sampled));
        TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                              pe_equity_calculate(game_holdem, spot.ranges, 2,
                                                  spot.board, spot.dead, &opts,
                                                  &sampled));
        TEST_ASSERT_EQUAL_INT(0, sampled.exact);
        TEST_ASSERT_EQUAL_INT(6, (int)sampled.samples);
        for (i = 0; i < 2; ++i) {
            double equity = sampled.results[i].equity;
            double error = sampled.standard_error[i];

            /* A sampled range must now report one. */
            TEST_ASSERT_TRUE(error > 0.0);
            sum[i] += equity;
            sum_squares[i] += equity * equity;
            sum_error[i] += error;
            if (fabs(equity - exact.results[i].equity) <= 1.96 * error)
                inside[i]++;
        }
    }

    for (i = 0; i < 2; ++i) {
        double mean = sum[i] / (double)RUNS;
        double reported = sum_error[i] / (double)RUNS;
        double variance = (sum_squares[i] - (double)RUNS * mean * mean) /
                          (double)(RUNS - 1);
        double spread = variance > 0.0 ? sqrt(variance) : 0.0;

        /* 95% of 40 runs is 38: allow four misses before calling the interval
         * broken.  A variance that was too small would miss far more often. */
        TEST_ASSERT_TRUE_MESSAGE(inside[i] >= 34,
                                 "the 95% interval missed the exact equity too "
                                 "often for the reported standard error");
        /* And the reported value must describe the estimator's actual spread,
         * not merely its own histogram.  A sample standard deviation from 40
         * runs is itself known to about 11%, so 40% is over three of those. */
        TEST_ASSERT_DOUBLE_WITHIN(0.40 * reported, reported, spread);
    }

    spot_free(&spot);
}

/*
 * The weighting: measure each matchup's per-sample variance on its own, then
 * combine the six with (w_m/W)^2 s_m^2/n_m and compare with the range's own
 * report.  This is what a variance that treated the matchups as equally likely
 * gets wrong, by 28% here.
 */
static void test_interval_weights_the_matchups(void) {
    enum { PER_MATCHUP = 1000 };
    spot_t spot;
    pe_equity_opts_t opts;
    pe_equity_result_multi_t whole;
    double predicted[2] = {0.0, 0.0};
    double total_weight = 0.0;
    int i, j, p;

    spot_init(&spot);
    for (i = 0; i < 3; ++i)
        total_weight += hero_weights[i];
    total_weight *= villain_weights[0] + villain_weights[1];

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = 6 * PER_MATCHUP;
    pe_rng_seed_current(7);
    memset(&whole, 0, sizeof(whole));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_holdem, spot.ranges, 2,
                                              spot.board, spot.dead, &opts,
                                              &whole));
    TEST_ASSERT_EQUAL_INT(6, (int)whole.samples);

    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 2; ++j) {
            const double unit[] = {1.0};
            pe_range_t *singles[2];
            const pe_range_t *pair[2];
            pe_equity_opts_t one_opts;
            pe_equity_result_multi_t one;
            double weight = hero_weights[i] * villain_weights[j];
            double share = weight / total_weight;

            singles[0] = make_range(game_holdem, &hero_cards[2 * i], 2, unit, 1);
            singles[1] =
                make_range(game_holdem, &villain_cards[2 * j], 2, unit, 1);
            pair[0] = singles[0];
            pair[1] = singles[1];
            memset(&one_opts, 0, sizeof(one_opts));
            one_opts.is_monte_carlo = 1;
            one_opts.iterations = PER_MATCHUP;
            memset(&one, 0, sizeof(one));
            TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                                  pe_equity_calculate(game_holdem, pair, 2,
                                                      spot.board, spot.dead,
                                                      &one_opts, &one));
            for (p = 0; p < 2; ++p) {
                /* The single matchup's standard error is s/sqrt(n). */
                double per_sample_variance =
                    one.standard_error[p] * one.standard_error[p] *
                    (double)PER_MATCHUP;
                predicted[p] +=
                    share * share * per_sample_variance / (double)PER_MATCHUP;
            }
            pe_range_free(singles[0]);
            pe_range_free(singles[1]);
        }
    }

    /* The measured prediction lands within a percent of the report; 10% is
     * several standard errors of the per-matchup variance estimates (n = 1000
     * each) and still rejects a variance that ignored the weights, which is
     * 28% off here. */
    for (p = 0; p < 2; ++p)
        TEST_ASSERT_DOUBLE_WITHIN(0.10 * whole.standard_error[p],
                                  whole.standard_error[p],
                                  sqrt(predicted[p]));

    spot_free(&spot);
}

/*
 * A low pot is a payoff correlated with the high one, and the share
 * histograms kept here cannot separate them, so a hi/lo range still reports no
 * interval rather than a plausible-looking number.
 */
static void test_interval_is_undefined_for_a_low_pot(void) {
    const char *const hero[] = {"Qs", "Qd", "Jh", "Th"};
    const char *const villain[] = {"9h", "9d", "8h", "8d"};
    const double unit[] = {1.0};
    pe_range_t *ranges[2] = {NULL, NULL};
    const pe_range_t *range_ptrs[2];
    StdDeck_CardMask board, dead;
    pe_equity_opts_t opts;
    pe_equity_result_multi_t result;

    StdDeck_CardMask_RESET(dead);
    board = cards_of(board_names, 4);
    ranges[0] = make_range(game_omaha8, hero, 4, unit, 1);
    ranges[1] = make_range(game_omaha8, villain, 4, unit, 1);
    range_ptrs[0] = ranges[0];
    range_ptrs[1] = ranges[1];

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = 2000;

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_omaha8, range_ptrs, 2,
                                              board, dead, &opts, &result));
    TEST_ASSERT_EQUAL_INT(0, result.exact);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, result.standard_error[0]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, result.standard_error[1]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1.0,
                              result.results[0].equity +
                                  result.results[1].equity);

    pe_range_free(ranges[0]);
    pe_range_free(ranges[1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_interval_reference_is_exact);
    RUN_TEST(test_interval_covers_the_exact_equity);
    RUN_TEST(test_interval_weights_the_matchups);
    RUN_TEST(test_interval_is_undefined_for_a_low_pot);
    return UNITY_END();
}
