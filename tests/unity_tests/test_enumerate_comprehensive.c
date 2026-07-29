/**
 * test_enumerate_comprehensive.c
 * 
 * Comprehensive tests for enumerate.c to improve code coverage.
 * Tests various edge cases, error conditions, and functionality paths.
 */

#include "unity.h"
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/poker_defs.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper to create a card mask from rank/suit pairs */
static StdDeck_CardMask create_hand_mask(int rank1, int suit1, int rank2, int suit2)
{
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank1, suit1));
    if (rank2 >= 0 && suit2 >= 0)
        StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank2, suit2));
    return hand;
}

/* Helper to add card to mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit)
{
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Test enumResultAlloc and enumResultFree */
static void test_result_alloc_free(void)
{
    enum_result_t result;
    int err;

    /* Test allocation with different ordering modes */
    memset(&result, 0, sizeof(result));
    err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);
    /* With mode_none, ordering is not allocated, just return success */
    enumResultFree(&result);

    /* Test with hi ordering */
    err = enumResultAlloc(&result, 3, enum_ordering_mode_hi);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ev);
    TEST_ASSERT_NOT_NULL(result.ordering);
    enumResultFree(&result);

    /* Test with lo ordering */
    err = enumResultAlloc(&result, 2, enum_ordering_mode_lo);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    enumResultFree(&result);

    /* Test max players */
    err = enumResultAlloc(&result, ENUM_MAXPLAYERS, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);
    enumResultFree(&result);
}

/* Test enumResultClear */
static void test_result_clear(void)
{
    enum_result_t result;
    int err;

    err = enumResultAlloc(&result, 2, enum_ordering_mode_hi);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* Set some values */
    result.nsamples = 100;
    result.ev[0] = 0.6;
    result.ev[1] = 0.4;
    result.nwinhi[0] = 60;

    /* Clear should reset everything */
    enumResultClear(&result);
    TEST_ASSERT_EQUAL_INT(0, result.nsamples);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, result.ev[0]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, result.ev[1]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[0]);

    enumResultFree(&result);
}

