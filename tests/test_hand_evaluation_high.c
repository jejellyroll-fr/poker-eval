#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/core/eval.h>

/* Function prototypes */
static StdDeck_CardMask create_mask_from_strings(const char *card_strings[], int num_cards);
static void test_high_card(void);
static void test_one_pair(void);
static void test_two_pair(void);
static void test_three_of_a_kind(void);
static void test_straight(void);
static void test_flush(void);
static void test_full_house(void);
static void test_four_of_a_kind(void);
static void test_straight_flush(void);

// Helper function to create a card mask from an array of card strings
static StdDeck_CardMask create_mask_from_strings(const char *card_strings[], int num_cards)
{
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);
    for (int i = 0; i < num_cards; ++i)
    {
        int card_idx;
        char card_str[4];
        strcpy(card_str, card_strings[i]); // Copy to non-const string
        int result = StdDeck_stringToCard(card_str, &card_idx);
        assert(result == 2); // Ensure card string is valid
        StdDeck_CardMask_SET(mask, card_idx);
        (void)result; // Suppress unused variable warning
    }
    return mask;
}

// Test for High Card
static void test_high_card(void)
{
    const char *cards[] = {"As", "Kd", "Qh", "Jc", "9s", "2d", "3h"};
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_NOPAIR);
    // Expected: A, K, Q, J, 9
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE);
    assert(HandVal_SECOND_CARD(val) == StdDeck_Rank_KING);
    assert(HandVal_THIRD_CARD(val) == StdDeck_Rank_QUEEN);
    assert(HandVal_FOURTH_CARD(val) == StdDeck_Rank_JACK);
    assert(HandVal_FIFTH_CARD(val) == StdDeck_Rank_9);
    (void)val; // Suppress unused variable warning
}

// Test for One Pair
static void test_one_pair(void)
{
    const char *cards[] = {"As", "Ad", "Qh", "Jc", "9s", "2d", "3h"};
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_ONEPAIR);
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE); // Pair of Aces
    // Kickers: Q, J, 9
    assert(HandVal_SECOND_CARD(val) == StdDeck_Rank_QUEEN);
    assert(HandVal_THIRD_CARD(val) == StdDeck_Rank_JACK);
    assert(HandVal_FOURTH_CARD(val) == StdDeck_Rank_9);
    (void)val; // Suppress unused variable warning
}

// Test for Two Pair
static void test_two_pair(void)
{
    const char *cards[] = {"As", "Ad", "Kh", "Kc", "9s", "2d", "3h"};
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_TWOPAIR);
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE);     // Top pair Aces
    assert(HandVal_SECOND_CARD(val) == StdDeck_Rank_KING); // Second pair Kings
    assert(HandVal_THIRD_CARD(val) == StdDeck_Rank_9);     // Kicker 9
    (void)val;                                             // Suppress unused variable warning
}

// Test for Three of a Kind
static void test_three_of_a_kind(void)
{
    const char *cards[] = {"As", "Ad", "Ah", "Kc", "9s", "2d", "3h"};
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_TRIPS);
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE); // Trips Aces
    // Kickers: K, 9
    assert(HandVal_SECOND_CARD(val) == StdDeck_Rank_KING);
    assert(HandVal_THIRD_CARD(val) == StdDeck_Rank_9);
    (void)val; // Suppress unused variable warning
}

// Test for Straight
static void test_straight(void)
{
    const char *cards[] = {"As", "Kd", "Qh", "Jc", "Ts", "2d", "3h"}; // A-T straight
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_STRAIGHT);
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE);
    (void)val; // Suppress unused variable warning

    const char *cards2[] = {"5s", "4d", "3h", "2c", "As", "Kd", "Qh"}; // 5-A wheel
    StdDeck_CardMask mask2 = create_mask_from_strings(cards2, 7);
    HandVal val2 = StdDeck_StdRules_EVAL_N(mask2, 7);
    assert(HandVal_HANDTYPE(val2) == StdRules_HandType_STRAIGHT);
    assert(HandVal_TOP_CARD(val2) == StdDeck_Rank_5);
    (void)val2; // Suppress unused variable warning
}

// Test for Flush
static void test_flush(void)
{
    const char *cards[] = {"As", "Ks", "Qs", "Js", "9s", "2d", "3h"}; // Ace high spade flush
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_FLUSH);
    // Expected: A, K, Q, J, 9 (all spades)
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE);
    assert(HandVal_SECOND_CARD(val) == StdDeck_Rank_KING);
    assert(HandVal_THIRD_CARD(val) == StdDeck_Rank_QUEEN);
    assert(HandVal_FOURTH_CARD(val) == StdDeck_Rank_JACK);
    assert(HandVal_FIFTH_CARD(val) == StdDeck_Rank_9);
    (void)val; // Suppress unused variable warning
}

// Test for Full House
static void test_full_house(void)
{
    const char *cards[] = {"As", "Ad", "Ah", "Kc", "Ks", "2d", "3h"}; // Aces full of Kings
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_FULLHOUSE);
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE);     // Trips Aces
    assert(HandVal_SECOND_CARD(val) == StdDeck_Rank_KING); // Pair Kings
    (void)val;                                             // Suppress unused variable warning
}

// Test for Four of a Kind
static void test_four_of_a_kind(void)
{
    const char *cards[] = {"As", "Ad", "Ah", "Ac", "Ks", "2d", "3h"}; // Quads Aces
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_QUADS);
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE);     // Quads Aces
    assert(HandVal_SECOND_CARD(val) == StdDeck_Rank_KING); // Kicker King
    (void)val;                                             // Suppress unused variable warning
}

// Test for Straight Flush
static void test_straight_flush(void)
{
    const char *cards[] = {"As", "Ks", "Qs", "Js", "Ts", "2d", "3h"}; // Royal flush (spades)
    StdDeck_CardMask mask = create_mask_from_strings(cards, 7);
    HandVal val = StdDeck_StdRules_EVAL_N(mask, 7);
    assert(HandVal_HANDTYPE(val) == StdRules_HandType_STFLUSH);
    assert(HandVal_TOP_CARD(val) == StdDeck_Rank_ACE);
    (void)val; // Suppress unused variable warning

    const char *cards2[] = {"9s", "8s", "7s", "6s", "5s", "Ad", "Kh"}; // 9-high straight flush (spades)
    StdDeck_CardMask mask2 = create_mask_from_strings(cards2, 7);
    HandVal val2 = StdDeck_StdRules_EVAL_N(mask2, 7);
    assert(HandVal_HANDTYPE(val2) == StdRules_HandType_STFLUSH);
    assert(HandVal_TOP_CARD(val2) == StdDeck_Rank_9);
    (void)val2; // Suppress unused variable warning
}

int main(void)
{
    test_high_card();
    test_one_pair();
    test_two_pair();
    test_three_of_a_kind();
    test_straight();
    test_flush();
    test_full_house();
    test_four_of_a_kind();
    test_straight_flush();

    printf("All hand evaluation (High) tests passed.\n");
    return 0;
}
