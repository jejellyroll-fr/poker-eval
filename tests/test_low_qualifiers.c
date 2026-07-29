#include <stdio.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/deck/deck_std.h>

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while (0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

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
        int ranks[] = {
            StdDeck_Rank_ACE,
            StdDeck_Rank_2,
            StdDeck_Rank_3,
            StdDeck_Rank_4,
            StdDeck_Rank_8
        };
        int suits[] = {
            StdDeck_Suit_SPADES,
            StdDeck_Suit_HEARTS,
            StdDeck_Suit_DIAMONDS,
            StdDeck_Suit_CLUBS,
            StdDeck_Suit_SPADES
        };
        StdDeck_CardMask hand = make_hand(ranks, suits, 5);
        LowHandVal hv = pe_eval_low_a5(hand);
        ASSERT_TRUE(pe_low_qualify5(hv, LOW_QUALIFIER_8), "A2348 qualifies 8-or-better");
        ASSERT_FALSE(pe_low_qualify5(hv, LOW_QUALIFIER_7), "A2348 does not qualify 7-or-better");
    }

    {
        int ranks[] = {
            StdDeck_Rank_ACE,
            StdDeck_Rank_2,
            StdDeck_Rank_3,
            StdDeck_Rank_4,
            StdDeck_Rank_9
        };
        int suits[] = {
            StdDeck_Suit_SPADES,
            StdDeck_Suit_HEARTS,
            StdDeck_Suit_DIAMONDS,
            StdDeck_Suit_CLUBS,
            StdDeck_Suit_SPADES
        };
        StdDeck_CardMask hand = make_hand(ranks, suits, 5);
        LowHandVal hv = pe_eval_low_a5(hand);
        ASSERT_FALSE(pe_low_qualify5(hv, LOW_QUALIFIER_8), "A2349 does not qualify 8-or-better");
    }

    {
        StdDeck_CardMask hand;
        StdDeck_CardMask_RESET(hand);
        StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
        StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
        StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));
        StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));
        LowHandVal hv = pe_eval_low_a5(hand);
        ASSERT_FALSE(pe_low_qualify5(hv, LOW_QUALIFIER_NONE), "Incomplete hand should not qualify");
    }

    printf("Low qualifier tests passed.\n");
    return 0;
}
