#include <stdio.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/deck/deck_std.h>

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while (0)

static StdDeck_CardMask make_hand(const int ranks[], const int suits[], int count) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    for (int i = 0; i < count; ++i) {
        StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(ranks[i], suits[i]));
    }
    return hand;
}

int main(void) {
    {
        int ranks[] = {StdDeck_Rank_ACE, StdDeck_Rank_2, StdDeck_Rank_3, StdDeck_Rank_4, StdDeck_Rank_5};
        int suits[] = {StdDeck_Suit_SPADES, StdDeck_Suit_CLUBS, StdDeck_Suit_HEARTS, StdDeck_Suit_DIAMONDS, StdDeck_Suit_CLUBS};
        StdDeck_CardMask wheel = make_hand(ranks, suits, 5);

        int ranks2[] = {StdDeck_Rank_ACE, StdDeck_Rank_2, StdDeck_Rank_3, StdDeck_Rank_4, StdDeck_Rank_6};
        StdDeck_CardMask six_high = make_hand(ranks2, suits, 5);

        LowHandVal hv_wheel = pe_eval_low_a5(wheel);
        LowHandVal hv_six = pe_eval_low_a5(six_high);
        ASSERT_TRUE(hv_wheel < hv_six, "Wheel should beat six-low in A-5");
    }

    {
        int ranks_good[] = {StdDeck_Rank_7, StdDeck_Rank_5, StdDeck_Rank_4, StdDeck_Rank_3, StdDeck_Rank_2};
        int suits_good[] = {StdDeck_Suit_SPADES, StdDeck_Suit_CLUBS, StdDeck_Suit_HEARTS, StdDeck_Suit_DIAMONDS, StdDeck_Suit_CLUBS};
        StdDeck_CardMask best = make_hand(ranks_good, suits_good, 5);

        int ranks_bad[] = {StdDeck_Rank_9, StdDeck_Rank_7, StdDeck_Rank_5, StdDeck_Rank_4, StdDeck_Rank_3};
        StdDeck_CardMask worse = make_hand(ranks_bad, suits_good, 5);

        LowHandVal hv_best = pe_eval_low_27(best);
        LowHandVal hv_worse = pe_eval_low_27(worse);
        ASSERT_TRUE(hv_best < hv_worse, "Seven-five low should beat nine-seven low in 2-7");
    }

    {
        StdDeck_CardMask short_hand;
        StdDeck_CardMask_RESET(short_hand);
        StdDeck_CardMask_SET(short_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
        StdDeck_CardMask_SET(short_hand, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
        LowHandVal hv = pe_eval_low_a5(short_hand);
        ASSERT_TRUE(hv == LowHandVal_NOTHING, "Insufficient cards should return LowHandVal_NOTHING");
    }

    printf("Low evaluation tests passed.\n");
    return 0;
}
