/**
 * test_configurable_ranking.c
 *
 * ISSUE-04 (#160): unit tests for the configurable hand ranking system
 * (pe_hand_ranking_config_t / pe_eval_configurable_hand). Covers preset
 * initialization, the 4-card category detectors and the official rules:
 *   - Sökö / Canadian Stud: Straight > Flush > Full House > Trips >
 *     Two Pair > 4-Card Flush > 4-Card Straight > One Pair > High Card
 *   - Four-card flushes and four-card straights tie on their own ranks only;
 *     the off-suit fifth card is not a tie-breaker.
 *   - Italian / Manila: Flush > Full House
 *   - Normalized HandVal: a larger value always means a stronger hand.
 */

#include "unity.h"
#include <poker_eval/core/configurable_ranking.h>
#include <poker_eval/deck/generalized_deck.h>

static pe_deck_spec_t std;

static pe_card_mask_t make_hand(const char **cards, int n) {
    pe_card_mask_t m = 0;
    int i;
    for (i = 0; i < n; i++) {
        int c = 0;
        int rc = pe_deck_string_to_card(&std, cards[i], &c);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, rc, "string_to_card failed");
        pe_deck_mask_set(&std, &m, c);
    }
    return m;
}

void setUp(void) {
    TEST_ASSERT_EQUAL_INT(0, pe_deck_get_predefined(PE_DECK_PRESET_STD, &std));
}

void tearDown(void) {}

/* All five presets initialize without error and activate the 4-card flags
 * only for the stud variants. */
static void test_presets_initialize(void) {
    pe_hand_ranking_config_t cfg;
    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("standard", &cfg));
    TEST_ASSERT_EQUAL_INT(0, cfg.allow_4card_straights);
    TEST_ASSERT_EQUAL_INT(0, cfg.allow_4card_flushes);
    TEST_ASSERT_EQUAL_INT(0, cfg.flush_beats_fullhouse);

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("short_deck", &cfg));
    TEST_ASSERT_EQUAL_INT(1, cfg.flush_beats_fullhouse);

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("italian_manila", &cfg));
    TEST_ASSERT_EQUAL_INT(1, cfg.flush_beats_fullhouse);
    TEST_ASSERT_EQUAL_INT(0, cfg.ace_low_straight_allowed);

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("canadian_stud", &cfg));
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_straights);
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_flushes);

    /* Sökö and Canadian Stud are the same official rules, so every alias
     * resolves to the identical ordering. */
    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("soko", &cfg));
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_straights);
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_flushes);
    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("s\xc3\xb6k\xc3\xb6", &cfg));
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_straights);
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_flushes);

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("new_york_stud", &cfg));
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_straights);
    TEST_ASSERT_EQUAL_INT(1, cfg.allow_4card_flushes);

    TEST_ASSERT_EQUAL_INT(-1, pe_ranking_config_set_preset("no_such_preset", &cfg));
}

/* Standard ranking: Full House beats Flush. */
static void test_standard_fullhouse_beats_flush(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t fh, fl;
    const char *fh_c[] = {"Ah", "Ad", "Ac", "Ks", "Kd"};
    const char *fl_c[] = {"Ah", "Jh", "8h", "5h", "2h"};
    pe_ranking_config_set_preset("standard", &cfg);
    fh = make_hand(fh_c, 5);
    fl = make_hand(fl_c, 5);
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, fh, 5) >
                     pe_eval_configurable_hand(&std, &cfg, fl, 5));
}

/* Acceptance #2: Italian / Manila ranking: Flush beats Full House. */
static void test_italian_manila_flush_beats_fullhouse(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t fh, fl;
    const char *fh_c[] = {"Ah", "Ad", "Ac", "Ks", "Kd"};
    const char *fl_c[] = {"Ah", "Jh", "8h", "5h", "2h"};
    pe_ranking_config_set_preset("italian_manila", &cfg);
    fh = make_hand(fh_c, 5);
    fl = make_hand(fl_c, 5);
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, fl, 5) >
                     pe_eval_configurable_hand(&std, &cfg, fh, 5));
}

