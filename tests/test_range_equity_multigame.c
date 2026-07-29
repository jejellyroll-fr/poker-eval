/*
 * test_range_equity_multigame.c -- tests for RangeEquity across game types
 *
 * Validates CalculateEquityForRanges() and multi-threaded variants (MT v1-v5)
 * across multiple poker game types.
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/RangeEquity_MT.h>
#include <poker_eval/equity/enumord.h>

/* Tolerance for floating point comparisons */
#define EV_TOLERANCE 1e-5
#define MC_TOLERANCE 0.02  /* Monte Carlo has higher variance */

/* Helper to add a card to a mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit)
{
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Generate pocket pairs for a single rank */
static int generate_pocket_pairs(StdDeck_CardMask *hands, int rank)
{
    int count = 0;
    for (int suit1 = 0; suit1 < 4; suit1++) {
        for (int suit2 = suit1 + 1; suit2 < 4; suit2++) {
            StdDeck_CardMask_RESET(hands[count]);
            add_card(&hands[count], rank, suit1);
            add_card(&hands[count], rank, suit2);
            count++;
        }
    }
    return count; /* Always 6 combos for a pair */
}

/* Generate suited hands for two ranks */
static int generate_suited_hands(StdDeck_CardMask *hands, int rank1, int rank2)
{
    int count = 0;
    for (int suit = 0; suit < 4; suit++) {
        StdDeck_CardMask_RESET(hands[count]);
        add_card(&hands[count], rank1, suit);
        add_card(&hands[count], rank2, suit);
        count++;
    }
    return count; /* Always 4 combos */
}

/* Generate offsuit hands for two ranks */
static int generate_offsuit_hands(StdDeck_CardMask *hands, int rank1, int rank2)
{
    int count = 0;
    for (int suit1 = 0; suit1 < 4; suit1++) {
        for (int suit2 = 0; suit2 < 4; suit2++) {
            if (suit1 == suit2) continue;
            StdDeck_CardMask_RESET(hands[count]);
            add_card(&hands[count], rank1, suit1);
            add_card(&hands[count], rank2, suit2);
            count++;
        }
    }
    return count; /* Always 12 combos */
}

/* Generate 4-card Omaha hands (AAxx for testing) */
static int generate_omaha_aces(StdDeck_CardMask *hands)
{
    int count = 0;
    /* AA with two other cards */
    for (int s1 = 0; s1 < 4; s1++) {
        for (int s2 = s1 + 1; s2 < 4; s2++) {
            /* AA with two kickers - use king and queen */
            for (int ks = 0; ks < 4; ks++) {
                for (int qs = 0; qs < 4; qs++) {
                    if (ks == qs) continue; /* Avoid duplicate suits with K/Q */
                    StdDeck_CardMask_RESET(hands[count]);
                    add_card(&hands[count], StdDeck_Rank_ACE, s1);
                    add_card(&hands[count], StdDeck_Rank_ACE, s2);
                    add_card(&hands[count], StdDeck_Rank_KING, ks);
                    add_card(&hands[count], StdDeck_Rank_QUEEN, qs);
                    count++;
                    if (count >= 20) return count; /* Limit for testing */
                }
            }
        }
    }
    return count;
}

/* Generate 4-card Omaha hands (KKxx for testing) */
static int generate_omaha_kings(StdDeck_CardMask *hands)
{
    int count = 0;
    for (int s1 = 0; s1 < 4; s1++) {
        for (int s2 = s1 + 1; s2 < 4; s2++) {
            for (int js = 0; js < 4; js++) {
                for (int ts = 0; ts < 4; ts++) {
                    if (js == ts) continue;
                    StdDeck_CardMask_RESET(hands[count]);
                    add_card(&hands[count], StdDeck_Rank_KING, s1);
                    add_card(&hands[count], StdDeck_Rank_KING, s2);
                    add_card(&hands[count], StdDeck_Rank_JACK, js);
                    add_card(&hands[count], StdDeck_Rank_TEN, ts);
                    count++;
                    if (count >= 20) return count;
                }
            }
        }
    }
    return count;
}

