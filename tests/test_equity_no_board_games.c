/*
 * test_equity_no_board_games.c - boardless games stay exact (#316 follow-up)
 *
 * pe_equity used to ask for five board cards for every game except seven-card
 * stud, so complete hands in the draw and five-card stud games looked
 * incomplete and were Monte-Carlo sampled over phantom cards.  The number of
 * cards to deal now comes from the game's own maxboard.
 *
 * This test walks every registered game with no board and a pocket of at most
 * eight cards, hands it two complete disjoint pockets, and requires a single
 * exact matchup.  It fails for any game whose complete hands start looking
 * incomplete again.
 */

#include "unity.h"
#include <string.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity.h>
#include <poker_eval/range.h>

void setUp(void) {}
void tearDown(void) {}

/* Two disjoint pockets of up to eight cards. */
static const char *const hand_cards[] = {
    "Ah", "Kh", "Qh", "Jh", "Th", "9h", "8h", "7h",
    "As", "Ks", "Qs", "Js", "Ts", "9s", "8s", "7s"};

static StdDeck_CardMask make_hand(int offset, int n) {
    StdDeck_CardMask hand;
    int i;

    StdDeck_CardMask_RESET(hand);
    for (i = 0; i < n; ++i) {
        char buf[3];
        int card = -1;

        buf[0] = hand_cards[offset + i][0];
        buf[1] = hand_cards[offset + i][1];
        buf[2] = '\0';
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, StdDeck_stringToCard(buf, &card),
                                      hand_cards[offset + i]);
        StdDeck_CardMask_SET(hand, card);
    }
    return hand;
}

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

static void test_boardless_games_are_exact(void) {
    StdDeck_CardMask board, dead;
    int game;
    int checked = 0;
    int saw_soko = 0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    for (game = 0; game < game_NUMGAMES; ++game) {
        enum_gameparams_t *params = enumGameParams((enum_game_t)game);
        pe_combo_t combos[2];
        pe_range_t ranges[2];
        const pe_range_t *range_ptrs[2];
        pe_equity_result_multi_t result;
        int pocket;

        if (params == NULL || params->maxboard != 0)
            continue;
        pocket = params->maxpocket;
        /* A complete pocket must fit in the two disjoint hands above, and
         * 13-card games (OFC, Chinese poker) distribute cards differently. */
        if (pocket < 2 || pocket > 8)
            continue;

        init_single_range(&ranges[0], &combos[0], make_hand(0, pocket),
                          (enum_game_t)game);
        init_single_range(&ranges[1], &combos[1], make_hand(8, pocket),
                          (enum_game_t)game);
        range_ptrs[0] = &ranges[0];
        range_ptrs[1] = &ranges[1];

        memset(&result, 0, sizeof(result));
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            PE_STATUS_OK,
            pe_equity_calculate((enum_game_t)game, range_ptrs, 2, board, dead,
                                NULL, &result),
            params->name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.exact, params->name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)result.samples, params->name);
        TEST_ASSERT_EQUAL_INT(2, result.num_players);
        if (params->hashipot)
            TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0,
                                      result.results[0].equity +
                                          result.results[1].equity);
        checked++;
        if ((enum_game_t)game == game_soko)
            saw_soko = 1;
    }

    /* The stud, draw, lowball and Sökö families are all in the matrix. */
    TEST_ASSERT_GREATER_THAN_INT(8, checked);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, saw_soko, "Sökö missing from the matrix");
}

/* Sökö specifically: a four-card flush beats one pair, exactly, with one
 * sample (the regression this matrix is here to keep). */
static void test_soko_four_flush_exact(void) {
    StdDeck_CardMask board, dead, flush_hand, pair_hand;
    pe_combo_t combos[2];
    pe_range_t ranges[2];
    const pe_range_t *range_ptrs[2];
    pe_equity_result_multi_t result;
    int card = -1;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(flush_hand);
    StdDeck_CardMask_RESET(pair_hand);
    {
        const char *flush_cards[] = {"9h", "Th", "Jh", "3h", "2s"};
        const char *pair_cards[] = {"Ah", "Ad", "Ks", "8c", "3c"};
        int i;

        for (i = 0; i < 5; ++i) {
            char buf[3];

            buf[0] = flush_cards[i][0];
            buf[1] = flush_cards[i][1];
            buf[2] = '\0';
            TEST_ASSERT_EQUAL_INT(2, StdDeck_stringToCard(buf, &card));
            StdDeck_CardMask_SET(flush_hand, card);
            buf[0] = pair_cards[i][0];
            buf[1] = pair_cards[i][1];
            TEST_ASSERT_EQUAL_INT(2, StdDeck_stringToCard(buf, &card));
            StdDeck_CardMask_SET(pair_hand, card);
        }
    }

    init_single_range(&ranges[0], &combos[0], flush_hand, game_soko);
    init_single_range(&ranges[1], &combos[1], pair_hand, game_soko);
    range_ptrs[0] = &ranges[0];
    range_ptrs[1] = &ranges[1];

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK,
                          pe_equity_calculate(game_soko, range_ptrs, 2, board,
                                              dead, NULL, &result));
    TEST_ASSERT_EQUAL_INT(1, result.exact);
    TEST_ASSERT_EQUAL_INT(1, (int)result.samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, result.results[0].equity);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_boardless_games_are_exact);
    RUN_TEST(test_soko_four_flush_exact);
    return UNITY_END();
}
