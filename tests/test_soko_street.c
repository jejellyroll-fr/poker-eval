/*
 * test_soko_street.c - street-aware Sökö equity (#316 follow-up)
 *
 * pe_equity_soko_street() pins each player's upcards and known downcard and
 * lets the shared enumerator deal the rest: fifth street is one exact matchup,
 * fourth street one card each, and third street is sampled with every upcard
 * dead.  These tests cover that split and the descriptions it must reject.
 */

#include "unity.h"
#include <string.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity.h>
#include <poker_eval/games/soko_street.h>

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
    RUN_TEST(test_invalid_streets_rejected);
    return UNITY_END();
}
