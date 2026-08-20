/*
 * Unit tests for the video poker hand classifier
 * (pe_video_poker_num_categories / pe_video_poker_category).
 *
 * The classifier must match the video poker machine rule: a wild hand is
 * scored as its best possible category, in the canonical row order of the
 * published (Wizard of Odds) tables.
 *
 * Run via CTest (registered with add_unity_test). A failing assertion aborts
 * the process with a non-zero exit code, which CTest reports as a failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/economics/video_poker_strategy.h>

#define H StdDeck_MAKE_CARD
#define R_TEN StdDeck_Rank_TEN
#define R_JACK StdDeck_Rank_JACK
#define R_QUEEN StdDeck_Rank_QUEEN
#define R_KING StdDeck_Rank_KING
#define R_ACE StdDeck_Rank_ACE
#define R_9 StdDeck_Rank_9
#define R_8 StdDeck_Rank_8
#define R_7 StdDeck_Rank_7
#define R_6 StdDeck_Rank_6
#define R_5 StdDeck_Rank_5
#define R_4 StdDeck_Rank_4
#define R_3 StdDeck_Rank_3
#define R_2 StdDeck_Rank_2
#define S_HEARTS StdDeck_Suit_HEARTS
#define S_DIAMONDS StdDeck_Suit_DIAMONDS
#define S_CLUBS StdDeck_Suit_CLUBS
#define S_SPADES StdDeck_Suit_SPADES

static StdDeck_CardMask make_hand(int c0, int c1, int c2, int c3, int c4)
{
    StdDeck_CardMask h;
    StdDeck_CardMask_RESET(h);
    StdDeck_CardMask_SET(h, c0);
    StdDeck_CardMask_SET(h, c1);
    StdDeck_CardMask_SET(h, c2);
    StdDeck_CardMask_SET(h, c3);
    StdDeck_CardMask_SET(h, c4);
    return h;
}

static StdDeck_CardMask make_joker_hand(int c0, int c1, int c2, int c3)
{
    StdDeck_CardMask h;
    StdDeck_CardMask_RESET(h);
    StdDeck_CardMask_SET(h, c0);
    StdDeck_CardMask_SET(h, c1);
    StdDeck_CardMask_SET(h, c2);
    StdDeck_CardMask_SET(h, c3);
    /* The joker lives in the dedicated joker field of the mask (bit 61 of the
     * 64-bit layout), not at raw bit 52: the StdDeck fields are packed
     * spades/clubs/diamonds/hearts with 3-bit padding, so bit 52 is the
     * hearts field (the 6 of hearts). */
    JokerDeck_CardMask jm;
    jm.cards_n = h.cards_n;
    JokerDeck_CardMask_SET_JOKER(jm, 1);
    h.cards_n = jm.cards_n;
    return h;
}

/* 1. Category counts and error paths. */
static void test_num_categories(void)
{
    assert(pe_video_poker_num_categories(PE_VP_JACKS_OR_BETTER) == 10);
    assert(pe_video_poker_num_categories(PE_VP_DEUCES_WILD) == 11);
    assert(pe_video_poker_num_categories(PE_VP_JOKER_POKER) == 12);
    assert(pe_video_poker_num_categories((pe_video_poker_variant_t)-1) == 0);
    assert(pe_video_poker_num_categories(PE_VP_VARIANT_COUNT) == 0);
    assert(pe_video_poker_category((pe_video_poker_variant_t)-1,
                                   make_hand(0, 1, 2, 3, 4)) == -1);
    printf("  num_categories ok\n");
}