/* Same Flush > Full House relationship for Short Deck. */
static void test_short_deck_flush_beats_fullhouse(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t fh, fl;
    const char *fh_c[] = {"Ah", "Ad", "Ac", "Ks", "Kd"};
    const char *fl_c[] = {"Ah", "Jh", "8h", "5h", "2h"};
    pe_ranking_config_set_preset("short_deck", &cfg);
    fh = make_hand(fh_c, 5);
    fl = make_hand(fl_c, 5);
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, fl, 5) >
                     pe_eval_configurable_hand(&std, &cfg, fh, 5));
}

/* Official Sökö (Canadian Stud) ordering from win2day §7.f.14:
 *   Straight > Flush > Full House > Trips > Two Pair > 4-Card Flush
 *   > 4-Card Straight > One Pair > High Card
 * The previously asserted order (4-card categories above trips) was wrong. */
static void test_soko_ordering(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t high_card, one_pair, four_straight, four_flush;
    pe_card_mask_t two_pair, trips, straight, flush, full_house, quads, st_flush;
    const char *high_card_c[]    = {"Ah", "Ks", "9d", "7c", "2h"};
    const char *one_pair_c[]     = {"Ah", "Ad", "Ks", "Qc", "2h"};
    const char *four_str_c[]     = {"9h", "Ts", "Jd", "Qc", "2h"};
    const char *four_flush_c[]   = {"9h", "Th", "Jh", "3h", "2s"};
    const char *two_pair_c[]     = {"Ah", "Ad", "Ks", "Kd", "2h"};
    const char *trips_c[]        = {"Ah", "Ad", "Ac", "Ks", "2d"};
    const char *straight_c[]     = {"Th", "Js", "Qs", "Ks", "Ah"};
    const char *flush_c[]        = {"Ah", "Jh", "8h", "5h", "2h"};
    const char *full_house_c[]   = {"Ah", "Ad", "Ac", "Ks", "Kd"};
    const char *quads_c[]        = {"Ah", "Ad", "Ac", "As", "2d"};
    const char *st_flush_c[]     = {"Th", "Jh", "Qh", "Kh", "Ah"};

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("soko", &cfg));
    high_card     = make_hand(high_card_c, 5);
    one_pair      = make_hand(one_pair_c, 5);
    four_straight = make_hand(four_str_c, 5);
    four_flush    = make_hand(four_flush_c, 5);
    two_pair      = make_hand(two_pair_c, 5);
    trips         = make_hand(trips_c, 5);
    straight      = make_hand(straight_c, 5);
    flush         = make_hand(flush_c, 5);
    full_house    = make_hand(full_house_c, 5);
    quads         = make_hand(quads_c, 5);
    st_flush      = make_hand(st_flush_c, 5);

    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, four_straight, 5) >
                     pe_eval_configurable_hand(&std, &cfg, one_pair, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, four_flush, 5) >
                     pe_eval_configurable_hand(&std, &cfg, four_straight, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, two_pair, 5) >
                     pe_eval_configurable_hand(&std, &cfg, four_flush, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, trips, 5) >
                     pe_eval_configurable_hand(&std, &cfg, two_pair, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, straight, 5) >
                     pe_eval_configurable_hand(&std, &cfg, trips, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, flush, 5) >
                     pe_eval_configurable_hand(&std, &cfg, straight, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, full_house, 5) >
                     pe_eval_configurable_hand(&std, &cfg, flush, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, quads, 5) >
                     pe_eval_configurable_hand(&std, &cfg, full_house, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, st_flush, 5) >
                     pe_eval_configurable_hand(&std, &cfg, quads, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, one_pair, 5) >
                     pe_eval_configurable_hand(&std, &cfg, high_card, 5));
}

/* Equal four-flush ranks tie even when the fifth, off-suit card differs
 * (win2day §7.f.14.1).  A stronger first differing suited rank still wins. */
