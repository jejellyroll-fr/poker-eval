/**
 * test_wildcard_policy.c
 *
 * ISSUE-05 (#161): unit tests for the generalized wildcard policy
 * (pe_wildcard_policy_t / pe_eval_wildcard_hand). Covers Deuces Wild
 * (5-of-a-Kind, Wild Royal, Straight Flush), Double Joker, the Bug rule
 * (no 5-of-a-Kind, Ace / filler roles) and 5-of-a-Kind recognition.
 */

#include "unity.h"
#include <poker_eval/games/wildcard_policy.h>
#include <poker_eval/deck/generalized_deck.h>

/* pe_ranking_category_rank() returns the category's *index* in the configured
 * ordering (0 = weakest ... highest = strongest), not the pe_hand_category_t
 * enum value. The policy uses the standard ordering (optionally with
 * Five-of-a-Kind appended on top), so these constants are the expected ranks. */
#define IDX_FIVE_KIND        9
#define IDX_STRAIGHT_FLUSH   8
#define IDX_QUADS            7
#define IDX_FLUSH            5
#define IDX_STRAIGHT         4

static pe_deck_spec_t std;   /* 52-card standard deck (Deuces Wild) */
static pe_deck_spec_t dj54;  /* 54-card deck with two jokers          */
static pe_deck_spec_t bj53;  /* 53-card deck with one joker (Bug)     */

/* Build a mask from an array of card strings (NULL-terminated is not required;
 * n cards are taken). Jokers are denoted "Xx". */
static pe_card_mask_t make_hand(const pe_deck_spec_t *spec, const char **cards, int n) {
    pe_card_mask_t m = 0;
    int i, c;
    TEST_ASSERT_NOT_NULL(spec);
    for (i = 0; i < n; i++) {
        int rc = pe_deck_string_to_card(spec, cards[i], &c);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, rc, "string_to_card failed");
        pe_deck_mask_set(spec, &m, c);
    }
    return m;
}

/* Manually add the i-th joker (index = suits*ranks + i) to a mask. */
static void add_joker(const pe_deck_spec_t *spec, pe_card_mask_t *m, int i) {
    int j = spec->num_suits * spec->num_ranks + i;
    TEST_ASSERT_TRUE(j < spec->num_cards);
    pe_deck_mask_set(spec, m, j);
}

void setUp(void) {
    TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &std));
    TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_JOKER_54, &dj54));
    TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_JOKER_53, &bj53));
}

void tearDown(void) {}

/* pe_wildcard_policy_init configures the documented defaults. */
static void test_policy_init(void) {
    pe_wildcard_policy_t p;
    TEST_ASSERT_EQUAL_INT(0, pe_wildcard_policy_init(
        PE_WILD_BEHAVIOR_FULLY_WILD, PE_WILD_DEUCES, 0, &p));
    TEST_ASSERT_EQUAL_INT(PE_WILD_BEHAVIOR_FULLY_WILD, p.behavior);
    TEST_ASSERT_EQUAL_INT(PE_WILD_DEUCES, p.wild_rank_mask);
    TEST_ASSERT_EQUAL_INT(1, p.allow_five_of_a_kind);

    TEST_ASSERT_EQUAL_INT(0, pe_wildcard_policy_init(
        PE_WILD_BEHAVIOR_BUG_RULE, 0, 1, &p));
    TEST_ASSERT_EQUAL_INT(PE_WILD_BEHAVIOR_BUG_RULE, p.behavior);
    TEST_ASSERT_EQUAL_INT(0, p.allow_five_of_a_kind);

    TEST_ASSERT_EQUAL_INT(-1, pe_wildcard_policy_init(
        (pe_wild_behavior_t)99, 0, 0, &p));
}

/* A non-wild hand with no deuces evaluates to its natural category. */
static void test_no_wildcards(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"Th", "Jh", "Qh", "Kh", "Ah"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_FULLY_WILD, PE_WILD_DEUCES, 0, &p);
    h = make_hand(&std, cards, 5);
    HandVal v = pe_eval_wildcard_hand(&std, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_STRAIGHT_FLUSH, pe_ranking_category_rank(v));
}

/* Deuces Wild: 4 deuces + an Ace -> Five of a Kind (Aces). */
static void test_deuces_five_of_a_kind(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"Ah", "2c", "2d", "2h", "2s"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_FULLY_WILD, PE_WILD_DEUCES, 0, &p);
    h = make_hand(&std, cards, 5);
    HandVal v = pe_eval_wildcard_hand(&std, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_FIVE_KIND, pe_ranking_category_rank(v));
}

/* Deuces Wild: four hearts to a royal + a deuce -> Wild Royal (Straight Flush). */
static void test_deuces_wild_royal(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"Th", "Jh", "Qh", "Kh", "2d"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_FULLY_WILD, PE_WILD_DEUCES, 0, &p);
    h = make_hand(&std, cards, 5);
    HandVal v = pe_eval_wildcard_hand(&std, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_STRAIGHT_FLUSH, pe_ranking_category_rank(v));
}

