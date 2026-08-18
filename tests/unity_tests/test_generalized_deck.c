/**
 * test_generalized_deck.c
 *
 * ISSUE-03 (#159): unit tests for the generalized deck abstraction
 * (pe_deck_spec_t / pe_card_mask_t). Covers the 7 predefined presets, bitmask
 * operations across deck sizes in [20..64], custom deck creation, and string
 * round-tripping.
 */

#include "unity.h"
#include <poker_eval/core/universal_deck.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/generalized_deck.h>

void setUp(void) {}
void tearDown(void) {}

/* Expected num_cards for the 7 presets, in preset-table order. */
static const int expected_cards[] = {20, 32, 36, 52, 53, 54, 60};
static const int expected_ranks[] = {5, 8, 9, 13, 13, 13, 13};
static const int expected_suits[] = {4, 4, 4, 4, 4, 4, 4};
static const int expected_jokers[] = {0, 0, 0, 0, 1, 2, 8};
static const uint16_t expected_rank_mask[] = {
    0x1F00, /* royal:  T..A  */
    0x1FE0, /* spanish: 7..A */
    0x1FF0, /* short:  6..A */
    0x1FFF, /* std     */
    0x1FFF, /* joker 53 */
    0x1FFF, /* joker 54 */
    0x1FFF, /* california */
};

static const char *preset_names[] = {
    PE_DECK_PRESET_ROYAL, PE_DECK_PRESET_SPANISH, PE_DECK_PRESET_SHORT,
    PE_DECK_PRESET_STD, PE_DECK_PRESET_JOKER_53, PE_DECK_PRESET_JOKER_54,
    PE_DECK_PRESET_CALIFORNIA};

/* Acceptance: every preset initializes with the expected geometry. */
static void test_all_presets_initialize(void) {
  size_t i;
  for (i = 0; i < 7; i++) {
    pe_deck_spec_t spec;
    TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(preset_names[i], &spec));
    TEST_ASSERT_EQUAL_INT(expected_cards[i], spec.num_cards);
    TEST_ASSERT_EQUAL_INT(expected_ranks[i], spec.num_ranks);
    TEST_ASSERT_EQUAL_INT(expected_suits[i], spec.num_suits);
    TEST_ASSERT_EQUAL_INT(expected_jokers[i], spec.num_jokers);
    TEST_ASSERT_EQUAL_UINT16(expected_rank_mask[i], spec.active_rank_mask);
    TEST_ASSERT_EQUAL_STRING(preset_names[i], spec.deck_name);
  }
}

/* Each preset's full mask holds exactly num_cards cards. */
static void test_preset_mask_full_count(void) {
  size_t i;
  for (i = 0; i < 7; i++) {
    pe_deck_spec_t spec;
    pe_card_mask_t full;
    TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(preset_names[i], &spec));
    full = pe_deck_mask_full(&spec);
    TEST_ASSERT_EQUAL_INT(expected_cards[i], pe_deck_mask_count(&spec, full));
  }
}

/* Unknown preset names are rejected. */
static void test_unknown_preset_rejected(void) {
  pe_deck_spec_t spec;
  TEST_ASSERT_EQUAL_INT(-1, pe_deck_get_predefined("not_a_deck", &spec));
  TEST_ASSERT_EQUAL_INT(-1, pe_deck_get_predefined(NULL, &spec));
}