static void test_soko_four_flush_tiebreaks(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t a, b, c, d;
    const char *a_c[] = {"Ah", "Jh", "8h", "5h", "2s"};
    const char *b_c[] = {"Ah", "Jh", "8h", "5h", "3d"}; /* only kicker differs */
    const char *c_c[] = {"Kh", "Qh", "9h", "4h", "2s"}; /* weaker ranks */
    const char *d_c[] = {"Ah", "Th", "8h", "5h", "2s"}; /* second rank lower */

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("soko", &cfg));
    a = make_hand(a_c, 5);
    b = make_hand(b_c, 5);
    c = make_hand(c_c, 5);
    d = make_hand(d_c, 5);

    TEST_ASSERT_EQUAL_UINT32(pe_eval_configurable_hand(&std, &cfg, a, 5),
                             pe_eval_configurable_hand(&std, &cfg, b, 5));
    /* Ranks compare high-to-low: a fourth suited card never substitutes for
     * a weaker first rank, whatever the kicker is. */
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, a, 5) >
                     pe_eval_configurable_hand(&std, &cfg, c, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, a, 5) >
                     pe_eval_configurable_hand(&std, &cfg, d, 5));
}

/* Equal four-straight high cards tie whatever the fifth card is; A-2-3-4 sits
 * below 2-3-4-5 and J-Q-K-A plays Ace-high (win2day §7.f.14.2). */
static void test_soko_four_straight_tiebreaks(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t a, b, queen_high, jack_high, ace_low, five_high;
    pe_card_mask_t king_high, ace_high;
    const char *a_c[] = {"9h", "Ts", "Jd", "Qc", "2h"};
    const char *b_c[] = {"9s", "Td", "Jh", "Qc", "3d"};
    const char *qh_c[] = {"9h", "Ts", "Jd", "Qc", "2h"};
    const char *jh_c[] = {"8h", "9s", "Td", "Jc", "2d"};
    const char *a_low_c[] = {"Ah", "2s", "3d", "4c", "Kh"};
    const char *five_high_c[] = {"2h", "3s", "4d", "5c", "Kh"};
    const char *k_high_c[] = {"Th", "Js", "Qd", "Kc", "2h"};
    const char *a_high_c[] = {"Jh", "Qs", "Kd", "Ac", "2h"};

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("soko", &cfg));
    a = make_hand(a_c, 5);
    b = make_hand(b_c, 5);
    queen_high = make_hand(qh_c, 5);
    jack_high = make_hand(jh_c, 5);
    ace_low = make_hand(a_low_c, 5);
    five_high = make_hand(five_high_c, 5);
    king_high = make_hand(k_high_c, 5);
    ace_high = make_hand(a_high_c, 5);

    TEST_ASSERT_EQUAL_UINT32(pe_eval_configurable_hand(&std, &cfg, a, 5),
                             pe_eval_configurable_hand(&std, &cfg, b, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, queen_high, 5) >
                     pe_eval_configurable_hand(&std, &cfg, jack_high, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, five_high, 5) >
                     pe_eval_configurable_hand(&std, &cfg, ace_low, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, ace_high, 5) >
                     pe_eval_configurable_hand(&std, &cfg, king_high, 5));
}

/* A hand qualifying for several categories uses the strongest Sökö one, and a
 * full five-card straight or flush is never demoted to a four-card category. */
static void test_soko_classification(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t both, full_flush, full_straight, st_flush, four_only;
    const char *both_c[] = {"Ah", "Kh", "Qh", "Jh", "9s"}; /* 4-flush + 4-straight */
    const char *full_flush_c[] = {"Ah", "Jh", "8h", "5h", "2h"};
    const char *full_straight_c[] = {"Th", "Js", "Qs", "Ks", "Ah"};
    const char *st_flush_c[] = {"Th", "Jh", "Qh", "Kh", "Ah"};
    const char *four_only_c[] = {"9h", "Ts", "Jd", "Qc", "2h"};

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("soko", &cfg));
    both = make_hand(both_c, 5);
    full_flush = make_hand(full_flush_c, 5);
    full_straight = make_hand(full_straight_c, 5);
    st_flush = make_hand(st_flush_c, 5);
    four_only = make_hand(four_only_c, 5);

    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_FOUR_CARD_FLUSH),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, both, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_FOUR_CARD_STRAIGHT),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, four_only, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_FLUSH),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, full_flush, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_STRAIGHT),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, full_straight, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_STRAIGHT_FLUSH),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, st_flush, 5)));
}

