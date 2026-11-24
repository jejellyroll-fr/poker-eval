/*
 * test_soko_street.c - street-aware Sökö equity (#316 follow-up)
 *
 * pe_equity_soko_street() pins each player's upcards and known downcard and
 * lets the shared enumerator deal the rest: fifth street is one exact matchup,
 * fourth street one card each, and third street is sampled with every upcard
 * dead.  These tests cover that split and the descriptions it must reject.
 */

#include "unity.h"
#include <math.h>
#include <string.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity.h>
#include <poker_eval/games/soko_street.h>
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

/* Fifth street, both hands complete: exact showdown, no sampling. */
static void test_fifth_street_equity_is_exact(void) {
    const char *up0[] = {"Kh", "Qh", "Jh", "2s"};
    const char *down0[] = {"Ah"}; /* four-card flush A-K-Q-J of hearts */
    const char *up1[] = {"9d", "8d", "7d", "6d"};
    const char *down1[] = {"Kc"}; /* four-card flush 9-8-7-6 of diamonds */
    pe_soko_street_player_t players[2];
    StdDeck_CardMask dead;
    pe_equity_result_multi_t result;

    StdDeck_CardMask_RESET(dead);
    players[0].street = PE_SOKO_STREET_FIFTH;
    players[0].up_cards = cards_of(up0, 4);
    players[0].down_cards = cards_of(down0, 1);
    players[1].street = PE_SOKO_STREET_FIFTH;
    players[1].up_cards = cards_of(up1, 4);
    players[1].down_cards = cards_of(down1, 1);

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_soko_street(players, 2, dead, NULL, &result));
    TEST_ASSERT_EQUAL_INT(1, result.exact);
    TEST_ASSERT_EQUAL_INT(1, (int)result.samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, result.results[0].equity);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, result.results[1].equity);
}

/* Fourth street: one card to come for each player, still exact. */
static void test_fourth_street_equity_is_exact(void) {
    const char *up0[] = {"Kh", "Qh", "Jh"};
    const char *down0[] = {"Ah"}; /* four-card flush already made */
    const char *up1[] = {"9d", "8d", "7d"};
    const char *down1[] = {"2c"};
    pe_soko_street_player_t players[2];
    StdDeck_CardMask dead;
    pe_equity_result_multi_t result;

    StdDeck_CardMask_RESET(dead);
    players[0].street = PE_SOKO_STREET_FOURTH;
    players[0].up_cards = cards_of(up0, 3);
    players[0].down_cards = cards_of(down0, 1);
    players[1].street = PE_SOKO_STREET_FOURTH;
    players[1].up_cards = cards_of(up1, 3);
    players[1].down_cards = cards_of(down1, 1);

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_soko_street(players, 2, dead, NULL, &result));
    /* The one missing card of each pocket is dealt by the enumerator, so the
     * result stays exhaustive rather than silently treating a four-card hand
     * as complete. */
    TEST_ASSERT_EQUAL_INT(1, result.exact);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1.0,
                              result.results[0].equity + result.results[1].equity);
    TEST_ASSERT_TRUE(result.results[0].equity > 0.5);
}

/* Third street: sampled, and both equities add up to the pot. */
static void test_third_street_equity_sampled(void) {
    const char *up0[] = {"Kh", "Qh"};
    const char *down0[] = {"Ah"};
    const char *up1[] = {"9d", "8d"};
    pe_soko_street_player_t players[2];
    StdDeck_CardMask dead;
    pe_equity_opts_t opts;
    pe_equity_result_multi_t result;

    StdDeck_CardMask_RESET(dead);
    players[0].street = PE_SOKO_STREET_THIRD;
    players[0].up_cards = cards_of(up0, 2);
    players[0].down_cards = cards_of(down0, 1);
    players[1].street = PE_SOKO_STREET_THIRD;
    players[1].up_cards = cards_of(up1, 2);
    StdDeck_CardMask_RESET(players[1].down_cards);

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = 300;

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_soko_street(players, 2, dead, &opts,
                                                &result));
    /* One range pair, sampled: "samples" in the legacy result is the number
     * of evaluated matchups, not the iteration budget. */
    TEST_ASSERT_EQUAL_INT(0, result.exact);
    TEST_ASSERT_EQUAL_INT(1, (int)result.samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1.0,
                              result.results[0].equity + result.results[1].equity);
    /* The known four-flush draw must be a favourite over an unknown hand, and
     * the opponent's upcards (9d, 8d) are dead for our completion, so the
     * flush cannot use them. */
    TEST_ASSERT_TRUE(result.results[0].equity > 0.5);
}