/* String round-trip: "As" parses to the top spade and renders back. */
static void test_std_string_roundtrip(void) {
  pe_deck_spec_t spec;
  int card;
  char buf[4];
  TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec));

  TEST_ASSERT_EQUAL_INT(2, pe_deck_string_to_card(&spec, "As", &card));
  TEST_ASSERT_EQUAL_INT(StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
                        card);

  card = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  TEST_ASSERT_EQUAL_INT(2, pe_deck_card_to_string(&spec, card, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("As", buf);
}

/* The generalized std_52 spec is consistent with the StdDeck index layout. */
static void test_std_mask_matches_stddeck(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t mask = 0;
  int i;
  TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec));

  /* Each StdDeck card index maps to the same generalized index. */
  for (i = 0; i < StdDeck_N_CARDS; i++) {
    int parsed;
    char buf[4];
    TEST_ASSERT_EQUAL_INT(2,
                          pe_deck_card_to_string(&spec, i, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(2, pe_deck_string_to_card(&spec, buf, &parsed));
    TEST_ASSERT_EQUAL_INT(i, parsed);
  }

  /* Set the same ace-of-spades in both representations. */
  pe_deck_mask_set(&spec, &mask, StdDeck_MAKE_CARD(StdDeck_Rank_ACE,
                                                   StdDeck_Suit_SPADES));
  TEST_ASSERT_TRUE(pe_deck_mask_is_set(&spec, mask,
                                       StdDeck_MAKE_CARD(StdDeck_Rank_ACE,
                                                         StdDeck_Suit_SPADES)));
  TEST_ASSERT_EQUAL_INT(1, pe_deck_mask_count(&spec, mask));
}

/* Acceptance: bitmask set/unset/count work for any deck size (range spots). */
static void test_mask_ops_std52(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t mask = 0;
  TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec));

  pe_deck_mask_set(&spec, &mask, 0);
  pe_deck_mask_set(&spec, &mask, 25);
  pe_deck_mask_set(&spec, &mask, 51);
  TEST_ASSERT_TRUE(pe_deck_mask_is_set(&spec, mask, 0));
  TEST_ASSERT_TRUE(pe_deck_mask_is_set(&spec, mask, 25));
  TEST_ASSERT_TRUE(pe_deck_mask_is_set(&spec, mask, 51));
  TEST_ASSERT_FALSE(pe_deck_mask_is_set(&spec, mask, 26));
  TEST_ASSERT_EQUAL_INT(3, pe_deck_mask_count(&spec, mask));

  pe_deck_mask_unset(&spec, &mask, 25);
  TEST_ASSERT_FALSE(pe_deck_mask_is_set(&spec, mask, 25));
  TEST_ASSERT_EQUAL_INT(2, pe_deck_mask_count(&spec, mask));
}

/* Jokers and extra cards occupy the high indices and render as "Xx". */
static void test_mask_ops_joker54(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t mask = 0;
  char buf[4];
  int card;
  TEST_ASSERT_EQUAL_INT(0,
                        pe_deck_get_predefined(PE_DECK_PRESET_JOKER_54, &spec));

  /* Joker indices are 52 and 53. */
  pe_deck_mask_set(&spec, &mask, 52);
  pe_deck_mask_set(&spec, &mask, 53);
  TEST_ASSERT_EQUAL_INT(2, pe_deck_mask_count(&spec, mask));

  TEST_ASSERT_EQUAL_INT(2, pe_deck_string_to_card(&spec, "Xx", &card));
  TEST_ASSERT_EQUAL_INT(52, card);
  TEST_ASSERT_EQUAL_INT(2, pe_deck_card_to_string(&spec, 52, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Xx", buf);
}

/* California (60 cards) uses 8 extra cards that are all masked and counted. */
static void test_mask_ops_california60(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t mask = 0;
  int j;
  TEST_ASSERT_EQUAL_INT(
      0, pe_deck_get_predefined(PE_DECK_PRESET_CALIFORNIA, &spec));

  for (j = 52; j < 60; j++) {
    pe_deck_mask_set(&spec, &mask, j);
  }
  TEST_ASSERT_EQUAL_INT(8, pe_deck_mask_count(&spec, mask));
  TEST_ASSERT_EQUAL_INT(2, pe_deck_string_to_card(&spec, "Xx", &j));
  TEST_ASSERT_EQUAL_INT(52, j);
  pe_deck_mask_unset(&spec, &mask, 52);
  TEST_ASSERT_EQUAL_INT(7, pe_deck_mask_count(&spec, mask));
}

/* Out-of-range card indices are safely ignored by mask operations. */
static void test_mask_out_of_range_safe(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t mask = 0;
  TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec));

  pe_deck_mask_set(&spec, &mask, -1);
  pe_deck_mask_set(&spec, &mask, 52); /* == num_cards */
  pe_deck_mask_unset(&spec, &mask, 100);
  TEST_ASSERT_EQUAL_INT(0, pe_deck_mask_count(&spec, mask));
  TEST_ASSERT_EQUAL_INT(0, pe_deck_mask_is_set(&spec, mask, 52));
  TEST_ASSERT_EQUAL_INT(0, pe_deck_mask_is_set(&spec, mask, -1));
  TEST_ASSERT_EQUAL_INT(-1, pe_deck_card_to_string(&spec, 52, (char[4]){0}, 4));
}