/* "canadian_stud", "soko" and "sökö" are aliases of one configuration. */
static void test_soko_preset_aliases_agree(void) {
    pe_hand_ranking_config_t canadian, soko, unicode;
    const char *hands[][5] = {
        {"9h", "Th", "Jh", "3h", "2s"},
        {"9h", "Ts", "Jd", "Qc", "2h"},
        {"Ah", "Ad", "Ks", "Kd", "2h"},
        {"Ah", "Ad", "Ac", "Ks", "2d"},
        {"Th", "Js", "Qs", "Ks", "Ah"},
    };
    int i;

    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("canadian_stud", &canadian));
    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("soko", &soko));
    TEST_ASSERT_EQUAL_INT(0, pe_ranking_config_set_preset("s\xc3\xb6k\xc3\xb6", &unicode));
    for (i = 0; i < (int)(sizeof(hands) / sizeof(hands[0])); i++) {
        pe_card_mask_t h = make_hand(hands[i], 5);
        HandVal base = pe_eval_configurable_hand(&std, &canadian, h, 5);
        TEST_ASSERT_EQUAL_UINT32(base, pe_eval_configurable_hand(&std, &soko, h, 5));
        TEST_ASSERT_EQUAL_UINT32(base, pe_eval_configurable_hand(&std, &unicode, h, 5));
    }
}

/* The 4-card categories are only active under the stud presets: under the
 * standard preset the same hands collapse to High Card. */
static void test_4card_categories_inactive_under_standard(void) {
    pe_hand_ranking_config_t std_cfg, can_cfg;
    pe_card_mask_t four_flush, four_straight, onepair;
    const char *four_flush_c[] = {"Ah", "Jh", "8h", "5h", "2s"};
    const char *four_str_c[]    = {"Th", "Js", "Qs", "Ks", "2h"};
    const char *pair_c[]        = {"Ah", "Ad", "Kc", "Qs", "2h"};

    pe_ranking_config_set_preset("standard", &std_cfg);
    pe_ranking_config_set_preset("canadian_stud", &can_cfg);
    four_flush   = make_hand(four_flush_c, 5);
    four_straight = make_hand(four_str_c, 5);
    onepair      = make_hand(pair_c, 5);

    /* Under standard these are not their own categories, so a pair beats them. */
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &std_cfg, onepair, 5) >
                     pe_eval_configurable_hand(&std, &std_cfg, four_flush, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &std_cfg, onepair, 5) >
                     pe_eval_configurable_hand(&std, &std_cfg, four_straight, 5));

    /* Under canadian the 4-card categories outrank a pair. */
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &can_cfg, four_flush, 5) >
                     pe_eval_configurable_hand(&std, &can_cfg, onepair, 5));
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &can_cfg, four_straight, 5) >
                     pe_eval_configurable_hand(&std, &can_cfg, onepair, 5));
}

/* New York Stud places the 4-card flush above the straight. */
static void test_new_york_stud_4cardflush_beats_straight(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t straight, four_flush;
    const char *straight_c[]   = {"Th", "Js", "Qs", "Ks", "Ah"};
    const char *four_flush_c[] = {"Ah", "Jh", "8h", "5h", "2s"};
    pe_ranking_config_set_preset("new_york_stud", &cfg);
    straight   = make_hand(straight_c, 5);
    four_flush = make_hand(four_flush_c, 5);
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, four_flush, 5) >
                     pe_eval_configurable_hand(&std, &cfg, straight, 5));
}

