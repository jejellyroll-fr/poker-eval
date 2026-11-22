/*
 * test_soko_street.c - street-aware Sökö equity (#316 follow-up)
 *
 * A street is described as a range of complete five-card hands: the visible
 * upcards and a known downcard are pinned, the missing cards are enumerated
 * from the deck minus every card another player is showing (and minus the
 * caller's dead cards).  These tests cover the range sizes, the dead-card
 * protection, and that fifth-street equity is exact.
 */

#include "unity.h"
#include <string.h>

#include <poker_eval/core/enumdefs.h>
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

/* Third street, hole card known: 49 free cards, two to come. */
static void test_third_street_range_size(void) {
    const char *up[] = {"Kh", "Qh"};
    const char *down[] = {"Ah"};
    pe_soko_street_player_t player;
    StdDeck_CardMask dead;
    pe_range_t *range = NULL;

    StdDeck_CardMask_RESET(dead);
    player.street = PE_SOKO_STREET_THIRD;
    player.up_cards = cards_of(up, 2);
    player.down_cards = cards_of(down, 1);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_soko_street_range(&player, dead, &range));
    TEST_ASSERT_NOT_NULL(range);
    TEST_ASSERT_EQUAL_UINT(1176, range->count); /* C(49,2) */
    TEST_ASSERT_EQUAL_INT(game_soko, range->game_type);
    pe_range_free(range);
}

/* Third street with an unknown hole card: 50 free cards, three to come. */
static void test_third_street_unknown_downcard(void) {
    const char *up[] = {"Kh", "Qh"};
    pe_soko_street_player_t player;
    StdDeck_CardMask dead;
    pe_range_t *range = NULL;

    StdDeck_CardMask_RESET(dead);
    player.street = PE_SOKO_STREET_THIRD;
    player.up_cards = cards_of(up, 2);
    StdDeck_CardMask_RESET(player.down_cards);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_soko_street_range(&player, dead, &range));
    TEST_ASSERT_EQUAL_UINT(19600, range->count); /* C(50,3) */
    pe_range_free(range);
}

/* Fifth street is a complete hand: one combo with a known hole card. */
static void test_fifth_street_is_complete(void) {
    const char *up[] = {"Kh", "Qh", "Jh", "2s"};
    const char *down[] = {"Ah"};
    pe_soko_street_player_t player;
    StdDeck_CardMask dead;
    pe_range_t *range = NULL;

    StdDeck_CardMask_RESET(dead);
    player.street = PE_SOKO_STREET_FIFTH;
    player.up_cards = cards_of(up, 4);
    player.down_cards = cards_of(down, 1);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_soko_street_range(&player, dead, &range));
    TEST_ASSERT_EQUAL_UINT(1, range->count);
    TEST_ASSERT_EQUAL_INT(5, StdDeck_numCards(range->combos[0].hand));
    TEST_ASSERT_TRUE(StdDeck_CardMask_ANY_SET(range->combos[0].hand,
                                              player.up_cards));
    TEST_ASSERT_TRUE(StdDeck_CardMask_ANY_SET(range->combos[0].hand,
                                              player.down_cards));
    pe_range_free(range);
}

/* A dead card must not appear in any completion. */
static void test_dead_cards_are_excluded(void) {
    const char *up[] = {"Kh", "Qh"};
    const char *down[] = {"Ah"};
    const char *dead_names[] = {"Kd"};
    pe_soko_street_player_t player;
    StdDeck_CardMask dead, dead_card;
    pe_range_t *range = NULL;
    size_t i;

    dead = cards_of(dead_names, 1);
    dead_card = dead;
    player.street = PE_SOKO_STREET_THIRD;
    player.up_cards = cards_of(up, 2);
    player.down_cards = cards_of(down, 1);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_soko_street_range(&player, dead, &range));
    TEST_ASSERT_EQUAL_UINT(1128, range->count); /* C(48,2) */
    for (i = 0; i < range->count; ++i)
        TEST_ASSERT_FALSE(StdDeck_CardMask_ANY_SET(range->combos[i].hand,
                                                   dead_card));
    pe_range_free(range);
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
    pe_range_t *range = NULL;
    pe_equity_result_multi_t result;

    StdDeck_CardMask_RESET(dead);

    /* Three upcards is not third street. */
    players[0].street = PE_SOKO_STREET_THIRD;
    players[0].up_cards = cards_of(up3, 3);
    players[0].down_cards = cards_of(down, 1);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG,
                          pe_soko_street_range(&players[0], dead, &range));
    TEST_ASSERT_NULL(range);

    /* Two players sharing an upcard. */
    players[0].street = PE_SOKO_STREET_THIRD;
    players[0].up_cards = cards_of(up, 2);
    players[0].down_cards = cards_of(down, 1);
    players[1].street = PE_SOKO_STREET_THIRD;
    players[1].up_cards = cards_of(up, 2);
    StdDeck_CardMask_RESET(players[1].down_cards);
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
    RUN_TEST(test_third_street_range_size);
    RUN_TEST(test_third_street_unknown_downcard);
    RUN_TEST(test_fifth_street_is_complete);
    RUN_TEST(test_dead_cards_are_excluded);
    RUN_TEST(test_fifth_street_equity_is_exact);
    RUN_TEST(test_third_street_equity_sampled);
    RUN_TEST(test_invalid_streets_rejected);
    return UNITY_END();
}
