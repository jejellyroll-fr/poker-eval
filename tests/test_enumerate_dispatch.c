/*
 * test_enumerate_dispatch.c -- tests for enumerate_dispatch.c routing logic
 *
 * Validates that the dispatch router correctly routes game types to the
 * appropriate backend (classic, EEDC, or EEDC Omaha optimized).
 */

#include "unity.h"
#include <stdlib.h>
#include <string.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumerate.h> /* enumerate_dispatch.h does not exist */

/* Helper to add a card to a mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit)
{
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Build dead cards from pockets and board */
static void build_dead(StdDeck_CardMask *dead, StdDeck_CardMask pockets[], int npockets,
                       StdDeck_CardMask board)
{
    StdDeck_CardMask_RESET(*dead);
    for (int i = 0; i < npockets; i++)
        StdDeck_CardMask_OR(*dead, *dead, pockets[i]);
    StdDeck_CardMask_OR(*dead, *dead, board);
}

void setUp(void) {}
void tearDown(void) {}

/*
 * Test that Holdem games route to EEDC backend
 */
void test_dispatch_holdem_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: AcKc */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    /* Player 2: 7h2d */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    /* No board */
    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
    TEST_ASSERT_GREATER_THAN(0, result.nsamples);
}

/*
 * Test that Omaha games route to EEDC Omaha optimized backend
 */
void test_dispatch_omaha_uses_eedc_omaha_opt(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: AcAdKcKd */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    /* Player 2: QsQhJsJh */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    /* Board: Ts9s8s */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_omaha, pockets, board, dead, 2, 3, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc_omaha_opt, enum_dispatch_last_backend());
    TEST_ASSERT_GREATER_THAN(0, result.nsamples);
}

/*
 * Test that Omaha5 also routes to EEDC Omaha optimized
 */
void test_dispatch_omaha5_uses_eedc_omaha_opt(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: 5 cards */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

    /* Player 2: 5 cards */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    /* Board: 5 cards */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_omaha5, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc_omaha_opt, enum_dispatch_last_backend());
}

/*
 * Test that Omaha8 (hi/lo) routes to EEDC Omaha optimized
 */
void test_dispatch_omaha8_uses_eedc_omaha_opt(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: Ac2c3dKh */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    /* Player 2: KsKdQsQd */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    /* Board: 4h5s6c */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_omaha8, pockets, board, dead, 2, 3, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc_omaha_opt, enum_dispatch_last_backend());
}

/*
 * Test that 7-card stud routes to EEDC backend
 */
void test_dispatch_stud_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: AcAdAsKcKd */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    /* Player 2: QsQhQdJsJh */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    /* No board for stud */
    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_7stud, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
    TEST_ASSERT_GREATER_THAN(0, result.nsamples);
}

/*
 * Test that Razz routes to EEDC backend
 */
void test_dispatch_razz_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: Ac2d3h4s5c */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    /* Player 2: 6c7d8h9sTc */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_razz, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test that 5-card draw routes to EEDC backend
 */
void test_dispatch_draw_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: AcAdAsKcKd */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    /* Player 2: QsQhQdJsJh */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_5draw, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test that lowball routes to EEDC backend
 */
void test_dispatch_lowball_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: Ac2d3h4s5c (wheel) */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    /* Player 2: 2c3d4h5s6c */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_lowball, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test that 2-7 lowball routes to EEDC backend
 */
void test_dispatch_lowball27_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: 2c3d4h5s7c (best 2-7 low) */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_CLUBS);

    /* Player 2: 2d3h4s5c8d */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_lowball27, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test that Holdem8 (hi/lo) routes to EEDC backend
 */
void test_dispatch_holdem8_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: Ac2c */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);

    /* Player 2: KsKh */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    /* Board: 3d4h5s */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_holdem8, pockets, board, dead, 2, 3, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test that short deck holdem routes to EEDC backend
 */
void test_dispatch_shortdeck_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: AcKc */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    /* Player 2: QsQh */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    /* Board: Ts9s8s */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_sdholdem, pockets, board, dead, 2, 3, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test enum_dispatch_last_backend() returns the correct value after multiple calls
 */
void test_dispatch_last_backend(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    /* Setup basic holdem hands */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    /* First call with holdem - should use EEDC */
    enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());

    /* Setup Omaha hands */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    /* Board: full 5 cards */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);

    /* Second call with omaha - should use EEDC Omaha opt */
    enumExhaustive_dispatch(game_omaha, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc_omaha_opt, enum_dispatch_last_backend());
}

/*
 * Test enum_dispatch_backend_name() returns correct names
 */
