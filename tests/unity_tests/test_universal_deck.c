/**
 * test_universal_deck.c
 *
 * Tests for universal_deck.c functions to improve code coverage.
 * Tests deck abstraction layer for standard and joker decks.
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/universal_deck.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/generalized_deck.h>

void setUp(void) {}
void tearDown(void) {}

/* Test: Universal_CardMask_RESET for standard deck */
static void test_reset_std_deck(void) {
  UniversalCardMask mask;
  mask.std.cards_n = 0xFFFFFFFF;

  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_STANDARD);
  TEST_ASSERT_TRUE(StdDeck_CardMask_IS_EMPTY(mask.std));
}

/* Test: Universal_CardMask_RESET for joker deck */
static void test_reset_joker_deck(void) {
  UniversalCardMask mask;

  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_JOKER);
  TEST_ASSERT_TRUE(JokerDeck_CardMask_IS_EMPTY(mask.joker));
}

/* Test: Universal_CardMask_SET for standard deck */
static void test_set_std_deck(void) {
  UniversalCardMask mask;
  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_STANDARD);

  int card = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  Universal_CardMask_SET(&mask, card, UNIVERSAL_DECK_STANDARD);

  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(mask, card, UNIVERSAL_DECK_STANDARD));
}

/* Test: Universal_CardMask_SET for joker deck */
static void test_set_joker_deck(void) {
  UniversalCardMask mask;
  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_JOKER);

  int card = JokerDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  Universal_CardMask_SET(&mask, card, UNIVERSAL_DECK_JOKER);

  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(mask, card, UNIVERSAL_DECK_JOKER));
}

/* Test: Universal_CardMask_OR standard deck */
static void test_or_std_deck(void) {
  UniversalCardMask m1, m2, result;
  Universal_CardMask_RESET(&m1, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_RESET(&m2, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_RESET(&result, UNIVERSAL_DECK_STANDARD);

  int card1 = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  int card2 = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES);

  Universal_CardMask_SET(&m1, card1, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_SET(&m2, card2, UNIVERSAL_DECK_STANDARD);

  Universal_CardMask_OR(&result, m1, m2, UNIVERSAL_DECK_STANDARD);

  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(result, card1, UNIVERSAL_DECK_STANDARD));
  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(result, card2, UNIVERSAL_DECK_STANDARD));
}

/* Test: Universal_CardMask_OR joker deck */
static void test_or_joker_deck(void) {
  UniversalCardMask m1, m2, result;
  Universal_CardMask_RESET(&m1, UNIVERSAL_DECK_JOKER);
  Universal_CardMask_RESET(&m2, UNIVERSAL_DECK_JOKER);
  Universal_CardMask_RESET(&result, UNIVERSAL_DECK_JOKER);

  int card1 = JokerDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  int card2 = JokerDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES);

  Universal_CardMask_SET(&m1, card1, UNIVERSAL_DECK_JOKER);
  Universal_CardMask_SET(&m2, card2, UNIVERSAL_DECK_JOKER);

  Universal_CardMask_OR(&result, m1, m2, UNIVERSAL_DECK_JOKER);

  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(result, card1, UNIVERSAL_DECK_JOKER));
  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(result, card2, UNIVERSAL_DECK_JOKER));
}

/* Test: Universal_StringToCard standard */
static void test_string_to_card_std(void) {
  int card;
  deck_type_t type;

  int result = Universal_StringToCard("As", &card, &type);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_STANDARD, type);
}

/* Test: Universal_StringToCard joker */
static void test_string_to_card_joker(void) {
  int card;
  deck_type_t type;

  int result = Universal_StringToCard("Xx", &card, &type);
  TEST_ASSERT_EQUAL_INT(2, result);
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_JOKER, type);
  TEST_ASSERT_EQUAL_INT(JokerDeck_JOKER, card);
}

/* Test: Universal_CardToString standard */
static void test_card_to_string_std(void) {
  char str[4];
  int card = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);

  int result = Universal_CardToString(card, str, UNIVERSAL_DECK_STANDARD);
  TEST_ASSERT_TRUE(result > 0);
}

/* Test: Universal_CardToString joker */
static void test_card_to_string_joker(void) {
  char str[4];

  int result =
      Universal_CardToString(JokerDeck_JOKER, str, UNIVERSAL_DECK_JOKER);
  TEST_ASSERT_EQUAL_INT(1, result);
  TEST_ASSERT_EQUAL_STRING("Xx", str);
}

