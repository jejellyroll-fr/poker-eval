/*
 * Test suite for Badugi, Badacey, and Badeucy evaluation
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <poker_eval/games/badugi_eval.h>
#include <poker_eval/games/rules_badugi.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_badugi.h>

/* Helper function to create a card mask from individual cards */
static StdDeck_CardMask create_hand(int card1, int card2, int card3, int card4, int card5)
{
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);

    if (card1 >= 0)
        StdDeck_CardMask_SET(hand, card1);
    if (card2 >= 0)
        StdDeck_CardMask_SET(hand, card2);
    if (card3 >= 0)
        StdDeck_CardMask_SET(hand, card3);
    if (card4 >= 0)
        StdDeck_CardMask_SET(hand, card4);
    if (card5 >= 0)
        StdDeck_CardMask_SET(hand, card5);

    return hand;
}

/* Helper function to make a card from rank and suit */
static int make_card(int rank, int suit)
{
    return StdDeck_MAKE_CARD(rank, suit);
}

/* Test basic Badugi evaluation */
static void test_badugi_basic(void)
{
    printf("Testing basic Badugi evaluation...\n");

    /* Perfect Badugi: A♠ 2♥ 3♦ 4♣ */
    StdDeck_CardMask hand1 = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        -1);

    BadugiHandVal val1 = StdDeck_BadugiRules_EVAL_4(hand1);
    assert(BadugiHandVal_HANDTYPE(val1) == BadugiRules_HandType_BADUGI);
    (void)val1; /* Suppress unused variable warning */
    printf("✓ Perfect Badugi (A234) recognized\n");

    /* Three-card hand: A♠ 2♥ 3♦ 3♣ (duplicate rank) */
    StdDeck_CardMask hand2 = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_CLUBS),
        -1);

    BadugiHandVal val2 = StdDeck_BadugiRules_EVAL_4(hand2);
    assert(BadugiHandVal_HANDTYPE(val2) == BadugiRules_HandType_THREE);
    (void)val2; /* Suppress unused variable warning */
    printf("✓ Three-card hand (duplicate rank) recognized\n");

    /* Two-card hand: A♠ 2♥ 3♥ 4♥ (duplicate suits) */
    StdDeck_CardMask hand3 = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_4, StdDeck_Suit_HEARTS),
        -1);

    BadugiHandVal val3 = StdDeck_BadugiRules_EVAL_4(hand3);
    assert(BadugiHandVal_HANDTYPE(val3) == BadugiRules_HandType_TWO);
    (void)val3; /* Suppress unused variable warning */
    printf("✓ Two-card hand (duplicate suits) recognized\n");

    /* One-card hand: A♠ A♥ A♦ A♣ (all same rank) */
    StdDeck_CardMask hand4 = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS),
        -1);

    BadugiHandVal val4 = StdDeck_BadugiRules_EVAL_4(hand4);
    assert(BadugiHandVal_HANDTYPE(val4) == BadugiRules_HandType_ONE);
    (void)val4; /* Suppress unused variable warning */
    printf("✓ One-card hand (all same rank) recognized\n");
}

/* Test Badugi hand comparison */
static void test_badugi_comparison(void)
{
    printf("\nTesting Badugi hand comparison...\n");

    /* A♠ 2♥ 3♦ 4♣ vs A♠ 2♥ 3♦ 5♣ */
    StdDeck_CardMask hand1 = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        -1);

    StdDeck_CardMask hand2 = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_5, StdDeck_Suit_CLUBS),
        -1);

    BadugiHandVal val1 = StdDeck_BadugiRules_EVAL_4(hand1);
    BadugiHandVal val2 = StdDeck_BadugiRules_EVAL_4(hand2);

    /* Lower hand value means better hand in Badugi */
    assert(val1 < val2);
    (void)val1;
    (void)val2; /* Suppress unused variable warnings */
    printf("✓ A234 beats A235\n");

    /* Four-card vs three-card */
    StdDeck_CardMask hand3 = create_hand(
        make_card(StdDeck_Rank_KING, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS),
        -1);

    StdDeck_CardMask hand4 = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_CLUBS),
        -1);

    BadugiHandVal val3 = StdDeck_BadugiRules_EVAL_4(hand3);
    BadugiHandVal val4 = StdDeck_BadugiRules_EVAL_4(hand4);

    assert(val3 < val4);
    (void)val3;
    (void)val4; /* Suppress unused variable warnings */
    printf("✓ Four-card KQJT beats three-card A23\n");
}

/* Test 5-card Badugi (best 4 cards) */
static void test_badugi_5_cards(void)
{
    printf("\nTesting 5-card Badugi evaluation...\n");

    /* A♠ 2♥ 3♦ 4♣ 5♠ - should use A♠ 2♥ 3♦ 4♣ */
    StdDeck_CardMask hand = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        make_card(StdDeck_Rank_5, StdDeck_Suit_SPADES) /* Duplicate suit */
    );

    BadugiHandVal val = StdDeck_BadugiRules_EVAL_5(hand);
    assert(BadugiHandVal_HANDTYPE(val) == BadugiRules_HandType_BADUGI);
    (void)val; /* Suppress unused variable warning */
    printf("✓ 5-card hand correctly evaluates best 4-card Badugi\n");
}

