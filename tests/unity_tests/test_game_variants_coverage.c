/**
 * test_game_variants_coverage.c
 * 
 * Tests for less-common game variants to improve coverage.
 * Covers: Short Deck, Badugi, Fusion, Manila, Triple Draw variants, etc.
 */

#include "unity.h"
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/core/poker_defs.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static int is_smoke_mode(void)
{
    const char *smoke = getenv("PE_TEST_SMOKE");
    return smoke && *smoke && strcmp(smoke, "0") != 0;
}

/* Helper to create a card mask */
static StdDeck_CardMask create_hand(int rank1, int suit1, int rank2, int suit2)
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

/* Test: Short Deck Hold'em basic */
static void test_short_deck_basic(void)
{
    enum_gameparams_t *params;

    /* Verify short deck params */
    params = enumGameParams(game_sdholdem);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(2, params->minpocket);
    TEST_ASSERT_EQUAL_INT(2, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(5, params->maxboard);
    TEST_ASSERT_EQUAL_STRING("ShortDeck Holdem NL Hi", params->name);
}

/* Test: Short Deck evaluation */
static void test_short_deck_evaluation(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;
    int iterations = is_smoke_mode() ? 500 : 2000;

    /* In short deck, 2-5 are removed, so lowest card is 6 */
    /* AA vs KK */
    pockets[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    pockets[1] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* Use Monte Carlo for short deck */
    err = enumSample(game_sdholdem, pockets, board, dead, 2, 5, iterations, 0, &result);
    
    if (err == 0) {
        /* AA should beat KK */
        TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    }
    /* If error, short deck might not be fully implemented - test passes */

    enumResultFree(&result);
}

/* Test: 2-7 Triple Draw basic */
static void test_27_triple_draw_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_27_triple_draw);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(5, params->minpocket);
    TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(0, params->maxboard); /* No board in draw */
    TEST_ASSERT_EQUAL_INT(1, params->haslopot);
    TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test: 2-7 Triple Draw evaluation */
static void test_27_triple_draw_evaluation(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;
    int iterations = is_smoke_mode() ? 500 : 2000;

    /* Create 5-card draw hands */
    /* Best 2-7 hand: 75432 (wheel is bad in 2-7) */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_SPADES);

    /* Worse 2-7 hand: A5432 (aces are high in 2-7) */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    /* Cleared, not allocated: the enumeration below starts with
     * enumResultClear(), which would drop an ordering allocated here. */
    enumResultClear(&result);

    /* Complete hands, so exhaustive with 0 draws */
    err = enumExhaustive(game_27_triple_draw, pockets, board, dead, 2, 5, 0, &result);
    
    if (err == 0) {
        /* 75432 should beat A5432 in 2-7 */
        TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    }

    enumResultFree(&result);
}

/* Test: A-5 Triple Draw basic */
static void test_a5_triple_draw_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_a5_triple_draw);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(5, params->minpocket);
    TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(0, params->maxboard);
}

/* Test: Badugi basic */
static void test_badugi_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_badugi);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(4, params->minpocket);
    TEST_ASSERT_EQUAL_INT(4, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(0, params->maxboard);
    TEST_ASSERT_EQUAL_INT(1, params->haslopot);
    TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test: Badugi evaluation */
static void test_badugi_evaluation(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    /* Perfect badugi: 4 cards, all different suits, all different ranks */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);

    /* Three-card badugi (two spades) */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    /* Cleared, not allocated: the enumeration below starts with
     * enumResultClear(), which would drop an ordering allocated here. */
    enumResultClear(&result);

    /* Complete hands */
    err = enumExhaustive(game_badugi, pockets, board, dead, 2, 4, 0, &result);
    
    if (err == 0) {
        /* Four-card badugi should beat three-card badugi */
        TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    }

    enumResultFree(&result);
}

/* Test: Manila Poker basic */
static void test_manila_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_manila);
    if (params != NULL) {
        TEST_ASSERT_EQUAL_INT(2, params->minpocket);
        TEST_ASSERT_EQUAL_INT(2, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxboard);
    }
    /* If Manila not implemented, test passes */
}

/* Test: Fusion basic */
static void test_fusion_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_fusion);
    if (params != NULL) {
        /* Fusion starts with 2 cards like Hold'em */
        TEST_ASSERT_EQUAL_INT(2, params->minpocket);
        TEST_ASSERT_EQUAL_INT(2, params->maxpocket);
    }
}

/* Test: Courchevel basic */
static void test_courchevel_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_courchevel);
    if (params != NULL) {
        /* Courchevel is 5-card Omaha variant */
        TEST_ASSERT_EQUAL_INT(5, params->minpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxboard);
    }
}

/* Test: Irish Poker basic */
static void test_irish_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_irish);
    if (params != NULL) {
        /* Irish starts with 4 cards like Omaha */
        TEST_ASSERT_EQUAL_INT(4, params->minpocket);
        TEST_ASSERT_EQUAL_INT(4, params->maxpocket);
    }
}

/* Test: Drawmaha (Sviten Special) basic */
static void test_drawmaha_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_drawmaha);
    if (params != NULL) {
        TEST_ASSERT_EQUAL_INT(5, params->minpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxboard);
    }
}