/* Generate 7-card stud hands (trip aces for testing) */
static int generate_stud_trip_aces(StdDeck_CardMask *hands)
{
    int count = 0;
    /* AAA with 4 kickers */
    for (int s1 = 0; s1 < 4; s1++) {
        for (int s2 = s1 + 1; s2 < 4; s2++) {
            for (int s3 = s2 + 1; s3 < 4; s3++) {
                /* Trip aces with K, Q, J, T kickers */
                StdDeck_CardMask_RESET(hands[count]);
                add_card(&hands[count], StdDeck_Rank_ACE, s1);
                add_card(&hands[count], StdDeck_Rank_ACE, s2);
                add_card(&hands[count], StdDeck_Rank_ACE, s3);
                add_card(&hands[count], StdDeck_Rank_KING, (s1 + 1) % 4);
                add_card(&hands[count], StdDeck_Rank_QUEEN, (s2 + 1) % 4);
                add_card(&hands[count], StdDeck_Rank_JACK, (s3 + 1) % 4);
                add_card(&hands[count], StdDeck_Rank_TEN, s1);
                count++;
            }
        }
    }
    return count; /* 4 combos */
}

/* Generate 7-card stud hands (trip kings for testing) */
static int generate_stud_trip_kings(StdDeck_CardMask *hands)
{
    int count = 0;
    for (int s1 = 0; s1 < 4; s1++) {
        for (int s2 = s1 + 1; s2 < 4; s2++) {
            for (int s3 = s2 + 1; s3 < 4; s3++) {
                StdDeck_CardMask_RESET(hands[count]);
                add_card(&hands[count], StdDeck_Rank_KING, s1);
                add_card(&hands[count], StdDeck_Rank_KING, s2);
                add_card(&hands[count], StdDeck_Rank_KING, s3);
                add_card(&hands[count], StdDeck_Rank_QUEEN, (s1 + 1) % 4);
                add_card(&hands[count], StdDeck_Rank_JACK, (s2 + 1) % 4);
                add_card(&hands[count], StdDeck_Rank_9, (s3 + 1) % 4);
                add_card(&hands[count], StdDeck_Rank_8, s1);
                count++;
            }
        }
    }
    return count;
}

void setUp(void) {}
void tearDown(void) {}

/* ===== HOLDEM RANGE EQUITY TESTS ===== */

/*
 * Test basic Holdem range equity: AA vs KK
 */
void test_range_equity_holdem_aa_vs_kk(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    /* Near-river board for faster computation */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_holdem, ranges, 2, board, dead,
                                        1, 0, 0, 0, &result);

    TEST_ASSERT_EQUAL_INT(36, matchups); /* 6x6 */
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.7, result.ev[0]); /* AA should dominate */
    TEST_ASSERT_LESS_THAN_DOUBLE(0.3, result.ev[1]);

    enumResultFree(&result);
}

/*
 * Test Holdem range equity with suited hands: AKs vs QQ
 */
void test_range_equity_holdem_aks_vs_qq(void)
{
    StdDeck_CardMask aks_hands[4], qq_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    int aks_count = generate_suited_hands(aks_hands, StdDeck_Rank_ACE, StdDeck_Rank_KING);
    int qq_count = generate_pocket_pairs(qq_hands, StdDeck_Rank_QUEEN);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aks_hands;
    ranges[0].count = aks_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aks_count;

    ranges[1].hand_masks = qq_hands;
    ranges[1].count = qq_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = qq_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_holdem, ranges, 2, board, dead,
                                        1, 0, 0, 0, &result);

    TEST_ASSERT_EQUAL_INT(24, matchups); /* 4x6 */
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.0, result.ev[0]);
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.0, result.ev[1]);

    enumResultFree(&result);
}

/*
 * Test weighted ranges
 */