/* Fourth street: the exact answer and 20k samples must agree, and the exact
 * answer must fall inside the reported confidence interval.  Two pair against
 * a four-card flush is genuinely uncertain, so the variance is not zero. */
static void test_monte_carlo_converges_to_exact(void) {
    const char *up0[] = {"Ah", "Ad", "Ks"};
    const char *down0[] = {"Kd"}; /* two pair, aces and kings */
    const char *up1[] = {"9h", "Th", "Jh"};
    const char *down1[] = {"8h"}; /* four-card flush, jack high */
    pe_soko_street_player_t players[2];
    StdDeck_CardMask dead;
    pe_equity_opts_t opts;
    pe_equity_result_multi_t exact, sampled, coarse;
    int i;

    StdDeck_CardMask_RESET(dead);
    players[0].street = PE_SOKO_STREET_FOURTH;
    players[0].up_cards = cards_of(up0, 3);
    players[0].down_cards = cards_of(down0, 1);
    players[1].street = PE_SOKO_STREET_FOURTH;
    players[1].up_cards = cards_of(up1, 3);
    players[1].down_cards = cards_of(down1, 1);

    memset(&exact, 0, sizeof(exact));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_soko_street(players, 2, dead, NULL, &exact));
    TEST_ASSERT_EQUAL_INT(1, exact.exact);
    TEST_ASSERT_EQUAL_INT(0, (int)exact.iterations);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, exact.standard_error[0]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, exact.standard_error[1]);

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = 20000;
    pe_rng_seed_current(5);

    memset(&sampled, 0, sizeof(sampled));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_soko_street(players, 2, dead, &opts,
                                                &sampled));
    TEST_ASSERT_EQUAL_INT(0, sampled.exact);
    TEST_ASSERT_EQUAL_INT(20000, (int)sampled.iterations);
    for (i = 0; i < 2; ++i) {
        double ci = 1.96 * sampled.standard_error[i];
        double mean = sampled.results[i].equity;
        /* Heads-up the payoff is 1, 0.5 or 0, so the standard error has an
         * independent formula in the win/tie probabilities. */
        double variance = sampled.results[i].win_prob +
                          0.25 * sampled.results[i].tie_prob - mean * mean;

        if (variance < 0.0) variance = 0.0;
        TEST_ASSERT_TRUE(sampled.standard_error[i] > 0.0);
        TEST_ASSERT_TRUE(ci > 0.0);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9,
                                  sqrt(variance / (double)sampled.iterations),
                                  sampled.standard_error[i]);
        /* Four standard errors: robust to one unlucky seed, but a biased
         * sampler or a wrong interval still fails. */
        TEST_ASSERT_TRUE(fabs(mean - exact.results[i].equity) <= 4.0 * ci);
        TEST_ASSERT_TRUE(fabs(mean - exact.results[i].equity) < 0.02);
    }

    /* A quarter of the budget must report a wider interval. */
    opts.iterations = 5000;
    memset(&coarse, 0, sizeof(coarse));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_soko_street(players, 2, dead, &opts,
                                                &coarse));
    TEST_ASSERT_EQUAL_INT(5000, (int)coarse.iterations);
    TEST_ASSERT_TRUE(coarse.standard_error[0] > sampled.standard_error[0]);
}

/* Six players at fifth street: every card is known, so the whole table is one
 * exact matchup and the strongest hand takes the pot. */
static void test_six_way_fifth_street_is_exact(void) {
    const char *up[6][4] = {
        {"Kh", "Qh", "Jh", "2s"}, {"9d", "8d", "7d", "6d"},
        {"As", "Ad", "Ks", "Kd"}, {"Qs", "Qd", "Js", "Jd"},
        {"5s", "5d", "4s", "4d"}, {"7s", "7h", "6s", "6h"},
    };
    const char *down[6] = {"Ah", "Kc", "2h", "3h", "3s", "5h"};
    pe_soko_street_player_t players[6];
    StdDeck_CardMask dead;
    pe_equity_result_multi_t result;
    double total = 0.0;
    int i;

    StdDeck_CardMask_RESET(dead);
    for (i = 0; i < 6; ++i) {
        players[i].street = PE_SOKO_STREET_FIFTH;
        players[i].up_cards = cards_of(up[i], 4);
        players[i].down_cards = cards_of(&down[i], 1);
    }

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_soko_street(players, 6, dead, NULL,
                                                &result));
    TEST_ASSERT_EQUAL_INT(6, result.num_players);
    TEST_ASSERT_EQUAL_INT(1, result.exact);
    TEST_ASSERT_EQUAL_INT(1, (int)result.samples);
    for (i = 0; i < 6; ++i)
        total += result.results[i].equity;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, total);
    /* Aces and kings, two pair, is the best hand at the table. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, result.results[2].equity);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, result.results[0].equity);
}

/* A range mixes matchups, so its interval cannot come from the pooled
 * histogram: it comes from the matchups themselves, each sampled matchup being
 * a batch whose mean is known to s^2/n.  A sampled range must report a
 * positive error, and an exhaustively evaluated one must still report none. */
