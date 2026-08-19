/**
 * test_hand_distribution.c
 *
 * ISSUE-02 (#158): exact combinatorial hand distribution engine built on the
 * generalized deck (ISSUE-03 #159) and configurable ranking (ISSUE-04 #160)
 * infrastructure. Validates the canonical 5-card distributions for the
 * 52-card standard deck, the 36-card Short Deck, and the 20-card Royal deck,
 * plus the game-based convenience wrapper (Short Deck hold'em).
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/generalized_deck.h>
#include <poker_eval/core/configurable_ranking.h>
#include <poker_eval/distributions/hand_distribution.h>

/* Look up a category's count by id within a computed distribution. */
static uint64_t count_of(const pe_hand_distribution_t *d, int category_id) {
    int i;
    for (i = 0; i < d->num_categories; i++) {
        if (d->categories[i].category_id == category_id) {
            return d->categories[i].count;
        }
    }
    return 0;
}

static void assert_total(const pe_hand_distribution_t *d, uint64_t total) {
    TEST_ASSERT_EQUAL_UINT64(total, d->total_combinations);
    uint64_t sum = 0;
    for (int i = 0; i < d->num_categories; i++) {
        sum += d->categories[i].count;
    }
    TEST_ASSERT_EQUAL_UINT64(total, sum);
}

/* Standard 52-card deck, 5-card hands, standard ranking. */
static void test_std_52_distribution(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution_for_preset(
        PE_DECK_PRESET_STD, "standard", 5, &dist));

    TEST_ASSERT_EQUAL_INT(52, dist.deck_size);
    TEST_ASSERT_EQUAL_INT(5, dist.hand_size);
    assert_total(&dist, 2598960ULL);

    /* Canonical Bollman counts for the standard 5-card poker hands. */
    TEST_ASSERT_EQUAL_UINT64(40ULL,    count_of(&dist, PE_CAT_STRAIGHT_FLUSH));
    TEST_ASSERT_EQUAL_UINT64(624ULL,   count_of(&dist, PE_CAT_FOUR_OF_A_KIND));
    TEST_ASSERT_EQUAL_UINT64(3744ULL,  count_of(&dist, PE_CAT_FULL_HOUSE));
    TEST_ASSERT_EQUAL_UINT64(5108ULL,  count_of(&dist, PE_CAT_FLUSH));
    TEST_ASSERT_EQUAL_UINT64(10200ULL, count_of(&dist, PE_CAT_STRAIGHT));
    TEST_ASSERT_EQUAL_UINT64(54912ULL, count_of(&dist, PE_CAT_THREE_OF_A_KIND));
    TEST_ASSERT_EQUAL_UINT64(123552ULL,count_of(&dist, PE_CAT_TWO_PAIR));
    TEST_ASSERT_EQUAL_UINT64(1098240ULL,count_of(&dist, PE_CAT_ONE_PAIR));
    TEST_ASSERT_EQUAL_UINT64(1302540ULL,count_of(&dist, PE_CAT_HIGH_CARD));
}

/* 36-card Short Deck, 5-card hands, Short Deck (flush > full house) ranking. */
static void test_short_36_distribution(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution_for_preset(
        PE_DECK_PRESET_SHORT, "short_deck", 5, &dist));

    TEST_ASSERT_EQUAL_INT(36, dist.deck_size);
    assert_total(&dist, 376992ULL);

    /* Exact combinatorial counts for the 36-card (6..A) Short Deck. The issue's
     * section-2 figures (Flush = 504, Full House = 1440) count flushes inclusive
     * of straight flushes and do not match exact combinatorics; the engine
     * reports the standard convention (straight flushes excluded, as for the
     * 52-card 5108 figure), so Flush = C(9,5)*4 - 20 = 484 and
     * Full House = 9 * C(4,3) * 8 * C(4,2) = 1728. */
    TEST_ASSERT_EQUAL_UINT64(20ULL,   count_of(&dist, PE_CAT_STRAIGHT_FLUSH));
    TEST_ASSERT_EQUAL_UINT64(484ULL,  count_of(&dist, PE_CAT_FLUSH));
    TEST_ASSERT_EQUAL_UINT64(1728ULL, count_of(&dist, PE_CAT_FULL_HOUSE));

    /* Flush now outranks Full House in the Short Deck ordering. */
    int flush_rank = -1, fh_rank = -1;
    for (int i = 0; i < dist.num_categories; i++) {
        if (dist.categories[i].category_id == PE_CAT_FLUSH) flush_rank = i;
        if (dist.categories[i].category_id == PE_CAT_FULL_HOUSE) fh_rank = i;
    }
    TEST_ASSERT_TRUE(flush_rank >= 0 && fh_rank >= 0);
    TEST_ASSERT_TRUE(flush_rank < fh_rank); /* smaller index == stronger */
}