void test_range_equity_weighted(void)
{
    StdDeck_CardMask hero_hands[2], villain_hands[6];
    double hero_weights[2] = {0.75, 0.25};
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    /* Hero: 75% AA, 25% KK (one combo each) */
    StdDeck_CardMask_RESET(hero_hands[0]);
    add_card(&hero_hands[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&hero_hands[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(hero_hands[1]);
    add_card(&hero_hands[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&hero_hands[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    /* Villain: QQ (all 6 combos) */
    int qq_count = generate_pocket_pairs(villain_hands, StdDeck_Rank_QUEEN);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hero_hands;
    ranges[0].count = 2;
    ranges[0].weights = hero_weights;
    ranges[0].total_weight = 1.0;

    ranges[1].hand_masks = villain_hands;
    ranges[1].count = qq_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = qq_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_holdem, ranges, 2, board, dead,
                                        1, 0, 0, 0, &result);

    TEST_ASSERT_GREATER_THAN(0, matchups);
    /* Weighted equity should reflect the mix */
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.0, result.ev[0]);

    enumResultFree(&result);
}

/* ===== OMAHA RANGE EQUITY TESTS ===== */

/*
 * Test Omaha range equity
 */
void test_range_equity_omaha(void)
{
    StdDeck_CardMask aces_hands[1], kings_hands[1];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    StdDeck_CardMask_RESET(aces_hands[0]);
    add_card(&aces_hands[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&aces_hands[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&aces_hands[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&aces_hands[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(kings_hands[0]);
    add_card(&kings_hands[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&kings_hands[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&kings_hands[0], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&kings_hands[0], StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);

    int aces_count = 1;
    int kings_count = 1;

    PlayerRange ranges[2];
    ranges[0].hand_masks = aces_hands;
    ranges[0].count = aces_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aces_count;

    ranges[1].hand_masks = kings_hands;
    ranges[1].count = kings_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kings_count;

    /* Full board for faster computation */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_omaha, ranges, 2, board, dead,
                                        0, 0, 0, 0, &result);

    TEST_ASSERT_GREATER_THAN(0, matchups);
    /* Both should have some equity */
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.ev[0]);
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.ev[1]);

    enumResultFree(&result);
}

/* ===== 7-STUD RANGE EQUITY TESTS ===== */

/*
 * Test 7-card stud range equity
 */
void test_range_equity_7stud(void)
{
    StdDeck_CardMask trip_aces[1], trip_kings[1];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    StdDeck_CardMask_RESET(trip_aces[0]);
    add_card(&trip_aces[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&trip_aces[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&trip_aces[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&trip_aces[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&trip_aces[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&trip_aces[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&trip_aces[0], StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(trip_kings[0]);
    add_card(&trip_kings[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&trip_kings[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&trip_kings[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&trip_kings[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&trip_kings[0], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&trip_kings[0], StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&trip_kings[0], StdDeck_Rank_8, StdDeck_Suit_SPADES);

    int aces_count = 1;
    int kings_count = 1;

    PlayerRange ranges[2];
    ranges[0].hand_masks = trip_aces;
    ranges[0].count = aces_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aces_count;

    ranges[1].hand_masks = trip_kings;
    ranges[1].count = kings_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kings_count;

    /* No board for stud */
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_7stud, ranges, 2, board, dead,
                                        0, 0, 0, 0, &result);

    TEST_ASSERT_GREATER_THAN(0, matchups);
    /* Trip aces should beat trip kings */
    TEST_ASSERT_GREATER_THAN_DOUBLE(result.ev[1], result.ev[0]);

    enumResultFree(&result);
}

/* ===== MT VERSION CONSISTENCY TESTS ===== */

/*
 * Test that MT v1 matches single-threaded results
 */
void test_mt_v1_matches_single_thread(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result_st, result_mt;
    int matchups_st, matchups_mt;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result_st, 2, enum_ordering_mode_hi);
    enumResultAlloc(&result_mt, 2, enum_ordering_mode_hi);

    matchups_st = CalculateEquityForRanges(game_holdem, ranges, 2, board, dead,
                                           1, 0, 0, 0, &result_st);
    matchups_mt = CalculateEquityForRanges_MT(game_holdem, ranges, 2, board, dead,
                                              1, 0, 0, 0, &result_mt, 1);

    TEST_ASSERT_EQUAL_INT(matchups_st, matchups_mt);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_st.ev[0], result_mt.ev[0]);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_st.ev[1], result_mt.ev[1]);

    enumResultFree(&result_st);
    enumResultFree(&result_mt);
}

#ifdef _OPENMP
/*
 * Test that MT v2 matches v1 results
 */
void test_mt_v2_matches_v1(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result_v1, result_v2;
    int matchups_v1, matchups_v2;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result_v1, 2, enum_ordering_mode_hi);
    enumResultAlloc(&result_v2, 2, enum_ordering_mode_hi);

    matchups_v1 = CalculateEquityForRanges_MT(game_holdem, ranges, 2, board, dead,
                                              1, 0, 0, 0, &result_v1, 2);
    matchups_v2 = CalculateEquityForRanges_MT_v2(game_holdem, ranges, 2, board, dead,
                                                  1, 0, 0, 0, &result_v2, 2);

    TEST_ASSERT_EQUAL_INT(matchups_v1, matchups_v2);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[0], result_v2.ev[0]);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[1], result_v2.ev[1]);

    enumResultFree(&result_v1);
    enumResultFree(&result_v2);
}

/*
 * Test that MT v3 matches v1 results
 */
void test_mt_v3_matches_v1(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result_v1, result_v3;
    int matchups_v1, matchups_v3;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result_v1, 2, enum_ordering_mode_hi);
    enumResultAlloc(&result_v3, 2, enum_ordering_mode_hi);

    matchups_v1 = CalculateEquityForRanges_MT(game_holdem, ranges, 2, board, dead,
                                              1, 0, 0, 0, &result_v1, 2);
    matchups_v3 = CalculateEquityForRanges_MT_v3(game_holdem, ranges, 2, board, dead,
                                                  1, 0, 0, 0, &result_v3, 2);

    TEST_ASSERT_EQUAL_INT(matchups_v1, matchups_v3);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[0], result_v3.ev[0]);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[1], result_v3.ev[1]);

    enumResultFree(&result_v1);
    enumResultFree(&result_v3);
}

/*
 * Test that MT v4 matches v1 results
 */
void test_mt_v4_matches_v1(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result_v1, result_v4;
    int matchups_v1, matchups_v4;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result_v1, 2, enum_ordering_mode_hi);
    enumResultAlloc(&result_v4, 2, enum_ordering_mode_hi);

    matchups_v1 = CalculateEquityForRanges_MT(game_holdem, ranges, 2, board, dead,
                                              1, 0, 0, 0, &result_v1, 2);
    matchups_v4 = CalculateEquityForRanges_MT_v4(game_holdem, ranges, 2, board, dead,
                                                  1, 0, 0, 0, &result_v4, 2);

    TEST_ASSERT_EQUAL_INT(matchups_v1, matchups_v4);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[0], result_v4.ev[0]);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[1], result_v4.ev[1]);

    enumResultFree(&result_v1);
    enumResultFree(&result_v4);
}

/*
 * Test that MT v5 matches v1 results
 */
void test_mt_v5_matches_v1(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result_v1, result_v5;
    int matchups_v1, matchups_v5;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result_v1, 2, enum_ordering_mode_hi);
    enumResultAlloc(&result_v5, 2, enum_ordering_mode_hi);

    matchups_v1 = CalculateEquityForRanges_MT(game_holdem, ranges, 2, board, dead,
                                              1, 0, 0, 0, &result_v1, 2);
    matchups_v5 = CalculateEquityForRanges_MT_v5(game_holdem, ranges, 2, board, dead,
                                                  1, 0, 0, 0, &result_v5, 2);

    TEST_ASSERT_EQUAL_INT(matchups_v1, matchups_v5);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[0], result_v5.ev[0]);
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result_v1.ev[1], result_v5.ev[1]);

    enumResultFree(&result_v1);
    enumResultFree(&result_v5);
}

/*
 * Test that MT Batched produces valid results
 */
void test_mt_batched_valid_results(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges_MT_Batched(game_holdem, ranges, 2, board, dead,
                                                    1, 0, 0, 0, &result, 2);

    TEST_ASSERT_EQUAL_INT(36, matchups);
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.5, result.ev[0]); /* AA should dominate */

    enumResultFree(&result);
}
#endif /* _OPENMP */

/*
 * Test Auto selection returns valid results
 */
void test_auto_selects_strategy(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges_Auto(game_holdem, ranges, 2, board, dead,
                                              1, 0, 0, 0, &result);

    TEST_ASSERT_EQUAL_INT(36, matchups);
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.5, result.ev[0]);

    enumResultFree(&result);
}

/* ===== EDGE CASE TESTS ===== */

/*
 * Test range with single hand
 */
void test_range_single_hand(void)
{
    StdDeck_CardMask hero_hand[1], villain_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    /* Hero: single AA combo */
    StdDeck_CardMask_RESET(hero_hand[0]);
    add_card(&hero_hand[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&hero_hand[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

    int kk_count = generate_pocket_pairs(villain_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hero_hand;
    ranges[0].count = 1;
    ranges[0].weights = NULL;
    ranges[0].total_weight = 1;

    ranges[1].hand_masks = villain_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_holdem, ranges, 2, board, dead,
                                        1, 0, 0, 0, &result);

    TEST_ASSERT_EQUAL_INT(6, matchups); /* 1x6 */
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.7, result.ev[0]);

    enumResultFree(&result);
}

/*
 * Test empty range returns 0 matchups
 */
void test_range_empty_returns_zero(void)
{
    StdDeck_CardMask aa_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = NULL;
    ranges[1].count = 0;
    ranges[1].weights = NULL;
    ranges[1].total_weight = 0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_holdem, ranges, 2, board, dead,
                                        5, 0, 0, 0, &result);

    TEST_ASSERT_EQUAL_INT(0, matchups);

    enumResultFree(&result);
}

/*
 * Test dead cards overlap with range (some combos blocked)
 */
void test_range_dead_cards_overlap(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    PlayerRange ranges[2];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    /* Dead card: Ac (blocks 3 AA combos) */
    StdDeck_CardMask_RESET(dead);
    add_card(&dead, StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_holdem, ranges, 2, board, dead,
                                        1, 0, 0, 0, &result);

    /* 3 AA combos (without Ac) x 6 KK combos = 18 */
    TEST_ASSERT_EQUAL_INT(18, matchups);

    enumResultFree(&result);
}

/*
 * Test invalid number of players returns error
 */
void test_range_invalid_players(void)
{
    StdDeck_CardMask aa_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int ret;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);

    PlayerRange ranges[1];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 2, enum_ordering_mode_hi);

    /* 0 players should return error */
    ret = CalculateEquityForRanges(game_holdem, ranges, 0, board, dead,
                                   5, 0, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* Negative players should return error */
    ret = CalculateEquityForRanges(game_holdem, ranges, -1, board, dead,
                                   5, 0, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    enumResultFree(&result);
}

/*
 * Test 3-player equity calculation
 */
void test_range_3player(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6], qq_hands[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int matchups;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);
    int qq_count = generate_pocket_pairs(qq_hands, StdDeck_Rank_QUEEN);

    /* Limit range sizes for speed */
    if (aa_count > 3) aa_count = 3;
    if (kk_count > 3) kk_count = 3;
    if (qq_count > 3) qq_count = 3;

    PlayerRange ranges[3];
    ranges[0].hand_masks = aa_hands;
    ranges[0].count = aa_count;
    ranges[0].weights = NULL;
    ranges[0].total_weight = aa_count;

    ranges[1].hand_masks = kk_hands;
    ranges[1].count = kk_count;
    ranges[1].weights = NULL;
    ranges[1].total_weight = kk_count;

    ranges[2].hand_masks = qq_hands;
    ranges[2].count = qq_count;
    ranges[2].weights = NULL;
    ranges[2].total_weight = qq_count;

    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);

    StdDeck_CardMask_RESET(dead);

    enumResultAlloc(&result, 3, enum_ordering_mode_hi);

    matchups = CalculateEquityForRanges(game_holdem, ranges, 3, board, dead,
                                        1, 0, 0, 0, &result);

    TEST_ASSERT_GREATER_THAN(0, matchups);
    /* AA should have highest equity */
    TEST_ASSERT_GREATER_THAN_DOUBLE(result.ev[1], result.ev[0]);
    TEST_ASSERT_GREATER_THAN_DOUBLE(result.ev[2], result.ev[0]);
    /* With one random river, KK and QQ have symmetric two-out set chances. */
    TEST_ASSERT_DOUBLE_WITHIN(EV_TOLERANCE, result.ev[1], result.ev[2]);

    enumResultFree(&result);
}

int main(void)
{
    UNITY_BEGIN();

    /* Holdem range equity tests */
    RUN_TEST(test_range_equity_holdem_aa_vs_kk);
    RUN_TEST(test_range_equity_holdem_aks_vs_qq);
    RUN_TEST(test_range_equity_weighted);

    /* Multi-game tests */
    RUN_TEST(test_range_equity_omaha);
    RUN_TEST(test_range_equity_7stud);

    /* MT version consistency tests */
    RUN_TEST(test_mt_v1_matches_single_thread);
#ifdef _OPENMP
    RUN_TEST(test_mt_v2_matches_v1);
    RUN_TEST(test_mt_v3_matches_v1);
    RUN_TEST(test_mt_v4_matches_v1);
    RUN_TEST(test_mt_v5_matches_v1);
    RUN_TEST(test_mt_batched_valid_results);
#endif
    RUN_TEST(test_auto_selects_strategy);

    /* Edge case tests */
    RUN_TEST(test_range_single_hand);
    RUN_TEST(test_range_empty_returns_zero);
    RUN_TEST(test_range_dead_cards_overlap);
    RUN_TEST(test_range_invalid_players);
    RUN_TEST(test_range_3player);

    return UNITY_END();
}
