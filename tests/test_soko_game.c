/*
 * test_soko_game.c - Sökö (Canadian Stud) as a public game
 *
 * ISSUE #316: Sökö is registered as game_soko and enumerated through the
 * shared dispatch.  These tests pin the behaviour at showdown:
 *   - the game parameters (five-card stud, no board, high only);
 *   - two pair beats a four-card flush, which beats a four-card straight,
 *     which beats one pair (the official win2day order);
 *   - a four-card flush beats one pair, unlike the standard five-card order;
 *   - equal four-card flushes/straights tie whatever the fifth card is;
 *   - incomplete pockets are completed by the shared enumerator.
 */

#include "unity.h"
#include <string.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/deck/deck_std.h>

void setUp(void) {}
void tearDown(void) {}

/* Build a mask from "Ah" style card names. */
static StdDeck_CardMask make_hand(const char **cards, int n) {
    StdDeck_CardMask mask;
    int i;

    StdDeck_CardMask_RESET(mask);
    for (i = 0; i < n; i++) {
        char buf[3];
        int card = -1;
        buf[0] = cards[i][0];
        buf[1] = cards[i][1];
        buf[2] = '\0';
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, StdDeck_stringToCard(buf, &card),
                                      "StdDeck_stringToCard failed");
        StdDeck_CardMask_SET(mask, card);
    }
    return mask;
}

static void build_dead(StdDeck_CardMask *dead, StdDeck_CardMask pockets[],
                       int npockets) {
    int i;

    StdDeck_CardMask_RESET(*dead);
    for (i = 0; i < npockets; i++)
        StdDeck_CardMask_OR(*dead, *dead, pockets[i]);
}

/* game_soko is a five-card stud game with no board and no low side. */
static void test_soko_game_params(void) {
    enum_gameparams_t *gp = enumGameParams(game_soko);

    TEST_ASSERT_NOT_NULL(gp);
    TEST_ASSERT_EQUAL_INT(game_soko, gp->game);
    TEST_ASSERT_EQUAL_INT(0, gp->minpocket);
    TEST_ASSERT_EQUAL_INT(5, gp->maxpocket);
    TEST_ASSERT_EQUAL_INT(0, gp->maxboard);
    TEST_ASSERT_EQUAL_INT(0, gp->haslopot);
    TEST_ASSERT_EQUAL_INT(1, gp->hashipot);
    TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_NONE, gp->low_qualifier);
    TEST_ASSERT_NOT_NULL(strstr(gp->name, "Sökö"));
    TEST_ASSERT_EQUAL_INT(game_soko, game_NUMGAMES - 1);
}

/* The P1 correctness fix: two pair stays above the four-card flush, as the
 * win2day order requires.  The old canadian_stud preset had the four-card
 * categories above two pair (and above trips). */
static void test_soko_two_pair_beats_four_flush(void) {
    const char *a_c[] = {"Ah", "Ad", "Ks", "Kd", "2h"}; /* two pair */
    const char *b_c[] = {"9h", "Th", "Jh", "3h", "2s"}; /* four-card flush */
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(board);
    pockets[0] = make_hand(a_c, 5);
    pockets[1] = make_hand(b_c, 5);
    build_dead(&dead, pockets, 2);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_soko, pockets, board, dead,
                                            2, 0, 0, &result));
    TEST_ASSERT_EQUAL_INT(1, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
    TEST_ASSERT_EQUAL_INT(0, result.ntiehi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.ntiehi[1]);
    enumResultFree(&result);
}

/* Sökö promotes the four-card flush above one pair, unlike the standard
 * five-card ranking, so the same showdown has a different winner. */
static void test_soko_four_flush_beats_one_pair(void) {
    const char *a_c[] = {"9h", "Th", "Jh", "3h", "2s"}; /* four-card flush */
    const char *b_c[] = {"Ah", "Ad", "Ks", "8c", "3h"}; /* one pair */
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(board);
    pockets[0] = make_hand(a_c, 5);
    pockets[1] = make_hand(b_c, 5);
    build_dead(&dead, pockets, 2);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_soko, pockets, board, dead,
                                            2, 0, 0, &result));
    TEST_ASSERT_EQUAL_INT(1, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
    enumResultFree(&result);

    /* The standard five-card evaluator ranks the pair higher. */
    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_5draw_std, pockets, board, dead,
                                            2, 0, 0, &result));
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(1, result.nwinhi[1]);
    enumResultFree(&result);
}