/* Test Badacey evaluation */
static void test_badacey(void)
{
    printf("\nTesting Badacey evaluation...\n");

    /* Perfect Badugi should beat any A-5 lowball */
    StdDeck_CardMask badugi_hand = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        make_card(StdDeck_Rank_5, StdDeck_Suit_SPADES));

    /* No Badugi possible - all same suit */
    StdDeck_CardMask lowball_hand = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_3, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_4, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_5, StdDeck_Suit_SPADES));

    HandVal val1 = StdDeck_BadaceyRules_EVAL_5(badugi_hand);
    HandVal val2 = StdDeck_BadaceyRules_EVAL_5(lowball_hand);

    /* Badugi should beat A-5 lowball */
    assert(HandVal_HANDTYPE(val1) <= BadaceyRules_HandType_ONE);
    assert(HandVal_HANDTYPE(val2) == BadaceyRules_HandType_A5_LOW);
    assert(val1 < val2);
    (void)val1;
    (void)val2; /* Suppress unused variable warnings */
    printf("✓ Badugi beats A-5 lowball in Badacey\n");
}

/* Test Badeucy evaluation */
static void test_badeucy(void)
{
    printf("\nTesting Badeucy evaluation...\n");

    /* Perfect Badugi should beat any 2-7 lowball */
    StdDeck_CardMask badugi_hand = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        make_card(StdDeck_Rank_5, StdDeck_Suit_SPADES));

    /* No Badugi possible - all same suit */
    StdDeck_CardMask lowball_hand = create_hand(
        make_card(StdDeck_Rank_2, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_3, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_4, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_5, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_7, StdDeck_Suit_SPADES));

    HandVal val1 = StdDeck_BadeucyRules_EVAL_5(badugi_hand);
    HandVal val2 = StdDeck_BadeucyRules_EVAL_5(lowball_hand);

    /* Badugi should beat 2-7 lowball */
    assert(HandVal_HANDTYPE(val1) <= BadeucyRules_HandType_ONE);
    assert(HandVal_HANDTYPE(val2) == BadeucyRules_HandType_27_LOW);
    assert(val1 < val2);
    (void)val1;
    (void)val2; /* Suppress unused variable warnings */
    printf("✓ Badugi beats 2-7 lowball in Badeucy\n");
}

/* Test string representation */
static void test_string_representation(void)
{
    printf("\nTesting string representation...\n");

    StdDeck_CardMask hand = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_4, StdDeck_Suit_CLUBS),
        -1);

    BadugiHandVal val = StdDeck_BadugiRules_EVAL_4(hand);
    char str[80];
    BadugiHandVal_toString(val, str);

    printf("✓ Hand string: %s\n", str);

    /* Test hand type names */
    printf("✓ Hand type names:\n");
    for (int i = 0; i < BadugiRules_HandType_COUNT; i++)
    {
        printf("  %s\n", BadugiRules_handTypeNames[i]);
    }
}

/* Test helper functions */
static void test_helper_functions(void)
{
    printf("\nTesting helper functions...\n");

    StdDeck_CardMask hand = create_hand(
        make_card(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        make_card(StdDeck_Rank_2, StdDeck_Suit_HEARTS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS),
        make_card(StdDeck_Rank_3, StdDeck_Suit_CLUBS),
        -1);

    int unique_suits = BadugiEval_CountUniqueSuits(hand, 4);
    int unique_ranks = BadugiEval_CountUniqueRanks(hand, 4);

    assert(unique_suits == 4);
    assert(unique_ranks == 3);
    printf("✓ Unique suits: %d, Unique ranks: %d\n", unique_suits, unique_ranks);

    StdDeck_CardMask best_badugi;
    int badugi_size;
    BadugiEval_GetBestBadugi(hand, 4, &best_badugi, &badugi_size);

    assert(badugi_size == 3);
    printf("✓ Best Badugi size: %d\n", badugi_size);
}

int main(void)
{
    printf("=== Badugi, Badacey, and Badeucy Test Suite ===\n\n");

    test_badugi_basic();
    test_badugi_comparison();
    test_badugi_5_cards();
    test_badacey();
    test_badeucy();
    test_string_representation();
    test_helper_functions();

    printf("\n🎉 All Badugi tests passed!\n");
    printf("\nBadugi implementation features:\n");
    printf("✓ Pure Badugi evaluation (4-card lowball, unique suits and ranks)\n");
    printf("✓ Badacey evaluation (Badugi + A-5 lowball fallback)\n");
    printf("✓ Badeucy evaluation (Badugi + 2-7 lowball fallback)\n");
    printf("✓ 4-card and 5-card hand evaluation\n");
    printf("✓ Proper hand ranking (fewer cards < more cards, lower ranks < higher ranks)\n");
    printf("✓ String representation and debugging functions\n");
    printf("✓ Helper functions for analysis\n");

    return 0;
}