static void test_standard_error_for_a_range(void) {
    pe_range_t *ranges[2] = {NULL, NULL};
    const pe_range_t *range_ptrs[2];
    StdDeck_CardMask board, dead;
    pe_equity_opts_t opts;
    pe_equity_result_multi_t result, exact;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    /* Two short pockets per range: every matchup still draws a card, so the
     * samples are numerous and genuinely random. */
    const char *h00[] = {"9h", "Th", "Jh", "3h"};
    const char *h01[] = {"9h", "Th", "Jh", "4h"};
    const char *h10[] = {"Ah", "Ad", "Ks", "Kd"};
    const char *h11[] = {"Ah", "Ad", "Ks", "Qd"};

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_range_create(game_soko, &ranges[0]));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_range_create(game_soko, &ranges[1]));
    ranges[0]->combos[0].hand = cards_of(h00, 4);
    ranges[0]->combos[0].weight = 1.0;
    ranges[0]->combos[1].hand = cards_of(h01, 4);
    ranges[0]->combos[1].weight = 1.0;
    ranges[0]->count = 2;
    ranges[0]->total_weight = 2.0;
    ranges[1]->combos[0].hand = cards_of(h10, 4);
    ranges[1]->combos[0].weight = 1.0;
    ranges[1]->combos[1].hand = cards_of(h11, 4);
    ranges[1]->combos[1].weight = 1.0;
    ranges[1]->count = 2;
    ranges[1]->total_weight = 2.0;
    range_ptrs[0] = ranges[0];
    range_ptrs[1] = ranges[1];

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = 2000;

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_soko, range_ptrs, 2, board,
                                              dead, &opts, &result));
    TEST_ASSERT_EQUAL_INT(0, result.exact);
    TEST_ASSERT_TRUE(result.samples > 1);
    /* The budget is split over the four matchups. */
    TEST_ASSERT_EQUAL_INT(500, (int)result.iterations);
    TEST_ASSERT_TRUE(result.standard_error[0] > 0.0);
    TEST_ASSERT_TRUE(result.standard_error[1] > 0.0);

    /* Enumerated instead of sampled, the same spot has no sampling error. */
    memset(&exact, 0, sizeof(exact));
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_soko, range_ptrs, 2, board,
                                              dead, NULL, &exact));
    TEST_ASSERT_EQUAL_INT(1, exact.exact);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, exact.standard_error[0]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, exact.standard_error[1]);

    pe_range_free(ranges[0]);
    pe_range_free(ranges[1]);
}

/* Three players at fourth street, all holding A-K-Q-T in mixed suits, with
 * only the four nines and the eight of clubs left to draw from: the drawn card
 * is a kicker and nothing else, so ranks alone decide the pot.  Dealing three
 * nines out of those five cards is a three-way tie in 40% of the deals;
 * otherwise the two nines split the pot and the eight loses it.  That reaches
 * the h = 3 term of the variance, which a heads-up spot never can: one sample
 * is worth 1/3 (p = 0.4), 1/2 (p = 0.4) or 0 (p = 0.2), a standard deviation
 * of sqrt(1/30).
 *
 * A-K-Q-J would already be a four-card straight under the Sökö preset and tie
 * every sample, so the fourth card is a ten.
 *
 * The reported standard error is compared with the spread of the equities
 * over runs with different seeds: it has to describe the sampling, not merely
 * its own share histogram. */