/* A four-card straight outranks one pair in Sökö, unlike the standard order. */
static void test_soko_four_straight_beats_one_pair(void) {
    const char *a_c[] = {"9h", "Ts", "Jd", "Qc", "2h"}; /* four-card straight */
    const char *b_c[] = {"Ah", "Ad", "Ks", "8c", "3h"}; /* one pair */
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(board);
    pockets[0] = make_hand(a_c, 5);
    pockets[1] = make_hand(b_c, 5);
    build_dead(&dead, pockets, 2);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive_dispatch(game_soko, pockets, board,
                                                     dead, 2, 0, 0, &result));
    TEST_ASSERT_EQUAL_INT(1, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
    enumResultFree(&result);
}

/* Equal four-card flush ranks tie whatever the fifth, off-suit card is. */
static void test_soko_four_flush_kicker_ties(void) {
    const char *a_c[] = {"Ah", "Jh", "8h", "5h", "2s"};
    const char *b_c[] = {"Ad", "Jd", "8d", "5d", "3c"};
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(board);
    pockets[0] = make_hand(a_c, 5);
    pockets[1] = make_hand(b_c, 5);
    build_dead(&dead, pockets, 2);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_soko, pockets, board, dead,
                                            2, 0, 0, &result));
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
    TEST_ASSERT_EQUAL_INT(1, result.ntiehi[0]);
    TEST_ASSERT_EQUAL_INT(1, result.ntiehi[1]);
    enumResultFree(&result);
}

/* Equal four-card straight high cards tie whatever the fifth card is. */
static void test_soko_four_straight_kicker_ties(void) {
    const char *a_c[] = {"9h", "Ts", "Jd", "Qc", "2h"};
    const char *b_c[] = {"9s", "Td", "Jh", "Qd", "3c"};
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(board);
    pockets[0] = make_hand(a_c, 5);
    pockets[1] = make_hand(b_c, 5);
    build_dead(&dead, pockets, 2);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_soko, pockets, board, dead,
                                            2, 0, 0, &result));
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
    TEST_ASSERT_EQUAL_INT(1, result.ntiehi[0]);
    TEST_ASSERT_EQUAL_INT(1, result.ntiehi[1]);
    enumResultFree(&result);
}

/* Equity over an incomplete pocket deals the missing cards with the same
 * evaluator: a made four-card straight keeps beating one pair on every card. */
static void test_soko_sampled_incomplete_pocket(void) {
    const char *a_c[] = {"9h", "Ts", "Jd", "Qc"}; /* four-straight, one to come */
    const char *b_c[] = {"Ah", "Ad", "Ks", "8c", "3h"};
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(board);
    pockets[0] = make_hand(a_c, 4);
    pockets[1] = make_hand(b_c, 5);
    build_dead(&dead, pockets, 2);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_soko, pockets, board, dead,
                                            2, 0, 0, &result));
    /* 52 cards minus the nine already held leave 43 rivers, all won. */
    TEST_ASSERT_EQUAL_INT(43, (int)result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
    enumResultFree(&result);

    /* The sampler uses the same evaluator. */
    TEST_ASSERT_EQUAL_INT(0, enumSample(game_soko, pockets, board, dead,
                                        2, 0, 200, 0, &result));
    TEST_ASSERT_EQUAL_INT(200, (int)result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
    enumResultFree(&result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_soko_game_params);
    RUN_TEST(test_soko_two_pair_beats_four_flush);
    RUN_TEST(test_soko_four_flush_beats_one_pair);
    RUN_TEST(test_soko_four_straight_beats_one_pair);
    RUN_TEST(test_soko_four_flush_kicker_ties);
    RUN_TEST(test_soko_four_straight_kicker_ties);
    RUN_TEST(test_soko_sampled_incomplete_pocket);
    return UNITY_END();
}
