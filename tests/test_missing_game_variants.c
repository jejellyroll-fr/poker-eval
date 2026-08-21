/* ISSUE-08/#164: public registry and deck/ranking smoke tests for variants
 * that were previously absent from enum_game_t. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/configurable_ranking.h>
#include <poker_eval/deck/generalized_deck.h>
#include <poker_eval/deck/deck_std.h>

static StdDeck_CardMask cards(const int *ids, size_t count)
{
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);
    for (size_t i = 0; i < count; ++i)
        StdDeck_CardMask_SET(mask, ids[i]);
    return mask;
}

static void test_variant_registry(void)
{
    const enum_game_t games[] = {
        game_royal, game_astud, game_italian, game_archie,
        game_badugi_hilo, game_drawmaha49, game_drawmaha_zero,
        game_drawmaha_dugi, game_doubleboard_omaha85, game_chinese13
    };
    const char *names[] = {
        "Royal Hold'em (20-card deck)", "Asian / Spanish Stud", "Italian Poker",
        "Archie Triple Draw Hi/Lo", "Badugi Hi/Lo", "Drawmaha 49",
        "Drawmaha Zero", "Drawmaha Low-Dugi",
        "Double-board Omaha 5 Hi/Lo", "Classic Chinese Poker 13-card"
    };
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); ++i) {
        enum_gameparams_t *params = enumGameParams(games[i]);
        assert(params != NULL);
        assert(params->game == games[i]);
        assert(strcmp(params->name, names[i]) == 0);
        assert(params->minpocket > 0 && params->maxpocket >= params->minpocket);
    }
}

static void test_generalized_decks(void)
{
    pe_deck_spec_t royal, spanish, short_deck;
    assert(pe_deck_get_predefined(PE_DECK_PRESET_ROYAL, &royal) == 0);
    assert(pe_deck_get_predefined(PE_DECK_PRESET_SPANISH, &spanish) == 0);
    assert(pe_deck_get_predefined(PE_DECK_PRESET_SHORT, &short_deck) == 0);
    assert(royal.num_cards == 20);
    assert(spanish.num_cards == 32);
    assert(short_deck.num_cards == 36);
}

static void test_italian_ranking(void)
{
    pe_hand_ranking_config_t config;
    assert(pe_ranking_config_set_preset("italian_manila", &config) == 0);
    assert(pe_ranking_config_category_rank(&config, PE_CAT_FLUSH) >
           pe_ranking_config_category_rank(&config, PE_CAT_FULL_HOUSE));
}

static void test_enumeration_paths(void)
{
    const int royal_pocket[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES)
    };
    const int royal_board[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS)
    };
    const int draw_pocket[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES)
    };
    const int badugi_pocket[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS)
    };
    const int astud_pocket[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_CLUBS)
    };
    const int chinese_pocket[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS)
    };
    const int second_board[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS),
        StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS)
    };
    StdDeck_CardMask pocket = cards(royal_pocket, 2);
    StdDeck_CardMask board = cards(royal_board, 5);
    StdDeck_CardMask draw = cards(draw_pocket, 5);
    StdDeck_CardMask badugi = cards(badugi_pocket, 4);
    StdDeck_CardMask astud = cards(astud_pocket, 7);
    StdDeck_CardMask chinese = cards(chinese_pocket, 13);
    StdDeck_CardMask board2 = cards(second_board, 5);
    StdDeck_CardMask board10;
    StdDeck_CardMask_OR(board10, board, board2);
    StdDeck_CardMask empty;
    StdDeck_CardMask_RESET(empty);
    enum_result_t result = {0};

    assert(enumExhaustive(game_royal, &pocket, board, empty,
                          1, 5, 0, &result) == 0);
    assert(result.nsamples == 1);
    assert(enumExhaustive(game_italian, &pocket, board, empty,
                          1, 5, 0, &result) == 0);
    assert(enumExhaustive(game_astud, &astud, empty, empty,
                          1, 0, 0, &result) == 0);
    assert(enumExhaustive(game_archie, &draw, empty, empty,
                          1, 0, 0, &result) == 0);
    assert(result.nwinhi[0] == 0);
    assert(enumExhaustive(game_badugi_hilo, &badugi, empty, empty,
                          1, 0, 0, &result) == 0);
    assert(enumSample(game_doubleboard_omaha85, &draw, empty, empty,
                      1, 0, 1, 0, &result) == 0);
    assert(enumSample(game_doubleboard_omaha85, &draw, board10, empty,
                      1, 10, 1, 0, &result) == 0);
    assert(enumExhaustive(game_doubleboard_omaha85, &draw, board10, empty,
                          1, 10, 0, &result) == 0);
    assert(enumSample(game_royal, &pocket, board, empty,
                      1, 5, 1, 0, &result) == 0);
    assert(enumSample(game_italian, &pocket, board, empty,
                      1, 5, 1, 0, &result) == 0);
    assert(enumExhaustive(game_drawmaha49, &draw, board, empty,
                          1, 5, 0, &result) == 0);
    assert(enumExhaustive(game_drawmaha_zero, &draw, board, empty,
                          1, 5, 0, &result) == 0);
    assert(enumExhaustive(game_drawmaha_dugi, &draw, board, empty,
                          1, 5, 0, &result) == 0);
    assert(enumExhaustive(game_chinese13, &chinese, empty, empty,
                          1, 0, 0, &result) == 0);
    enumResultFree(&result);
}

int main(void)
{
    test_variant_registry();
    test_generalized_decks();
    test_italian_ranking();
    test_enumeration_paths();
    puts("Missing game variant registry tests passed");
    return 0;
}