/* Test: Universal_numCards standard */
static void test_num_cards_std(void) {
  UniversalCardMask mask;
  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_STANDARD);

  Universal_CardMask_SET(
      &mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
      UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_SET(
      &mask, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES),
      UNIVERSAL_DECK_STANDARD);

  int count = Universal_numCards(mask, UNIVERSAL_DECK_STANDARD);
  TEST_ASSERT_EQUAL_INT(2, count);
}

/* Test: Universal_numCards joker */
static void test_num_cards_joker(void) {
  UniversalCardMask mask;
  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_JOKER);

  Universal_CardMask_SET(
      &mask, JokerDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
      UNIVERSAL_DECK_JOKER);

  int count = Universal_numCards(mask, UNIVERSAL_DECK_JOKER);
  TEST_ASSERT_EQUAL_INT(1, count);
}

/* Test: Universal_DetermineRequiredDeckType - standard games */
static void test_deck_type_std_games(void) {
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_STANDARD,
                        Universal_DetermineRequiredDeckType(game_holdem));
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_STANDARD,
                        Universal_DetermineRequiredDeckType(game_omaha));
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_STANDARD,
                        Universal_DetermineRequiredDeckType(game_7stud));
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_STANDARD,
                        Universal_DetermineRequiredDeckType(game_razz));
}

/* Test: Universal_DetermineRequiredDeckType - joker games */
static void test_deck_type_joker_games(void) {
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_JOKER,
                        Universal_DetermineRequiredDeckType(game_5draw));
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_JOKER,
                        Universal_DetermineRequiredDeckType(game_5draw8));
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_JOKER,
                        Universal_DetermineRequiredDeckType(game_lowball));
}

/* Test: Universal_IsJokerGame */
static void test_is_joker_game(void) {
  TEST_ASSERT_TRUE(Universal_IsJokerGame(game_5draw));
  TEST_ASSERT_TRUE(Universal_IsJokerGame(game_lowball));
  TEST_ASSERT_FALSE(Universal_IsJokerGame(game_holdem));
  TEST_ASSERT_FALSE(Universal_IsJokerGame(game_omaha));
}

/* Test: ConvertStdToJoker */
static void test_convert_std_to_joker(void) {
  StdDeck_CardMask std;
  JokerDeck_CardMask joker;

  StdDeck_CardMask_RESET(std);
  StdDeck_CardMask_SET(
      std, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

  Universal_ConvertStdToJoker(std, &joker);
  TEST_ASSERT_FALSE(JokerDeck_CardMask_IS_EMPTY(joker));
}

/* Test: ConvertJokerToStd */
static void test_convert_joker_to_std(void) {
  JokerDeck_CardMask joker;
  StdDeck_CardMask std;

  JokerDeck_CardMask_RESET(joker);
  JokerDeck_CardMask_SET(
      joker, JokerDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

  Universal_ConvertJokerToStd(joker, &std);
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(std));
}

/* ISSUE-03 phase 2 (#203): Universal_* mask ops delegate to pe_deck_*, so the
 * two layers must produce identical bits and counts for the standard deck. */
static void test_delegation_std_mask(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t pm = 0;
  UniversalCardMask mask;

  TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec));
  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_SET(&mask, 7, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_SET(&mask, 40, UNIVERSAL_DECK_STANDARD);

  pe_deck_mask_set(&spec, &pm, 7);
  pe_deck_mask_set(&spec, &pm, 40);

  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(mask, 7, UNIVERSAL_DECK_STANDARD));
  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(mask, 40, UNIVERSAL_DECK_STANDARD));
  TEST_ASSERT_FALSE(
      Universal_CardMask_CARD_IS_SET(mask, 8, UNIVERSAL_DECK_STANDARD));
  TEST_ASSERT_EQUAL_UINT64(pm, mask.cards);
  TEST_ASSERT_EQUAL_INT(
      pe_deck_mask_count(&spec, pm),
      Universal_numCards(mask, UNIVERSAL_DECK_STANDARD));
}

/* Same delegation consistency check for the joker deck, including the joker
 * card index (card 52). */
