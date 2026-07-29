/*
 * test_ofc_basic.c - Basic OFC functionality tests
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

static void test_ofc_initialization(void) {
    UNUSED ofc_hand_t hand;

    printf("Testing OFC hand initialization...\n");

    assert(OFC_InitializeHand(&hand) == 0);

    /* Verify all hands are empty */
    assert(hand.card_count[OFC_TOP] == 0);
    assert(hand.card_count[OFC_MIDDLE] == 0);
    assert(hand.card_count[OFC_BOTTOM] == 0);

    /* Verify flags are reset */
    assert(hand.is_foul == 0);
    assert(hand.is_fantasyland == 0);
    assert(hand.stay_fantasyland == 0);

    printf("  ✓ Hand initialization works correctly\n");
}

static void test_ofc_place_card(void) {
    ofc_hand_t hand;
    UNUSED int ace_spades = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    UNUSED int king_hearts = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    printf("Testing OFC card placement...\n");

    OFC_InitializeHand(&hand);

    /* Place card in top */
    assert(OFC_PlaceCard(&hand, ace_spades, OFC_TOP) == 0);
    assert(hand.card_count[OFC_TOP] == 1);
    assert(StdDeck_CardMask_CARD_IS_SET(hand.hands[OFC_TOP], ace_spades));

    /* Place card in middle */
    assert(OFC_PlaceCard(&hand, king_hearts, OFC_MIDDLE) == 0);
    assert(hand.card_count[OFC_MIDDLE] == 1);
    assert(StdDeck_CardMask_CARD_IS_SET(hand.hands[OFC_MIDDLE], king_hearts));

    printf("  ✓ Card placement works correctly\n");
}

static void test_ofc_overfill_protection(void) {
    ofc_hand_t hand;
    UNUSED int cards[4];
    int i;

    printf("Testing OFC overfill protection...\n");

    /* Create some test cards */
    for (i = 0; i < 4; i++) {
        cards[i] = StdDeck_MAKE_CARD(i % StdDeck_Rank_COUNT, i % StdDeck_Suit_COUNT);
    }

    OFC_InitializeHand(&hand);

    /* Fill top hand (3 cards max) */
    for (i = 0; i < 3; i++) {
        assert(OFC_PlaceCard(&hand, cards[i], OFC_TOP) == 0);
    }

    /* Try to overfill top hand */
    assert(OFC_PlaceCard(&hand, cards[3], OFC_TOP) == -1);  /* Should fail */
    assert(hand.card_count[OFC_TOP] == 3);  /* Should still be 3 */

    printf("  ✓ Overfill protection works correctly\n");
}

static void test_ofc_completion_check(void) {
    ofc_hand_t hand;
    int cards[13];
    int i;

    printf("Testing OFC completion check...\n");

    /* Create test cards */
    for (i = 0; i < 13; i++) {
        cards[i] = StdDeck_MAKE_CARD(i % StdDeck_Rank_COUNT, i % StdDeck_Suit_COUNT);
    }

    OFC_InitializeHand(&hand);

    /* Hand should not be complete initially */
    assert(OFC_IsComplete(&hand) == 0);

    /* Fill hands partially */
    OFC_PlaceCard(&hand, cards[0], OFC_TOP);
    assert(OFC_IsComplete(&hand) == 0);

    /* Fill top completely */
    OFC_PlaceCard(&hand, cards[1], OFC_TOP);
    OFC_PlaceCard(&hand, cards[2], OFC_TOP);
    assert(OFC_IsComplete(&hand) == 0);

    /* Fill middle completely */
    for (i = 3; i < 8; i++) {
        OFC_PlaceCard(&hand, cards[i], OFC_MIDDLE);
    }
    assert(OFC_IsComplete(&hand) == 0);

    /* Fill bottom completely */
    for (i = 8; i < 13; i++) {
        OFC_PlaceCard(&hand, cards[i], OFC_BOTTOM);
    }

    /* Now hand should be complete */
    assert(OFC_IsComplete(&hand) == 1);

    printf("  ✓ Completion check works correctly\n");
}

static void test_ofc_position_strings(void) {
    printf("Testing OFC position strings...\n");

    assert(strcmp(OFC_PositionToString(OFC_TOP), "Top") == 0);
    assert(strcmp(OFC_PositionToString(OFC_MIDDLE), "Middle") == 0);
    assert(strcmp(OFC_PositionToString(OFC_BOTTOM), "Bottom") == 0);

    printf("  ✓ Position strings work correctly\n");
}

int main(void) {
    printf("=== OFC Basic Tests ===\n\n");

    test_ofc_initialization();
    test_ofc_place_card();
    test_ofc_overfill_protection();
    test_ofc_completion_check();
    test_ofc_position_strings();

    printf("\n✅ All OFC basic tests passed!\n");
    return 0;
}