/* Deuces Wild: 3d 4d 5d 6d + a deuce -> Straight Flush. */
static void test_deuces_straight_flush(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"2d", "3d", "4d", "5d", "6d"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_FULLY_WILD, PE_WILD_DEUCES, 0, &p);
    /* The first card is itself a deuce (wild); make sure it still works. */
    h = make_hand(&std, cards, 5);
    HandVal v = pe_eval_wildcard_hand(&std, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_STRAIGHT_FLUSH, pe_ranking_category_rank(v));
}

/* Double Joker: 3 Kings + 2 jokers -> Five of a Kind. */
static void test_double_joker_five_of_a_kind(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"Kh", "Kd", "Ks"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_FULLY_WILD, 0, 2, &p);
    h = make_hand(&dj54, cards, 3);
    add_joker(&dj54, &h, 0);
    add_joker(&dj54, &h, 1);
    HandVal v = pe_eval_wildcard_hand(&dj54, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_FIVE_KIND, pe_ranking_category_rank(v));
}

/* Double Joker: T J Q K + 2 jokers -> Straight (9 T J Q K A). */
static void test_double_joker_straight(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"Tc", "Jd", "Qh", "Ks"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_FULLY_WILD, 0, 2, &p);
    h = make_hand(&dj54, cards, 4);
    add_joker(&dj54, &h, 0);
    add_joker(&dj54, &h, 1);
    HandVal v = pe_eval_wildcard_hand(&dj54, &p, h, 6);
    TEST_ASSERT_EQUAL_INT(IDX_STRAIGHT, pe_ranking_category_rank(v));
}

/* Bug rule: 4 Kings + bug joker must NOT form 5-of-a-Kind; best is Quads. */
static void test_bug_no_five_of_a_kind(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"Kh", "Kd", "Ks", "Kc"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_BUG_RULE, 0, 1, &p);
    h = make_hand(&bj53, cards, 4);
    add_joker(&bj53, &h, 0);
    HandVal v = pe_eval_wildcard_hand(&bj53, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_QUADS, pe_ranking_category_rank(v));
}

/* Bug rule: Ks Qs Js Ts + bug joker acts as Ace -> Royal (Straight Flush). */
static void test_bug_as_ace_royal(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"Ks", "Qs", "Js", "Ts"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_BUG_RULE, 0, 1, &p);
    h = make_hand(&bj53, cards, 4);
    add_joker(&bj53, &h, 0);
    HandVal v = pe_eval_wildcard_hand(&bj53, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_STRAIGHT_FLUSH, pe_ranking_category_rank(v));
}

/* Bug rule: 3h 4h 5h 6h + bug joker fills the straight (as a 2 or 7). */
static void test_bug_filler_straight(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"3h", "4d", "5s", "6c"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_BUG_RULE, 0, 1, &p);
    h = make_hand(&bj53, cards, 4);
    add_joker(&bj53, &h, 0);
    HandVal v = pe_eval_wildcard_hand(&bj53, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_STRAIGHT, pe_ranking_category_rank(v));
}

/* Bug rule: 4 spades + bug joker fills the flush. */
static void test_bug_filler_flush(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t h;
    const char *cards[] = {"2s", "5s", "8s", "Ts"};
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_BUG_RULE, 0, 1, &p);
    h = make_hand(&bj53, cards, 4);
    add_joker(&bj53, &h, 0);
    HandVal v = pe_eval_wildcard_hand(&bj53, &p, h, 5);
    TEST_ASSERT_EQUAL_INT(IDX_FLUSH, pe_ranking_category_rank(v));
}

/* Invalid input is rejected gracefully. */
static void test_invalid_input(void) {
    pe_wildcard_policy_t p;
    pe_card_mask_t empty = 0;
    pe_wildcard_policy_init(PE_WILD_BEHAVIOR_FULLY_WILD, PE_WILD_DEUCES, 0, &p);
    TEST_ASSERT_EQUAL_INT(HandVal_NOTHING,
                          pe_eval_wildcard_hand(NULL, &p, empty, 5));
    TEST_ASSERT_EQUAL_INT(HandVal_NOTHING,
                          pe_eval_wildcard_hand(&std, NULL, empty, 5));
    TEST_ASSERT_EQUAL_INT(HandVal_NOTHING,
                          pe_eval_wildcard_hand(&std, &p, empty, 5));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_policy_init);
    RUN_TEST(test_no_wildcards);
    RUN_TEST(test_deuces_five_of_a_kind);
    RUN_TEST(test_deuces_wild_royal);
    RUN_TEST(test_deuces_straight_flush);
    RUN_TEST(test_double_joker_five_of_a_kind);
    RUN_TEST(test_double_joker_straight);
    RUN_TEST(test_bug_no_five_of_a_kind);
    RUN_TEST(test_bug_as_ace_royal);
    RUN_TEST(test_bug_filler_straight);
    RUN_TEST(test_bug_filler_flush);
    RUN_TEST(test_invalid_input);
    return UNITY_END();
}