/* 2. Jacks or Better: every category in row order. */
static void test_job_classifier(void)
{
    pe_video_poker_variant_t v = PE_VP_JACKS_OR_BETTER;
    assert(pe_video_poker_category(
               v, make_hand(H(R_TEN, S_HEARTS), H(R_JACK, S_HEARTS),
                            H(R_QUEEN, S_HEARTS), H(R_KING, S_HEARTS),
                            H(R_ACE, S_HEARTS))) == PE_VP_JOB_ROYAL_FLUSH);
    assert(pe_video_poker_category(
               v, make_hand(H(R_9, S_HEARTS), H(R_JACK, S_HEARTS),
                            H(R_QUEEN, S_HEARTS), H(R_KING, S_HEARTS),
                            H(R_TEN, S_HEARTS))) == PE_VP_JOB_STRAIGHT_FLUSH);
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_ACE, S_CLUBS),
                            H(R_ACE, S_DIAMONDS), H(R_ACE, S_SPADES),
                            H(R_2, S_HEARTS))) == PE_VP_JOB_FOUR_OF_A_KIND);
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_ACE, S_CLUBS),
                            H(R_ACE, S_DIAMONDS), H(R_KING, S_SPADES),
                            H(R_KING, S_HEARTS))) == PE_VP_JOB_FULL_HOUSE);
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_4, S_HEARTS),
                            H(R_7, S_HEARTS), H(R_KING, S_HEARTS),
                            H(R_9, S_HEARTS))) == PE_VP_JOB_FLUSH);
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_3, S_CLUBS),
                            H(R_4, S_DIAMONDS), H(R_5, S_SPADES),
                            H(R_6, S_HEARTS))) == PE_VP_JOB_STRAIGHT);
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_2, S_CLUBS),
                            H(R_3, S_DIAMONDS), H(R_4, S_SPADES),
                            H(R_5, S_HEARTS))) == PE_VP_JOB_STRAIGHT);
    assert(pe_video_poker_category(
               v, make_hand(H(R_7, S_HEARTS), H(R_7, S_CLUBS),
                            H(R_7, S_DIAMONDS), H(R_5, S_SPADES),
                            H(R_2, S_HEARTS))) == PE_VP_JOB_THREE_OF_A_KIND);
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_ACE, S_CLUBS),
                            H(R_KING, S_DIAMONDS), H(R_KING, S_SPADES),
                            H(R_2, S_HEARTS))) == PE_VP_JOB_TWO_PAIR);
    assert(pe_video_poker_category(
               v, make_hand(H(R_JACK, S_HEARTS), H(R_JACK, S_CLUBS),
                            H(R_2, S_DIAMONDS), H(R_3, S_SPADES),
                            H(R_4, S_HEARTS))) == PE_VP_JOB_JACKS_OR_BETTER);
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_ACE, S_CLUBS),
                            H(R_2, S_DIAMONDS), H(R_3, S_SPADES),
                            H(R_4, S_HEARTS))) == PE_VP_JOB_JACKS_OR_BETTER);
    /* A pair of tens is below the pay line. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_TEN, S_HEARTS), H(R_TEN, S_CLUBS),
                            H(R_2, S_DIAMONDS), H(R_3, S_SPADES),
                            H(R_4, S_HEARTS))) == PE_VP_JOB_NOTHING);
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_KING, S_CLUBS),
                            H(R_8, S_DIAMONDS), H(R_5, S_SPADES),
                            H(R_2, S_HEARTS))) == PE_VP_JOB_NOTHING);
    printf("  jacks or better ok\n");
}

/* 3. Deuces Wild: natural royal, four deuces, wild royals, five of a kind. */
static void test_dw_classifier(void)
{
    pe_video_poker_variant_t v = PE_VP_DEUCES_WILD;
    /* Natural royal flush, no deuces. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_TEN, S_HEARTS), H(R_JACK, S_HEARTS),
                            H(R_QUEEN, S_HEARTS), H(R_KING, S_HEARTS),
                            H(R_ACE, S_HEARTS))) == PE_VP_DW_NATURAL_ROYAL_FLUSH);
    /* A royal with a deuce wild is NOT natural. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_TEN, S_HEARTS), H(R_JACK, S_HEARTS),
                            H(R_QUEEN, S_HEARTS), H(R_KING, S_HEARTS),
                            H(R_2, S_HEARTS))) == PE_VP_DW_WILD_ROYAL_FLUSH);
    assert(pe_video_poker_category(
               v, make_hand(H(R_TEN, S_HEARTS), H(R_JACK, S_HEARTS),
                            H(R_QUEEN, S_HEARTS), H(R_2, S_CLUBS),
                            H(R_2, S_DIAMONDS))) == PE_VP_DW_WILD_ROYAL_FLUSH);
    /* Four deuces outrank everything (natural royal included). */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_2, S_CLUBS),
                            H(R_2, S_DIAMONDS), H(R_2, S_SPADES),
                            H(R_ACE, S_HEARTS))) == PE_VP_DW_FOUR_DEUCES);
    /* Five of a kind: 3 deuces + a pair, or 1 deuce + quads. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_2, S_CLUBS),
                            H(R_2, S_DIAMONDS), H(R_9, S_SPADES),
                            H(R_9, S_HEARTS))) == PE_VP_DW_FIVE_OF_A_KIND);
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_9, S_CLUBS),
                            H(R_9, S_DIAMONDS), H(R_9, S_SPADES),
                            H(R_9, S_HEARTS))) == PE_VP_DW_FIVE_OF_A_KIND);
    /* Straight flush (natural and wild). */
    assert(pe_video_poker_category(
               v, make_hand(H(R_3, S_HEARTS), H(R_4, S_HEARTS),
                            H(R_5, S_HEARTS), H(R_6, S_HEARTS),
                            H(R_7, S_HEARTS))) == PE_VP_DW_STRAIGHT_FLUSH);
    assert(pe_video_poker_category(
               v, make_hand(H(R_3, S_HEARTS), H(R_4, S_HEARTS),
                            H(R_5, S_HEARTS), H(R_6, S_HEARTS),
                            H(R_2, S_CLUBS))) == PE_VP_DW_STRAIGHT_FLUSH);
    /* Four of a kind (with wilds). */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_9, S_CLUBS),
                            H(R_9, S_DIAMONDS), H(R_9, S_SPADES),
                            H(R_3, S_HEARTS))) == PE_VP_DW_FOUR_OF_A_KIND);
    /* Full house with wilds: one deuce upgrades one of the two pairs. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_9, S_CLUBS),
                            H(R_9, S_DIAMONDS), H(R_3, S_SPADES),
                            H(R_3, S_HEARTS))) == PE_VP_DW_FULL_HOUSE);
    /* Two deuces + a pair is four of a kind (both wilds join the pair). */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_2, S_CLUBS),
                            H(R_9, S_DIAMONDS), H(R_9, S_SPADES),
                            H(R_3, S_HEARTS))) == PE_VP_DW_FOUR_OF_A_KIND);
    /* Flush and straight. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_5, S_HEARTS),
                            H(R_8, S_HEARTS), H(R_KING, S_HEARTS),
                            H(R_9, S_HEARTS))) == PE_VP_DW_FLUSH);
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_4, S_CLUBS),
                            H(R_6, S_DIAMONDS), H(R_7, S_SPADES),
                            H(R_8, S_HEARTS))) == PE_VP_DW_STRAIGHT);
    /* Three of a kind with wilds. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_HEARTS), H(R_9, S_CLUBS),
                            H(R_9, S_DIAMONDS), H(R_4, S_SPADES),
                            H(R_5, S_HEARTS))) == PE_VP_DW_THREE_OF_A_KIND);
    assert(pe_video_poker_category(
               v, make_hand(H(R_7, S_HEARTS), H(R_9, S_CLUBS),
                            H(R_4, S_DIAMONDS), H(R_5, S_SPADES),
                            H(R_6, S_HEARTS))) == PE_VP_DW_NOTHING);
    printf("  deuces wild ok\n");
}

/* 4. Joker Poker: joker as wild, kings or better line. */
static void test_jp_classifier(void)
{
    pe_video_poker_variant_t v = PE_VP_JOKER_POKER;
    /* Natural royal flush (no joker). */
    assert(pe_video_poker_category(
               v, make_hand(H(R_TEN, S_HEARTS), H(R_JACK, S_HEARTS),
                            H(R_QUEEN, S_HEARTS), H(R_KING, S_HEARTS),
                            H(R_ACE, S_HEARTS))) == PE_VP_JP_NATURAL_ROYAL_FLUSH);
    /* Joker + four hearts T-A is a wild royal flush, not natural. */
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_TEN, S_HEARTS), H(R_JACK, S_HEARTS),
                                  H(R_QUEEN, S_HEARTS), H(R_KING, S_HEARTS))) ==
           PE_VP_JP_WILD_ROYAL_FLUSH);
    /* Joker + four of a kind = five of a kind. */
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_9, S_HEARTS), H(R_9, S_CLUBS),
                                  H(R_9, S_DIAMONDS), H(R_9, S_SPADES))) ==
           PE_VP_JP_FIVE_OF_A_KIND);
    /* Joker + 4 to a straight flush. */
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_3, S_HEARTS), H(R_4, S_HEARTS),
                                  H(R_5, S_HEARTS), H(R_6, S_HEARTS))) ==
           PE_VP_JP_STRAIGHT_FLUSH);
    /* Joker + trips = four of a kind. */
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_9, S_HEARTS), H(R_9, S_CLUBS),
                                  H(R_9, S_DIAMONDS), H(R_4, S_SPADES))) ==
           PE_VP_JP_FOUR_OF_A_KIND);
    /* Joker + pair = trips. */
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_9, S_HEARTS), H(R_9, S_CLUBS),
                                  H(R_4, S_DIAMONDS), H(R_5, S_SPADES))) ==
           PE_VP_JP_THREE_OF_A_KIND);
    /* Joker + ace = aces, kings or better. */
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_ACE, S_HEARTS), H(R_4, S_CLUBS),
                                  H(R_5, S_DIAMONDS), H(R_6, S_SPADES))) ==
           PE_VP_JP_KINGS_OR_BETTER);
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_KING, S_HEARTS), H(R_4, S_CLUBS),
                                  H(R_5, S_DIAMONDS), H(R_6, S_SPADES))) ==
           PE_VP_JP_KINGS_OR_BETTER);
    /* Joker + queen or lower is nothing. */
    assert(pe_video_poker_category(
               v, make_joker_hand(H(R_QUEEN, S_HEARTS), H(R_4, S_CLUBS),
                                  H(R_5, S_DIAMONDS), H(R_6, S_SPADES))) ==
           PE_VP_JP_NOTHING);
    /* Natural two pair. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_ACE, S_CLUBS),
                            H(R_KING, S_DIAMONDS), H(R_KING, S_SPADES),
                            H(R_2, S_HEARTS))) == PE_VP_JP_TWO_PAIR);
    /* Natural pair of aces is kings or better. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_ACE, S_HEARTS), H(R_ACE, S_CLUBS),
                            H(R_4, S_DIAMONDS), H(R_5, S_SPADES),
                            H(R_6, S_HEARTS))) == PE_VP_JP_KINGS_OR_BETTER);
    assert(pe_video_poker_category(
               v, make_hand(H(R_QUEEN, S_HEARTS), H(R_QUEEN, S_CLUBS),
                            H(R_4, S_DIAMONDS), H(R_5, S_SPADES),
                            H(R_6, S_HEARTS))) == PE_VP_JP_NOTHING);
    printf("  joker poker ok\n");
}

/* 5. Wilds must be scored as the best possible category. */
static void test_wild_scoring(void)
{
    pe_video_poker_variant_t v = PE_VP_DEUCES_WILD;
    /* 2c 2d 9h 9s 3c: two deuces + a pair. The best category is four of a
     * kind (both wilds join the pair), not a full house. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_CLUBS), H(R_2, S_DIAMONDS),
                            H(R_9, S_HEARTS), H(R_9, S_SPADES),
                            H(R_3, S_CLUBS))) == PE_VP_DW_FOUR_OF_A_KIND);
    /* 2c 9h 9s 3d 3c: one deuce upgrades one of the two pairs to trips. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_CLUBS), H(R_9, S_HEARTS),
                            H(R_9, S_SPADES), H(R_3, S_DIAMONDS),
                            H(R_3, S_CLUBS))) == PE_VP_DW_FULL_HOUSE);
    /* 2c Jd Qh Ks Ac: one deuce completes the broadway straight, so the best
     * category is a straight (not nothing). */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_CLUBS), H(R_JACK, S_DIAMONDS),
                            H(R_QUEEN, S_HEARTS), H(R_KING, S_SPADES),
                            H(R_ACE, S_CLUBS))) == PE_VP_DW_STRAIGHT);
    /* 2c 3h 7d Js Kc: the deuce cannot complete any straight. */
    assert(pe_video_poker_category(
               v, make_hand(H(R_2, S_CLUBS), H(R_3, S_HEARTS),
                            H(R_7, S_DIAMONDS), H(R_JACK, S_SPADES),
                            H(R_KING, S_CLUBS))) == PE_VP_DW_NOTHING);
    /* Joker poker: joker + four offsuit distinct low cards is nothing. */
    assert(pe_video_poker_category(
               PE_VP_JOKER_POKER,
               make_joker_hand(H(R_3, S_CLUBS), H(R_6, S_DIAMONDS),
                               H(R_9, S_HEARTS), H(R_QUEEN, S_SPADES))) ==
           PE_VP_JP_NOTHING);
    printf("  wild scoring ok\n");
}