static void test_three_way_standard_error_matches_spread(void) {
    const char *up[3][3] = {
        {"Ah", "Kd", "Qc"}, {"Ad", "Kc", "Qs"}, {"As", "Kh", "Qd"},
    };
    const char *down[3] = {"Ts", "Th", "Tc"};
    const char *left_to_draw[] = {"9c", "9d", "9h", "9s", "8c"};
    enum { RUNS = 256, ITERATIONS = 4000 };
    pe_soko_street_player_t players[3];
    StdDeck_CardMask dealt, dead;
    pe_equity_opts_t opts;
    double sum[3] = {0.0, 0.0, 0.0};
    double sum_squares[3] = {0.0, 0.0, 0.0};
    double sum_error[3] = {0.0, 0.0, 0.0};
    int run, i;

    /* Everything held or waiting to be drawn stays out of the dead mask. */
    StdDeck_CardMask_RESET(dealt);
    for (i = 0; i < 3; ++i) {
        players[i].street = PE_SOKO_STREET_FOURTH;
        players[i].up_cards = cards_of(up[i], 3);
        players[i].down_cards = cards_of(&down[i], 1);
        StdDeck_CardMask_OR(dealt, dealt, players[i].up_cards);
        StdDeck_CardMask_OR(dealt, dealt, players[i].down_cards);
    }
    StdDeck_CardMask_OR(dealt, dealt, cards_of(left_to_draw, 5));
    StdDeck_CardMask_NOT(dead, dealt);

    memset(&opts, 0, sizeof(opts));
    opts.is_monte_carlo = 1;
    opts.iterations = ITERATIONS;

    for (run = 0; run < RUNS; ++run) {
        pe_equity_result_multi_t result;

        pe_rng_seed_current(1000u + (uint64_t)run);
        memset(&result, 0, sizeof(result));
        TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                              pe_equity_soko_street(players, 3, dead, &opts,
                                                    &result));
        /* iterations is only filled for a sampled result. */
        TEST_ASSERT_EQUAL_INT(ITERATIONS, (int)result.iterations);
        for (i = 0; i < 3; ++i) {
            double equity = result.results[i].equity;

            TEST_ASSERT_TRUE(result.standard_error[i] > 0.0);
            sum[i] += equity;
            sum_squares[i] += equity * equity;
            sum_error[i] += result.standard_error[i];
        }
    }

    for (i = 0; i < 3; ++i) {
        double mean = sum[i] / (double)RUNS;
        double reported = sum_error[i] / (double)RUNS;
        double variance = (sum_squares[i] - (double)RUNS * mean * mean) /
                          (double)(RUNS - 1);

        /* The spot is symmetric, so every player is worth a third of the pot.
         * The estimate itself is only off by about 2e-4, so this checks the
         * spot rather than the sampler. */
        TEST_ASSERT_DOUBLE_WITHIN(0.005, 1.0 / 3.0, mean);
        TEST_ASSERT_TRUE(variance > 0.0);
        /* sqrt(variance) is the one-sample standard deviation measured over
         * RUNS independent runs, and such an estimate has a relative spread of
         * 1/sqrt(2 * (RUNS - 1)) - 4% here - so a 20% window is more than four
         * of those.  Counting a three-way share as 1/2 would report 2.7 times
         * too much; dropping the h = 3 term altogether would report zero and
         * fail the check above. */
        TEST_ASSERT_TRUE(fabs(sqrt(variance) - reported) <= 0.20 * reported);
    }
}

/* Invalid descriptions are rejected rather than silently mis-enumerated. */
static void test_invalid_streets_rejected(void) {
    const char *up3[] = {"Kh", "Qh", "Jh"};
    const char *up[] = {"Kh", "Qh"};
    const char *down[] = {"Ah"};
    pe_soko_street_player_t players[2];
    StdDeck_CardMask dead;
    pe_equity_result_multi_t result;

    StdDeck_CardMask_RESET(dead);

    /* Three upcards is not third street. */
    players[0].street = PE_SOKO_STREET_THIRD;
    players[0].up_cards = cards_of(up3, 3);
    players[0].down_cards = cards_of(down, 1);
    players[1].street = PE_SOKO_STREET_THIRD;
    players[1].up_cards = cards_of(up, 2);
    StdDeck_CardMask_RESET(players[1].down_cards);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG,
                          pe_equity_soko_street(players, 2, dead, NULL,
                                                &result));

    /* Two players sharing an upcard. */
    players[0].up_cards = cards_of(up, 2);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG,
                          pe_equity_soko_street(players, 2, dead, NULL,
                                                &result));

    /* A dead card that a player holds. */
    players[1].up_cards = cards_of((const char *[]){"Kd", "Qd"}, 2);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG,
                          pe_equity_soko_street(players, 2,
                                                cards_of((const char *[]){"Kd"},
                                                         1),
                                                NULL, &result));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fifth_street_equity_is_exact);
    RUN_TEST(test_fourth_street_equity_is_exact);
    RUN_TEST(test_third_street_equity_sampled);
    RUN_TEST(test_six_way_fifth_street_is_exact);
    RUN_TEST(test_monte_carlo_converges_to_exact);
    RUN_TEST(test_standard_error_for_a_range);
    RUN_TEST(test_three_way_standard_error_matches_spread);
    RUN_TEST(test_invalid_streets_rejected);
    return UNITY_END();
}
