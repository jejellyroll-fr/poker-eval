#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <poker_eval/equity/flop_equity.h>
#include <poker_eval/deck/deck_std.h>

/* Test 1: Monotone detection */
static void test_monotone_flop(void)
{
    printf("Test 1: Monotone flop (Ah Kh Qh)... ");

    /* Build AhKhQh */
    StdDeck_CardMask flop;
    StdDeck_CardMask_RESET(flop);
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    flop_analysis_t analysis;
    int ret = analyze_flop_texture(flop, &analysis);
    (void)ret; /* Mark as used */

    assert(ret == 0);
    assert(analysis.is_monotone == true);
    assert(analysis.is_two_tone == false);
    assert(analysis.is_rainbow == false);
    assert(analysis.texture == FLOP_TEXTURE_COORDINATED); /* Monotone is considered coordinated */

    printf("PASSED\n");
}

/* Test 2: Rainbow detection */
static void test_rainbow_flop(void)
{
    printf("Test 2: Rainbow flop (As Kd 7c)... ");

    StdDeck_CardMask flop;
    StdDeck_CardMask_RESET(flop);
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));

    flop_analysis_t analysis;
    analyze_flop_texture(flop, &analysis);

    assert(analysis.is_rainbow == true);
    assert(analysis.is_monotone == false);
    assert(analysis.texture == FLOP_TEXTURE_DRY);

    printf("PASSED\n");
}

/* Test 3: Paired flop */
static void test_paired_flop(void)
{
    printf("Test 3: Paired flop (Kh Kd 7s)... ");

    StdDeck_CardMask flop;
    StdDeck_CardMask_RESET(flop);
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));

    flop_analysis_t analysis;
    analyze_flop_texture(flop, &analysis);

    assert(analysis.is_paired == true);
    assert(analysis.is_trips == false);
    assert(analysis.texture == FLOP_TEXTURE_PAIRED);

    printf("PASSED\n");
}

/* Test 4: Trips flop */
static void test_trips_flop(void)
{
    printf("Test 4: Trips flop (Kh Kd Ks)... ");

    StdDeck_CardMask flop;
    StdDeck_CardMask_RESET(flop);
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    flop_analysis_t analysis;
    analyze_flop_texture(flop, &analysis);

    assert(analysis.is_trips == true);
    assert(analysis.is_paired == true);
    assert(analysis.texture == FLOP_TEXTURE_TRIPS);

    printf("PASSED\n");
}

/* Test 5: Connected flop */
static void test_connected_flop(void)
{
    printf("Test 5: Connected flop (9h 8d 7c)... ");

    StdDeck_CardMask flop;
    StdDeck_CardMask_RESET(flop);
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));

    flop_analysis_t analysis;
    analyze_flop_texture(flop, &analysis);

    assert(analysis.is_connected == true);
    assert(analysis.has_oesd == true);
    assert(analysis.texture == FLOP_TEXTURE_WET);

    printf("PASSED\n");
}

/* Test 6: Wheel wrap around */
static void test_wheel_flop(void)
{
    printf("Test 6: Wheel wrap around (Ah 2d 3c)... ");

    StdDeck_CardMask flop;
    StdDeck_CardMask_RESET(flop);
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(flop, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));

    flop_analysis_t analysis;
    analyze_flop_texture(flop, &analysis);

    assert(analysis.is_connected == true);
    assert(analysis.max_gap == 1);
    assert(analysis.texture == FLOP_TEXTURE_WET);

    printf("PASSED\n");
}

int main(void)
{
    printf("=== Flop Texture Tests ===\n\n");

    test_monotone_flop();
    test_rainbow_flop();
    test_paired_flop();
    test_trips_flop();
    test_connected_flop();
    test_wheel_flop();

    printf("\n✅ All flop texture tests PASSED (6/6)\n");
    return 0;
}
