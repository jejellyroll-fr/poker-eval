/*
 * test_ofc_scoring.c - OFC scoring and royalties tests
 *
 * Copyright (C) 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <poker_eval/ofc/ofc.h>
#include <poker_eval/deck/deck_std.h>

/* Suppress unused variable warnings */
#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

static void test_ofc_top_royalties(void)
{
    ofc_hand_t hand;
    UNUSED int royalties = 0;

    printf("Testing OFC top hand royalties...\n");

    OFC_InitializeHand(&hand);

    /* Test pair of aces (should get 9 points) */
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

    royalties = OFC_CalculateTopRoyalties(hand.hands[OFC_TOP]);
    assert(royalties == 9);

    printf("  ✓ Pair of aces royalties work correctly\n");

    /* Test trips of kings (should get 10 points) */
    OFC_InitializeHand(&hand);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

    royalties = OFC_CalculateTopRoyalties(hand.hands[OFC_TOP]);
    assert(royalties == 10);

    printf("  ✓ Trip kings royalties work correctly\n");
}

static void test_ofc_middle_royalties(void)
{
    ofc_hand_t hand;
    UNUSED int royalties = 0;

    printf("Testing OFC middle hand royalties...\n");

    OFC_InitializeHand(&hand);

    /* Test full house (should get 12 points) */
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_MIDDLE);

    royalties = OFC_CalculateMiddleRoyalties(hand.hands[OFC_MIDDLE]);
    assert(royalties == 12);

    printf("  ✓ Full house royalties work correctly\n");

    /* Test flush (should get 8 points) */
    OFC_InitializeHand(&hand);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES), OFC_MIDDLE);

    royalties = OFC_CalculateMiddleRoyalties(hand.hands[OFC_MIDDLE]);
    assert(royalties == 8);

    printf("  ✓ Flush royalties work correctly\n");
}

static void test_ofc_bottom_royalties(void)
{
    ofc_hand_t hand;
    UNUSED int royalties = 0;

    printf("Testing OFC bottom hand royalties...\n");

    OFC_InitializeHand(&hand);

    /* Test straight (should get 2 points) */
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_BOTTOM);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS), OFC_BOTTOM);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS), OFC_BOTTOM);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES), OFC_BOTTOM);

    royalties = OFC_CalculateBottomRoyalties(hand.hands[OFC_BOTTOM]);
    assert(royalties == 2);

    printf("  ✓ Straight royalties work correctly\n");
}

static void test_ofc_fantasyland(void)
{
    ofc_hand_t hand;
    UNUSED int qualifies = 0;

    printf("Testing OFC fantasyland qualification...\n");

    OFC_InitializeHand(&hand);

    /* Test pair of queens (should qualify) */
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

    qualifies = OFC_CheckFantasyland(&hand);
    assert(qualifies == 1);

    printf("  ✓ Pair of queens qualifies for fantasyland\n");

    /* Test pair of jacks (should not qualify) */
    OFC_InitializeHand(&hand);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS), OFC_TOP);
    OFC_PlaceCard(&hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

    qualifies = OFC_CheckFantasyland(&hand);
    assert(qualifies == 0);

    printf("  ✓ Pair of jacks does not qualify for fantasyland\n");
}

static void test_ofc_hand_scoring(void)
{
    ofc_hand_t hand1, hand2;
    UNUSED ofc_score_t score1, score2;

    printf("Testing OFC hand scoring...\n");

    /* Create two simple complete hands */
    OFC_InitializeHand(&hand1);
    OFC_InitializeHand(&hand2);

    /* Hand 1: Strong hand */
    /* Top: Pair of aces */
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES), OFC_TOP);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS), OFC_TOP);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

    /* Middle: Trips */
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES), OFC_MIDDLE);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS), OFC_MIDDLE);

    /* Bottom: Full house */
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES), OFC_BOTTOM);
    OFC_PlaceCard(&hand1, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS), OFC_BOTTOM);

    /* Hand 2: Weaker hand */
    /* Top: High card */
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS), OFC_TOP);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS), OFC_TOP);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS), OFC_TOP);

    /* Middle: Pair */
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS), OFC_MIDDLE);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_CLUBS), OFC_MIDDLE);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);

    /* Bottom: Two pair */
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_SPADES), OFC_BOTTOM);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS), OFC_BOTTOM);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_CLUBS), OFC_BOTTOM);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS), OFC_BOTTOM);
    OFC_PlaceCard(&hand2, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES), OFC_BOTTOM);

    /* Score the hands */
    assert(OFC_ScoreHands(&hand1, &hand2, &score1, &score2) == 0);

    /* Hand 1 should win all three hands (scoop) */
    assert(score1.points[OFC_TOP] == 1);
    assert(score1.points[OFC_MIDDLE] == 1);
    assert(score1.points[OFC_BOTTOM] == 1);
    assert(score1.scoop_bonus == 3);

    /* Hand 1 should get royalties */
    assert(score1.royalties.top_royalties == 9);    /* AA */
    assert(score1.royalties.middle_royalties == 2); /* Trips */
    assert(score1.royalties.bottom_royalties == 6); /* Full house */

    printf("  ✓ Hand scoring works correctly\n");
}

int main(void)
{
    printf("=== OFC Scoring Tests ===\n\n");

    test_ofc_top_royalties();
    test_ofc_middle_royalties();
    test_ofc_bottom_royalties();
    test_ofc_fantasyland();
    test_ofc_hand_scoring();

    printf("\n✅ All OFC scoring tests passed!\n");
    return 0;
}