/* pe_deck_mask_to_string produces space-separated card names in mask order. */
static void test_mask_to_string(void) {
  pe_deck_spec_t spec;
  pe_card_mask_t mask = 0;
  char buf[128];
  TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec));

  pe_deck_mask_set(&spec, &mask, 0);  /* 2h */
  pe_deck_mask_set(&spec, &mask, 51); /* As */
  TEST_ASSERT_TRUE(
      pe_deck_mask_to_string(&spec, mask, buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("2h As", buf);

  /* An empty mask renders an empty string and returns 0. */
  mask = 0;
  TEST_ASSERT_EQUAL_INT(
      0, pe_deck_mask_to_string(&spec, mask, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

/* Custom decks: valid construction, geometry, and invalid-argument rejection. */
static void test_custom_deck(void) {
  pe_deck_spec_t spec;

  /* T..A x4 suits, no jokers -> Royal-size custom deck. */
  TEST_ASSERT_EQUAL_INT(0,
                        pe_deck_create_custom(8, 12, 4, 0, &spec));
  TEST_ASSERT_EQUAL_INT(20, spec.num_cards);
  TEST_ASSERT_EQUAL_INT(5, spec.num_ranks);
  TEST_ASSERT_EQUAL_INT(4, spec.num_suits);
  TEST_ASSERT_EQUAL_INT(0, spec.num_jokers);
  TEST_ASSERT_EQUAL_UINT16(0x1F00, spec.active_rank_mask);
  TEST_ASSERT_EQUAL_STRING("custom", spec.deck_name);

  /* Maximal 64-card deck: 13 ranks x4 suits + 12 extra cards. */
  TEST_ASSERT_EQUAL_INT(0,
                        pe_deck_create_custom(0, 12, 4, 12, &spec));
  TEST_ASSERT_EQUAL_INT(64, spec.num_cards);
  TEST_ASSERT_EQUAL_INT(64, pe_deck_mask_count(&spec, pe_deck_mask_full(&spec)));

  /* Invalid arguments are rejected. */
  TEST_ASSERT_EQUAL_INT(-1, pe_deck_create_custom(3, 2, 4, 0, &spec));
  TEST_ASSERT_EQUAL_INT(-1, pe_deck_create_custom(0, 12, 0, 0, &spec));
  TEST_ASSERT_EQUAL_INT(-1, pe_deck_create_custom(0, 12, 4, -1, &spec));
  TEST_ASSERT_EQUAL_INT(-1, pe_deck_create_custom(0, 13, 4, 0, &spec));
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_all_presets_initialize);
  RUN_TEST(test_preset_mask_full_count);
  RUN_TEST(test_unknown_preset_rejected);
  RUN_TEST(test_std_string_roundtrip);
  RUN_TEST(test_std_mask_matches_stddeck);
  RUN_TEST(test_mask_ops_std52);
  RUN_TEST(test_mask_ops_joker54);
  RUN_TEST(test_mask_ops_california60);
  RUN_TEST(test_mask_out_of_range_safe);
  RUN_TEST(test_mask_to_string);
  RUN_TEST(test_custom_deck);

  return UNITY_END();
}