/* 6. Invalid hands: wrong card count, joker in a non-joker game. */
static void test_invalid_hands(void)
{
    StdDeck_CardMask four;
    StdDeck_CardMask_RESET(four);
    StdDeck_CardMask_SET(four, H(R_ACE, S_HEARTS));
    StdDeck_CardMask_SET(four, H(R_KING, S_HEARTS));
    StdDeck_CardMask_SET(four, H(R_QUEEN, S_HEARTS));
    StdDeck_CardMask_SET(four, H(R_JACK, S_HEARTS));
    assert(pe_video_poker_category(PE_VP_JACKS_OR_BETTER, four) == -1);
    assert(pe_video_poker_category(PE_VP_DEUCES_WILD, four) == -1);
    assert(pe_video_poker_category(PE_VP_JOKER_POKER, four) == -1);

    StdDeck_CardMask joker_hand = make_joker_hand(
        H(R_ACE, S_HEARTS), H(R_KING, S_HEARTS), H(R_QUEEN, S_HEARTS),
        H(R_JACK, S_HEARTS));
    assert(pe_video_poker_category(PE_VP_JACKS_OR_BETTER, joker_hand) == -1);
    assert(pe_video_poker_category(PE_VP_DEUCES_WILD, joker_hand) == -1);
    assert(pe_video_poker_category(PE_VP_JOKER_POKER, joker_hand) ==
           PE_VP_JP_WILD_ROYAL_FLUSH);
    printf("  invalid hands ok\n");
}

int main(void)
{
    printf("=== Video Poker Strategy test suite ===\n");
    test_num_categories();
    test_job_classifier();
    test_dw_classifier();
    test_jp_classifier();
    test_wild_scoring();
    test_invalid_hands();
    printf("=== All Video Poker Strategy tests passed ===\n");
    return 0;
}
