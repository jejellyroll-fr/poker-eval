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
#include <poker_eval/equity.h>
#include <poker_eval/range.h>

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

/* Build a one-combo range around `hand`, which is what pe_equity takes. */
static void init_single_range(pe_range_t *range, pe_combo_t *combo,
                              StdDeck_CardMask hand, enum_game_t game) {
    combo->hand = hand;
    combo->weight = 1.0;
    memset(range, 0, sizeof(*range));
    range->combos = combo;
    range->count = 1;
    range->capacity = 1;
    range->game_type = game;
    range->total_weight = 1.0;
}

/* pe_equity must rank with the Sökö rules, not the standard five-card order.
 * A four-card flush beats one pair in Sökö; the standard evaluator puts the
 * pair ahead, so a silent fallback would flip the winner. */
static void test_pe_equity_uses_soko_ranking(void) {
    const char *flush_c[] = {"9h", "Th", "Jh", "3h", "2s"};
    const char *pair_c[] = {"Ah", "Ad", "Ks", "8c", "3c"};
    StdDeck_CardMask flush_hand = make_hand(flush_c, 5);
    StdDeck_CardMask pair_hand = make_hand(pair_c, 5);
    StdDeck_CardMask board, dead;
    pe_combo_t combos[2];
    pe_range_t ranges[2];
    const pe_range_t *range_ptrs[2];
    pe_equity_result_multi_t result;

    /* Dead means "not dealt", so it must not contain the players' own cards:
     * the engine adds each selected hand to the dead set itself. */
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    init_single_range(&ranges[0], &combos[0], flush_hand, game_soko);
    init_single_range(&ranges[1], &combos[1], pair_hand, game_soko);
    range_ptrs[0] = &ranges[0];
    range_ptrs[1] = &ranges[1];

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_soko, range_ptrs, 2, board,
                                              dead, NULL, &result));
    TEST_ASSERT_TRUE(result.results[0].equity > result.results[1].equity);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 1.0, result.results[0].equity);
    /* Complete Sökö hands have a single outcome, so this is exact: no silent
     * sampling of five board cards the game never deals. */
    TEST_ASSERT_EQUAL_INT(1, result.exact);
    TEST_ASSERT_EQUAL_INT(1, (int)result.samples);

    /* The control: the same hands under five-card draw give the pair the pot. */
    ranges[0].game_type = game_5draw_std;
    ranges[1].game_type = game_5draw_std;
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_5draw_std, range_ptrs, 2,
                                              board, dead, NULL, &result));
    TEST_ASSERT_TRUE(result.results[1].equity > result.results[0].equity);
    TEST_ASSERT_EQUAL_INT(1, result.exact);
}

/* Two pair stays above a four-card flush through the equity API too. */
static void test_pe_equity_two_pair_over_four_flush(void) {
    const char *two_pair_c[] = {"Ah", "Ad", "Ks", "Kd", "2h"};
    const char *flush_c[] = {"9h", "Th", "Jh", "3h", "2s"};
    StdDeck_CardMask two_pair = make_hand(two_pair_c, 5);
    StdDeck_CardMask flush_hand = make_hand(flush_c, 5);
    StdDeck_CardMask board, dead;
    pe_combo_t combos[2];
    pe_range_t ranges[2];
    const pe_range_t *range_ptrs[2];
    pe_equity_result_multi_t result;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    init_single_range(&ranges[0], &combos[0], two_pair, game_soko);
    init_single_range(&ranges[1], &combos[1], flush_hand, game_soko);
    range_ptrs[0] = &ranges[0];
    range_ptrs[1] = &ranges[1];

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_soko, range_ptrs, 2, board,
                                              dead, NULL, &result));
    TEST_ASSERT_TRUE(result.results[0].equity > result.results[1].equity);
    TEST_ASSERT_EQUAL_INT(1, result.exact);
}

/* Sökö ranges parse with the stud grammar, so pe_equity callers can build
 * them from strings instead of hand masks. */
static void test_pe_range_parse_soko(void) {
    StdDeck_CardMask dead;
    pe_range_t *range = NULL;

    StdDeck_CardMask_RESET(dead);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_range_parse(game_soko, "(AhAd)KsKd2h", dead, NULL,
                                         &range));
    TEST_ASSERT_NOT_NULL(range);
    TEST_ASSERT_EQUAL_UINT(1, range->count);
    TEST_ASSERT_EQUAL_INT(5, StdDeck_numCards(range->combos[0].hand));
    pe_range_free(range);

    /* Nonsense cannot be mistaken for a legal Sökö range. */
    range = NULL;
    TEST_ASSERT_EQUAL_INT(PE_STATUS_PARSE_ERROR,
                          pe_range_parse(game_soko, "not-a-hand", dead, NULL,
                                         &range));
    TEST_ASSERT_NULL(range);
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
    RUN_TEST(test_pe_equity_uses_soko_ranking);
    RUN_TEST(test_pe_equity_two_pair_over_four_flush);
    RUN_TEST(test_pe_range_parse_soko);
    return UNITY_END();
}