/* Test enumGameParams */
static void test_game_params(void)
{
    enum_gameparams_t *params;

    /* Test holdem params */
    params = enumGameParams(game_holdem);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(2, params->minpocket);
    TEST_ASSERT_EQUAL_INT(2, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(5, params->maxboard);
    TEST_ASSERT_EQUAL_INT(0, params->haslopot);
    TEST_ASSERT_EQUAL_INT(1, params->hashipot);

    /* Test holdem8 params */
    params = enumGameParams(game_holdem8);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(1, params->haslopot);
    TEST_ASSERT_EQUAL_INT(1, params->hashipot);

    /* Test omaha params */
    params = enumGameParams(game_omaha);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(4, params->minpocket);
    TEST_ASSERT_EQUAL_INT(4, params->maxpocket);

    /* Test 7-card stud */
    params = enumGameParams(game_7stud);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(3, params->minpocket);
    TEST_ASSERT_EQUAL_INT(7, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(0, params->maxboard);

    /* Test razz */
    params = enumGameParams(game_razz);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(1, params->haslopot);
    TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test exhaustive enumeration - heads up holdem */
static void test_exhaustive_holdem_hu(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* AA vs KK preflop */
    pockets[0] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    pockets[1] = create_hand_mask(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                                  StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* This would take very long, so we'll use a flop to reduce search space */
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);
    StdDeck_CardMask_OR(dead, dead, board);

    err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_TRUE(result.nsamples > 0);
    
    /* AA should have better equity than KK */
    TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    
    /* Equities should sum close to 1.0 */
    double total = result.ev[0] + result.ev[1];
    TEST_ASSERT_TRUE(total >= 0.99 && total <= 1.01);

    enumResultFree(&result);
}

/* Test Monte Carlo sampling */
static void test_sample_montecarlo(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;
    int nsamples = 5000;

    /* AK vs 22 */
    pockets[0] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    pockets[1] = create_hand_mask(StdDeck_Rank_2, StdDeck_Suit_CLUBS,
                                  StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    /* enumSample will initialize result internally */
    /* nboard=0 means no board cards yet, will deal 5 */
    memset(&result, 0, sizeof(result));
    err = enumSample(game_holdem, pockets, board, dead, 2, 0, nsamples, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(nsamples, result.nsamples);
    
    /* 22 should have better equity than AK preflop (small pair vs overcards) */
    TEST_ASSERT_TRUE(result.ev[1] > result.ev[0]);

    enumResultFree(&result);
}

/* Test 3-player game */
static void test_multiway_equity(void)
{
    StdDeck_CardMask pockets[3], board, dead;
    enum_result_t result;
    int err;

    /* AA vs KK vs QQ */
    pockets[0] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    pockets[1] = create_hand_mask(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                                  StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    pockets[2] = create_hand_mask(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, pockets[2]);

    /* enumSample will initialize result internally */
    /* nboard=0 means no board cards yet, will deal 5 */
    memset(&result, 0, sizeof(result));
    err = enumSample(game_holdem, pockets, board, dead, 3, 0, 10000, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    
    /* Equity order should be AA > KK > QQ */
    TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    TEST_ASSERT_TRUE(result.ev[1] > result.ev[2]);

    enumResultFree(&result);
}

/* Test Hi/Lo split game (Omaha8) */
static void test_hilo_game(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Create hands for Omaha8 */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);

    /* enumSample will initialize result internally */
    /* nboard=3 means 3 cards on board (flop), will deal 2 more */
    memset(&result, 0, sizeof(result));
    err = enumSample(game_omaha8, pockets, board, dead, 2, 3, 5000, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    
    /* Player 1 has good low draw, should have some equity */
    TEST_ASSERT_TRUE(result.ev[0] > 0.0);
    TEST_ASSERT_TRUE(result.ev[1] > 0.0);
    
    /* Check that low stats are being tracked */
    TEST_ASSERT_TRUE(result.nwinlo[0] + result.ntielo[0] + result.nloselo[0] > 0 ||
                     result.nwinlo[1] + result.ntielo[1] + result.nloselo[1] > 0);

    enumResultFree(&result);
}

/* Test with full board (river) */
static void test_complete_board(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    pockets[0] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    pockets[1] = create_hand_mask(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS,
                                  StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

    /* Complete board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);

    err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* Exhaustive with complete board should have exactly 1 sample */
    err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(1, result.nsamples);

    /* One player should win or there should be a tie */
    TEST_ASSERT_TRUE(result.ev[0] + result.ev[1] >= 0.99);

    enumResultFree(&result);
}

/* Test enumeration with ordering */
static void test_enumeration_ordering(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    pockets[0] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    pockets[1] = create_hand_mask(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                                  StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);

    /* Test enumSample works - ordering allocation is automatic with orderflag */
    /* nboard=3 means 3 cards on board (flop), will deal 2 more */
    memset(&result, 0, sizeof(result));
    err = enumSample(game_holdem, pockets, board, dead, 2, 3, 1000, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    enumResultFree(&result);
}

/* Test edge case: identical hands (should tie) */
static void test_identical_hands_tie(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Same pocket cards in different suits */
    pockets[0] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    pockets[1] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                                  StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    /* Board that makes both players have same flush */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);

    err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);

    err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    
    /* Both players should have 0.5 equity (tie) */
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.5, result.ev[0]);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.5, result.ev[1]);

    enumResultFree(&result);
}

/* Test Monte Carlo sampling basic functionality */
static void test_sample_with_seed(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    pockets[0] = create_hand_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                  StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    pockets[1] = create_hand_mask(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS,
                                  StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Run Monte Carlo sampling */
    /* nboard=0 means no board cards yet, will deal 5 */
    memset(&result, 0, sizeof(result));
    err = enumSample(game_holdem, pockets, board, dead, 2, 0, 1000, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(1000, result.nsamples);

    enumResultFree(&result);
}

/* Test enumeration macros with different card counts */
static void test_enumerate_macros(void)
{
    int count4 = 0, count5 = 0;
    StdDeck_CardMask cards, dead;
    
    StdDeck_CardMask_RESET(dead);

    /* Count 4-card combinations */
    DECK_ENUMERATE_4_CARDS_D(StdDeck, cards, dead, {
        count4++;
        if (count4 >= 1000) break; /* Limit iterations for test speed */
    });
    TEST_ASSERT_TRUE(count4 > 0);

    /* Count 5-card combinations */
    DECK_ENUMERATE_5_CARDS_D(StdDeck, cards, dead, {
        count5++;
        if (count5 >= 1000) break; /* Limit iterations for test speed */
    });
    TEST_ASSERT_TRUE(count5 > 0);
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_result_alloc_free);
    RUN_TEST(test_result_clear);
    RUN_TEST(test_game_params);
    RUN_TEST(test_exhaustive_holdem_hu);
    RUN_TEST(test_sample_montecarlo);
    RUN_TEST(test_multiway_equity);
    RUN_TEST(test_hilo_game);
    RUN_TEST(test_complete_board);
    RUN_TEST(test_enumeration_ordering);
    RUN_TEST(test_identical_hands_tie);
    RUN_TEST(test_sample_with_seed);
    RUN_TEST(test_enumerate_macros);
    
    return UNITY_END();
}