/* 20-card Royal deck (T..A), 5-card hands, standard ranking. */
static void test_royal_20_distribution(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution_for_preset(
        PE_DECK_PRESET_ROYAL, "standard", 5, &dist));

    TEST_ASSERT_EQUAL_INT(20, dist.deck_size);
    assert_total(&dist, 15504ULL);

    /* 20-card Royal deck (T..A): only the Broadway straight exists. 4 straight
     * flushes (one per suit) and 4^5 - 4 = 1020 non-flush straights; no
     * non-straight-flush flush is possible with only 5 ranks. */
    TEST_ASSERT_EQUAL_UINT64(4ULL,   count_of(&dist, PE_CAT_STRAIGHT_FLUSH));
    TEST_ASSERT_EQUAL_UINT64(1020ULL, count_of(&dist, PE_CAT_STRAIGHT));
    TEST_ASSERT_EQUAL_UINT64(0ULL,   count_of(&dist, PE_CAT_FLUSH));
}

/* Game-based convenience wrapper: Short Deck hold'em. */
static void test_game_wrapper_sdholdem(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution(game_sdholdem, &dist));
    TEST_ASSERT_EQUAL_INT(game_sdholdem, dist.game);
    assert_total(&dist, 376992ULL);
    TEST_ASSERT_EQUAL_UINT64(484ULL,  count_of(&dist, PE_CAT_FLUSH));
    TEST_ASSERT_EQUAL_UINT64(1728ULL, count_of(&dist, PE_CAT_FULL_HOUSE));
}

/* Game-based convenience wrapper: a standard-deck game maps to the std table. */
static void test_game_wrapper_standard(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution(game_holdem, &dist));
    TEST_ASSERT_EQUAL_INT(game_holdem, dist.game);
    assert_total(&dist, 2598960ULL);
    TEST_ASSERT_EQUAL_UINT64(40ULL, count_of(&dist, PE_CAT_STRAIGHT_FLUSH));
    TEST_ASSERT_EQUAL_UINT64(624ULL, count_of(&dist, PE_CAT_FOUR_OF_A_KIND));
}

/* Unsupported game returns failure. */
static void test_game_wrapper_unsupported(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(-1, pe_compute_hand_distribution((enum_game_t)-1, &dist));
}

/* Unknown preset returns failure. */
static void test_preset_unknown(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(-1, pe_compute_hand_distribution_for_preset(
        "no_such_deck", "standard", 5, &dist));
    TEST_ASSERT_EQUAL_INT(-1, pe_compute_hand_distribution_for_preset(
        PE_DECK_PRESET_STD, "no_such_ranking", 5, &dist));
}

/* Probability sums to ~1.0 across categories. */
static void test_probabilities_sum_to_one(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution_for_preset(
        PE_DECK_PRESET_ROYAL, "standard", 5, &dist));
    double sum = 0.0;
    for (int i = 0; i < dist.num_categories; i++) {
        sum += dist.categories[i].probability;
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, sum);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0,
        dist.categories[dist.num_categories - 1].cumulative_probability);
}

/* Markdown and JSON formatting helpers run without error. */
static void test_print_helpers(void) {
    pe_hand_distribution_t dist;
    TEST_ASSERT_EQUAL_INT(0, pe_compute_hand_distribution_for_preset(
        PE_DECK_PRESET_ROYAL, "standard", 5, &dist));
    TEST_ASSERT_EQUAL_INT(0,
        pe_hand_distribution_print_markdown(&dist, stdout));
    TEST_ASSERT_EQUAL_INT(0,
        pe_hand_distribution_print_json(&dist, stdout));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_std_52_distribution);
    RUN_TEST(test_short_36_distribution);
    RUN_TEST(test_royal_20_distribution);
    RUN_TEST(test_game_wrapper_sdholdem);
    RUN_TEST(test_game_wrapper_standard);
    RUN_TEST(test_game_wrapper_unsupported);
    RUN_TEST(test_preset_unknown);
    RUN_TEST(test_probabilities_sum_to_one);
    RUN_TEST(test_print_helpers);
    return UNITY_END();
}