static void test_delegation_joker_mask(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t pm = 0;
  UniversalCardMask mask;

  TEST_ASSERT_EQUAL_INT(0,
                        pe_deck_get_predefined(PE_DECK_PRESET_JOKER_53, &spec));
  Universal_CardMask_RESET(&mask, UNIVERSAL_DECK_JOKER);
  Universal_CardMask_SET(&mask, 10, UNIVERSAL_DECK_JOKER);
  Universal_CardMask_SET(&mask, 52, UNIVERSAL_DECK_JOKER);

  pe_deck_mask_set(&spec, &pm, 10);
  pe_deck_mask_set(&spec, &pm, 52);

  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(mask, 10, UNIVERSAL_DECK_JOKER));
  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(mask, 52, UNIVERSAL_DECK_JOKER));
  TEST_ASSERT_EQUAL_UINT64(pm, mask.cards);
  TEST_ASSERT_EQUAL_INT(
      pe_deck_mask_count(&spec, pm),
      Universal_numCards(mask, UNIVERSAL_DECK_JOKER));
}

/* Universal_CardMask_OR matches the bitwise OR of the generalized bits. */
static void test_delegation_or(void) {
  UniversalCardMask a, b, r;
  Universal_CardMask_RESET(&a, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_RESET(&b, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_SET(&a, 1, UNIVERSAL_DECK_STANDARD);
  Universal_CardMask_SET(&b, 44, UNIVERSAL_DECK_STANDARD);

  Universal_CardMask_OR(&r, a, b, UNIVERSAL_DECK_STANDARD);
  TEST_ASSERT_TRUE(Universal_CardMask_CARD_IS_SET(r, 1, UNIVERSAL_DECK_STANDARD));
  TEST_ASSERT_TRUE(
      Universal_CardMask_CARD_IS_SET(r, 44, UNIVERSAL_DECK_STANDARD));
  TEST_ASSERT_EQUAL_UINT64(a.cards | b.cards, r.cards);
}

/* Universal_* string conversions agree with the generalized parsers. */
static void test_delegation_string(void) {
  pe_deck_spec_t spec;
  char ubuf[16], pbuf[16];
  int u_card, p_card;
  deck_type_t u_type;

  TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec));

  TEST_ASSERT_TRUE(Universal_StringToCard("Ks", &u_card, &u_type) > 0);
  TEST_ASSERT_EQUAL_INT(UNIVERSAL_DECK_STANDARD, u_type);
  TEST_ASSERT_TRUE(pe_deck_string_to_card(&spec, "Ks", &p_card) > 0);
  TEST_ASSERT_EQUAL_INT(p_card, u_card);

  TEST_ASSERT_TRUE(
      Universal_CardToString(u_card, ubuf, UNIVERSAL_DECK_STANDARD) > 0);
  TEST_ASSERT_TRUE(
      pe_deck_card_to_string(&spec, u_card, pbuf, sizeof(pbuf)) > 0);
  TEST_ASSERT_EQUAL_STRING(pbuf, ubuf);
}

int main(void) {
  UNITY_BEGIN();

  /* CardMask operations - standard */
  RUN_TEST(test_reset_std_deck);
  RUN_TEST(test_set_std_deck);
  RUN_TEST(test_or_std_deck);
  RUN_TEST(test_num_cards_std);

  /* CardMask operations - joker */
  RUN_TEST(test_reset_joker_deck);
  RUN_TEST(test_set_joker_deck);
  RUN_TEST(test_or_joker_deck);
  RUN_TEST(test_num_cards_joker);

  /* String parsing */
  RUN_TEST(test_string_to_card_std);
  RUN_TEST(test_string_to_card_joker);
  RUN_TEST(test_card_to_string_std);
  RUN_TEST(test_card_to_string_joker);

  /* Game type detection */
  RUN_TEST(test_deck_type_std_games);
  RUN_TEST(test_deck_type_joker_games);
  RUN_TEST(test_is_joker_game);

  /* Conversion functions */
  RUN_TEST(test_convert_std_to_joker);
  RUN_TEST(test_convert_joker_to_std);

  /* ISSUE-03 phase 2 (#203): delegation into the generalized deck layer */
  RUN_TEST(test_delegation_std_mask);
  RUN_TEST(test_delegation_joker_mask);
  RUN_TEST(test_delegation_or);
  RUN_TEST(test_delegation_string);

  return UNITY_END();
}