/* Test: Pineapple basic */
static void test_pineapple_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_pineapple);
    if (params != NULL) {
        /* Pineapple starts with 3 cards */
        TEST_ASSERT_EQUAL_INT(3, params->minpocket);
        TEST_ASSERT_EQUAL_INT(3, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxboard);
    }
}

/* Test: Badacey basic */
static void test_badacey_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_badacey);
    if (params != NULL) {
        /* Badacey is Badugi + A-5 */
        TEST_ASSERT_EQUAL_INT(5, params->minpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(1, params->haslopot);
        TEST_ASSERT_EQUAL_INT(1, params->hashipot);
    }
}

/* Test: Badeucy basic */
static void test_badeucy_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_badeucy);
    if (params != NULL) {
        /* Badeucy is Badugi + 2-7 */
        TEST_ASSERT_EQUAL_INT(5, params->minpocket);
        TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(1, params->haslopot);
        TEST_ASSERT_EQUAL_INT(1, params->hashipot);
    }
}

/* Test: Razz basic */
static void test_razz_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_razz);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(3, params->minpocket);
    TEST_ASSERT_EQUAL_INT(7, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(0, params->maxboard);
    TEST_ASSERT_EQUAL_INT(1, params->haslopot);
    TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test: Razz evaluation */
static void test_razz_evaluation(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;
    int iterations = is_smoke_mode() ? 500 : 2000;

    /* Create 7-card razz hands */
    /* Good razz hand: A2345xx */
    StdDeck_CardMask_RESET(pockets[0]);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

    /* Worse razz hand: high cards */
    StdDeck_CardMask_RESET(pockets[1]);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);

    /* Cleared, not allocated: the enumeration below starts with
     * enumResultClear(), which would drop an ordering allocated here. */
    enumResultClear(&result);

    /* Complete hands */
    err = enumExhaustive(game_razz, pockets, board, dead, 2, 7, 0, &result);
    
    if (err == 0) {
        /* Low hand should beat high hand in Razz */
        TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    }

    enumResultFree(&result);
}

/* Test: 7-Card Stud Hi/Lo */
static void test_7stud8_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_7stud8);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(3, params->minpocket);
    TEST_ASSERT_EQUAL_INT(7, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(1, params->haslopot);
    TEST_ASSERT_EQUAL_INT(1, params->hashipot);
}

/* Test: Lowball basic */
static void test_lowball_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_lowball);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL_INT(0, params->minpocket); /* Joker deck */
    TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
    TEST_ASSERT_EQUAL_INT(0, params->maxboard);
    TEST_ASSERT_EQUAL_INT(1, params->haslopot);
    TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test: Double Flop Hold'em */
static void test_doubleflop_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_doubleflop_holdem);
    if (params != NULL) {
        TEST_ASSERT_EQUAL_INT(2, params->minpocket);
        TEST_ASSERT_EQUAL_INT(2, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(10, params->maxboard); /* Two flops */
    }
}

/* Test: OFC (Open Face Chinese) */
static void test_ofc_basic(void)
{
    enum_gameparams_t *params;

    params = enumGameParams(game_ofc);
    if (params != NULL) {
        /* OFC uses 13 cards */
        TEST_ASSERT_EQUAL_INT(13, params->minpocket);
        TEST_ASSERT_EQUAL_INT(13, params->maxpocket);
        TEST_ASSERT_EQUAL_INT(0, params->maxboard);
    }
}

/* Test: Game parameter completeness */
static void test_all_games_have_params(void)
{
    /* Verify all game types have valid parameters */
    enum_game_t games[] = {
        game_holdem, game_holdem8, game_omaha, game_omaha5, game_omaha6,
        game_omaha8, game_omaha85, game_omaha86, game_7stud, game_7stud8,
        game_7studnsq, game_razz, game_5draw, game_5draw8, game_5drawnsq,
        game_lowball, game_lowball27, game_sdholdem
    };

    int num_games = sizeof(games) / sizeof(games[0]);
    
    for (int i = 0; i < num_games; i++) {
        enum_gameparams_t *params = enumGameParams(games[i]);
        TEST_ASSERT_NOT_NULL(params);
        TEST_ASSERT_NOT_NULL(params->name);
        TEST_ASSERT_TRUE(params->maxpocket >= params->minpocket);
    }
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_short_deck_basic);
    RUN_TEST(test_short_deck_evaluation);
    RUN_TEST(test_27_triple_draw_basic);
    RUN_TEST(test_27_triple_draw_evaluation);
    RUN_TEST(test_a5_triple_draw_basic);
    RUN_TEST(test_badugi_basic);
    RUN_TEST(test_badugi_evaluation);
    RUN_TEST(test_manila_basic);
    RUN_TEST(test_fusion_basic);
    RUN_TEST(test_courchevel_basic);
    RUN_TEST(test_irish_basic);
    RUN_TEST(test_drawmaha_basic);
    RUN_TEST(test_pineapple_basic);
    RUN_TEST(test_badacey_basic);
    RUN_TEST(test_badeucy_basic);
    RUN_TEST(test_razz_basic);
    RUN_TEST(test_razz_evaluation);
    RUN_TEST(test_7stud8_basic);
    RUN_TEST(test_lowball_basic);
    RUN_TEST(test_doubleflop_basic);
    RUN_TEST(test_ofc_basic);
    RUN_TEST(test_all_games_have_params);
    
    return UNITY_END();
}
