/*
 * test_joker_hands.c - Test joker completing specific hands
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/games/rules_joker.h>
#include <poker_eval/games/eval_joker.h>
#include <poker_eval/games/eval_joker_low.h>
#include <poker_eval/games/rules_std.h>

static void print_hand_value(HandVal val) {
    int type = HandVal_HANDTYPE(val);
    printf("Hand type: ");
    switch(type) {
        case JokerRules_HandType_STFLUSH:  printf("Straight Flush"); break;
        case JokerRules_HandType_QUADS:    printf("Four of a Kind"); break;
        case JokerRules_HandType_FULLHOUSE: printf("Full House"); break;
        case JokerRules_HandType_FLUSH:    printf("Flush"); break;
        case JokerRules_HandType_STRAIGHT: printf("Straight"); break;
        case JokerRules_HandType_TRIPS:    printf("Three of a Kind"); break;
        case JokerRules_HandType_TWOPAIR:  printf("Two Pair"); break;
        case JokerRules_HandType_ONEPAIR:  printf("One Pair"); break;
        case JokerRules_HandType_NOPAIR:   printf("High Card"); break;
        default: printf("Unknown"); break;
    }
    printf("\n");
}

static void test_joker_completes_straight(void) {
    printf("Testing joker completing a straight...\n");
    
    JokerDeck_CardMask hand;
    HandVal val;
    
    // Hand: 2h 3d 4c 5s Xx - joker should make A-5 straight
    JokerDeck_CardMask_RESET(hand);
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_2, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_3, JokerDeck_Suit_DIAMONDS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_4, JokerDeck_Suit_CLUBS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_5, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_JOKER);
    
    val = JokerDeck_JokerRules_EVAL_N(hand, 5);
    printf("  Hand: 2♥ 3♦ 4♣ 5♠ Xx\n  ");
    print_hand_value(val);
    
    assert(HandVal_HANDTYPE(val) == JokerRules_HandType_STRAIGHT);
    printf("  ✓ Joker completes wheel straight (A-2-3-4-5)\n");
    
    // Hand: Th Jh Qh Kh Xx - joker should make royal flush
    JokerDeck_CardMask_RESET(hand);
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_TEN, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_JACK, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_QUEEN, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_JOKER);
    
    val = JokerDeck_JokerRules_EVAL_N(hand, 5);
    printf("\n  Hand: T♥ J♥ Q♥ K♥ Xx\n  ");
    print_hand_value(val);
    
    assert(HandVal_HANDTYPE(val) == JokerRules_HandType_STFLUSH);
    printf("  ✓ Joker completes royal flush\n");
}

static void test_joker_completes_flush(void) {
    printf("\nTesting joker completing a flush...\n");
    
    JokerDeck_CardMask hand;
    HandVal val;
    
    // Hand: 2s 5s 7s 9s Xx - joker should make spade flush
    JokerDeck_CardMask_RESET(hand);
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_2, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_5, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_7, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_9, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_JOKER);
    
    val = JokerDeck_JokerRules_EVAL_N(hand, 5);
    printf("  Hand: 2♠ 5♠ 7♠ 9♠ Xx\n  ");
    print_hand_value(val);
    
    assert(HandVal_HANDTYPE(val) == JokerRules_HandType_FLUSH);
    printf("  ✓ Joker completes flush\n");
}

static void test_joker_makes_quads(void) {
    printf("\nTesting joker making four of a kind...\n");
    
    JokerDeck_CardMask hand;
    HandVal val;
    
    // Hand: As Ah Ad 2c Xx - joker should make quad aces
    JokerDeck_CardMask_RESET(hand);
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_ACE, JokerDeck_Suit_DIAMONDS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_2, JokerDeck_Suit_CLUBS));
    JokerDeck_CardMask_SET(hand, JokerDeck_JOKER);
    
    val = JokerDeck_JokerRules_EVAL_N(hand, 5);
    printf("  Hand: A♠ A♥ A♦ 2♣ Xx\n  ");
    print_hand_value(val);
    
    assert(HandVal_HANDTYPE(val) == JokerRules_HandType_QUADS);
    printf("  ✓ Joker makes four aces\n");
}

static void test_joker_makes_fullhouse(void) {
    printf("\nTesting joker making full house...\n");
    
    JokerDeck_CardMask hand;
    HandVal val;
    
    // Hand: Ks Kh 7d 7c Xx - joker should make KKK77 full house
    JokerDeck_CardMask_RESET(hand);
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_KING, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_7, JokerDeck_Suit_DIAMONDS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_7, JokerDeck_Suit_CLUBS));
    JokerDeck_CardMask_SET(hand, JokerDeck_JOKER);
    
    val = JokerDeck_JokerRules_EVAL_N(hand, 5);
    printf("  Hand: K♠ K♥ 7♦ 7♣ Xx\n  ");
    print_hand_value(val);
    
    assert(HandVal_HANDTYPE(val) == JokerRules_HandType_FULLHOUSE);
    printf("  ✓ Joker makes full house (KKK77)\n");
}

static void test_joker_in_lowball(void) {
    printf("\nTesting joker in lowball context...\n");
    
    JokerDeck_CardMask hand;
    LowHandVal val;
    
    // Hand: 7h 5s 3d 2c Xx - joker should be ace for 7-5-3-2-A
    JokerDeck_CardMask_RESET(hand);
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_7, JokerDeck_Suit_HEARTS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_5, JokerDeck_Suit_SPADES));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_3, JokerDeck_Suit_DIAMONDS));
    JokerDeck_CardMask_SET(hand, JokerDeck_MAKE_CARD(JokerDeck_Rank_2, JokerDeck_Suit_CLUBS));
    JokerDeck_CardMask_SET(hand, JokerDeck_JOKER);
    
    val = JokerDeck_Lowball_EVAL(hand, 5);
    printf("  Hand: 7♥ 5♠ 3♦ 2♣ Xx\n");
    printf("  Lowball hand type: %d\n", LowHandVal_HANDTYPE(val));
    
    // Should be no pair (best low hand)
    assert(LowHandVal_HANDTYPE(val) == StdRules_HandType_NOPAIR);
    printf("  ✓ Joker acts as ace in lowball (7-5-3-2-A)\n");
}

int main(void) {
    printf("=== Joker Hand Completion Tests ===\n\n");
    
    test_joker_completes_straight();
    test_joker_completes_flush();
    test_joker_makes_quads();
    test_joker_makes_fullhouse();
    test_joker_in_lowball();
    
    printf("\n✅ All joker hand tests passed!\n");
    
    return 0;
}