/* Canonical tiebreak: 333KK beats 222AA (trips rank dominates the pair kicker). */
static void test_canonical_fullhouse_tiebreak(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t a, b;
    const char *a_c[] = {"2h", "2d", "2c", "As", "Ad"};
    const char *b_c[] = {"3h", "3d", "3c", "Ks", "Kd"};
    pe_ranking_config_set_preset("standard", &cfg);
    a = make_hand(a_c, 5);
    b = make_hand(b_c, 5);
    TEST_ASSERT_TRUE(pe_eval_configurable_hand(&std, &cfg, b, 5) >
                     pe_eval_configurable_hand(&std, &cfg, a, 5));
}

/* Normalization: the HANDTYPE field equals the category's configured rank,
 * and a larger HandVal always corresponds to a stronger configured category. */
static void test_normalized_handval(void) {
    pe_hand_ranking_config_t cfg;
    pe_card_mask_t two_pair, trips, four_straight, four_flush, straight;
    const char *two_pair_c[]    = {"Ah", "Ad", "Ks", "Kd", "2h"};
    const char *trips_c[]        = {"Ah", "Ad", "Ac", "Ks", "2d"};
    const char *four_str_c[]     = {"Th", "Js", "Qs", "Ks", "2h"};
    const char *four_flush_c[]   = {"Ah", "Jh", "8h", "5h", "2s"};
    const char *straight_c[]     = {"Th", "Js", "Qs", "Ks", "Ah"};

    pe_ranking_config_set_preset("canadian_stud", &cfg);
    two_pair    = make_hand(two_pair_c, 5);
    trips       = make_hand(trips_c, 5);
    four_straight = make_hand(four_str_c, 5);
    four_flush  = make_hand(four_flush_c, 5);
    straight    = make_hand(straight_c, 5);

    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_TWO_PAIR),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, two_pair, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_THREE_OF_A_KIND),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, trips, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_FOUR_CARD_STRAIGHT),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, four_straight, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_FOUR_CARD_FLUSH),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, four_flush, 5)));
    TEST_ASSERT_EQUAL_INT(pe_ranking_config_category_rank(&cfg, PE_CAT_STRAIGHT),
                          pe_ranking_category_rank(pe_eval_configurable_hand(&std, &cfg, straight, 5)));

    TEST_ASSERT_EQUAL_INT(pe_ranking_category_from_rank(pe_ranking_config_category_rank(&cfg, PE_CAT_STRAIGHT)),
                          PE_CAT_STRAIGHT);
}

/* Invalid input is rejected gracefully. */
static void test_invalid_input(void) {
    pe_hand_ranking_config_t cfg;
    pe_hand_ranking_config_t *nullcfg = NULL;
    pe_deck_spec_t *nullspec = NULL;
    pe_card_mask_t empty = 0;
    pe_ranking_config_set_preset("standard", &cfg);
    TEST_ASSERT_EQUAL_INT(HandVal_NOTHING,
                          pe_eval_configurable_hand(&std, &cfg, empty, 5));
    TEST_ASSERT_EQUAL_INT(HandVal_NOTHING,
                          pe_eval_configurable_hand(nullspec, &cfg, empty, 5));
    TEST_ASSERT_EQUAL_INT(HandVal_NOTHING,
                          pe_eval_configurable_hand(&std, nullcfg, empty, 5));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_presets_initialize);
    RUN_TEST(test_standard_fullhouse_beats_flush);
    RUN_TEST(test_italian_manila_flush_beats_fullhouse);
    RUN_TEST(test_short_deck_flush_beats_fullhouse);
    RUN_TEST(test_soko_ordering);
    RUN_TEST(test_soko_four_flush_tiebreaks);
    RUN_TEST(test_soko_four_straight_tiebreaks);
    RUN_TEST(test_soko_classification);
    RUN_TEST(test_soko_preset_aliases_agree);
    RUN_TEST(test_4card_categories_inactive_under_standard);
    RUN_TEST(test_new_york_stud_4cardflush_beats_straight);
    RUN_TEST(test_canonical_fullhouse_tiebreak);
    RUN_TEST(test_normalized_handval);
    RUN_TEST(test_invalid_input);
    return UNITY_END();
}