void test_dispatch_backend_name(void)
{
    TEST_ASSERT_EQUAL_STRING("classic", enum_dispatch_backend_name(enum_dispatch_backend_classic));
    TEST_ASSERT_EQUAL_STRING("eedc", enum_dispatch_backend_name(enum_dispatch_backend_eedc));
    TEST_ASSERT_EQUAL_STRING("eedc_omaha_opt", enum_dispatch_backend_name(enum_dispatch_backend_eedc_omaha_opt));
    TEST_ASSERT_EQUAL_STRING("unknown", enum_dispatch_backend_name((enum_dispatch_backend_t)999));
}

/*
 * Test that dispatch results match classic enumExhaustive results
 * (verifies correctness, not just routing)
 */
void test_dispatch_results_match_classic(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t dispatch_result, classic_result;
    int dispatch_err, classic_err;

    /* Player 1: AcKc */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    /* Player 2: 7h2d */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    /* Board: Ts9s8s7c */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);

    /* Call both dispatch and classic */
    dispatch_err = enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, 4, 0, &dispatch_result);
    classic_err = enumExhaustive(game_holdem, pockets, board, dead, 2, 4, 0, &classic_result);

    TEST_ASSERT_EQUAL_INT(0, dispatch_err);
    TEST_ASSERT_EQUAL_INT(0, classic_err);

    /* Results should match */
    TEST_ASSERT_EQUAL_INT(classic_result.nsamples, dispatch_result.nsamples);
    TEST_ASSERT_EQUAL_INT(classic_result.nwinhi[0], dispatch_result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(classic_result.nwinhi[1], dispatch_result.nwinhi[1]);
    TEST_ASSERT_EQUAL_INT(classic_result.ntiehi[0], dispatch_result.ntiehi[0]);
    TEST_ASSERT_EQUAL_INT(classic_result.ntiehi[1], dispatch_result.ntiehi[1]);
}

/*
 * Test dispatch with 3 players
 */
void test_dispatch_3player(void)
{
    StdDeck_CardMask pockets[3], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: AcKc */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    /* Player 2: QsQh */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    /* Player 3: 7h2d */
    StdDeck_CardMask_RESET(pockets[2]);
    add_card(&pockets[2], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[2], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    /* Board: Ts9s8s */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 3, board);

    err = enumExhaustive_dispatch(game_holdem, pockets, board, dead, 3, 3, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
    TEST_ASSERT_GREATER_THAN(0, result.nsamples);
}

/*
 * Test 7-Stud Hi/Lo routes to EEDC
 */
void test_dispatch_7stud8_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: Ac2d3h4s5c (wheel for low) */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    /* Player 2: AhAdAsKcKd (trips for high) */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_7stud8, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test 2-7 triple draw routes to EEDC
 */
void test_dispatch_27_triple_draw_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: 2c3d4h5s7c */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_CLUBS);

    /* Player 2: 2d3h4s5c8d */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_27_triple_draw, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

/*
 * Test A-5 triple draw routes to EEDC
 */
void test_dispatch_a5_triple_draw_uses_eedc(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Player 1: Ac2d3h4s5c */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    /* Player 2: Ad2h3c4d6c */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(board);
    build_dead(&dead, pockets, 2, board);

    err = enumExhaustive_dispatch(game_a5_triple_draw, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(enum_dispatch_backend_eedc, enum_dispatch_last_backend());
}

int main(void)
{
    UNITY_BEGIN();

    /* Holdem family routing */
    RUN_TEST(test_dispatch_holdem_uses_eedc);
    RUN_TEST(test_dispatch_holdem8_uses_eedc);
    RUN_TEST(test_dispatch_shortdeck_uses_eedc);

    /* Omaha family routing */
    RUN_TEST(test_dispatch_omaha_uses_eedc_omaha_opt);
    RUN_TEST(test_dispatch_omaha5_uses_eedc_omaha_opt);
    RUN_TEST(test_dispatch_omaha8_uses_eedc_omaha_opt);

    /* Stud family routing */
    RUN_TEST(test_dispatch_stud_uses_eedc);
    RUN_TEST(test_dispatch_razz_uses_eedc);
    RUN_TEST(test_dispatch_7stud8_uses_eedc);

    /* Draw games routing */
    RUN_TEST(test_dispatch_draw_uses_eedc);
    RUN_TEST(test_dispatch_lowball_uses_eedc);
    RUN_TEST(test_dispatch_lowball27_uses_eedc);
    RUN_TEST(test_dispatch_27_triple_draw_uses_eedc);
    RUN_TEST(test_dispatch_a5_triple_draw_uses_eedc);

    /* Utility functions */
    RUN_TEST(test_dispatch_last_backend);
    RUN_TEST(test_dispatch_backend_name);

    /* Result correctness */
    RUN_TEST(test_dispatch_results_match_classic);
    RUN_TEST(test_dispatch_3player);

    return UNITY_END();
}
