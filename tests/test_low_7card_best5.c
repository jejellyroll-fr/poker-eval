/*
 * Regression tests for the ace-to-five low evaluator on more than five cards.
 *
 * StdDeck_Lowball_EVAL used to give up on a "no pair" low as soon as any rank
 * appeared twice, so a stud hand like 4-5-5-6-6-7-8 was scored as a pair
 * instead of 8-7-6-5-4.  It also packed the five selected cards lowest-first
 * while LowHandVal expects the highest card in TOP_CARD, which reversed the
 * comparison between two lows sharing their bottom cards.  On top of that,
 * pe_low_qualify5 only looked at TOP_CARD, so a pair of deuces passed the
 * 8-or-better qualifier.
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_low.h>
#include <poker_eval/games/eval_low8.h>

#define ASSERT_TRUE(cond, msg)                             \
    do {                                                   \
        if (!(cond)) {                                     \
            fprintf(stderr, "Assertion failed: %s\n", msg); \
            return 1;                                      \
        }                                                  \
    } while (0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

static StdDeck_CardMask hand_from_string(const char *cards[], int count) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    for (int i = 0; i < count; ++i) {
        char card_text[3] = {cards[i][0], cards[i][1], '\0'};
        int index;
        StdDeck_stringToCard(card_text, &index);
        StdDeck_CardMask_SET(hand, index);
    }
    return hand;
}

/* Best five-of-n by brute force, using the five-card evaluator as reference. */
static LowHandVal brute_force_low_a5(const int cards[], int n) {
    LowHandVal best = 0xFFFFFFFF;
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b)
            for (int c = b + 1; c < n; ++c)
                for (int d = c + 1; d < n; ++d)
                    for (int e = d + 1; e < n; ++e) {
                        StdDeck_CardMask five;
                        StdDeck_CardMask_RESET(five);
                        StdDeck_CardMask_SET(five, cards[a]);
                        StdDeck_CardMask_SET(five, cards[b]);
                        StdDeck_CardMask_SET(five, cards[c]);
                        StdDeck_CardMask_SET(five, cards[d]);
                        StdDeck_CardMask_SET(five, cards[e]);
                        LowHandVal value = StdDeck_Lowball_EVAL(five, 5);
                        if (value < best)
                            best = value;
                    }
    return best;
}

int main(void) {
    /* The showdown that started this: seven-card stud hi/lo, hand 2780605304.
       Montlebon holds a qualifying 8-7-6-5-4 despite two pairs among his seven
       cards; his opponent has only four distinct low ranks and does not
       qualify at all. */
    {
        const char *montlebon_cards[] = {"7h", "6s", "8h", "4c", "5c", "5s", "6d"};
        const char *opponent_cards[] = {"Kd", "6h", "Ah", "Kc", "2s", "3h", "2h"};
        StdDeck_CardMask montlebon = hand_from_string(montlebon_cards, 7);
        StdDeck_CardMask opponent = hand_from_string(opponent_cards, 7);

        const char *best_five_cards[] = {"4c", "5c", "6s", "7h", "8h"};
        StdDeck_CardMask best_five = hand_from_string(best_five_cards, 5);

        ASSERT_TRUE(pe_eval_low_a5(montlebon) == StdDeck_Lowball_EVAL(best_five, 5),
                    "seven cards with two pairs still play their best five");
        ASSERT_TRUE(pe_low_qualify5(pe_eval_low_a5(montlebon), LOW_QUALIFIER_8),
                    "8-7-6-5-4 qualifies 8-or-better");
        ASSERT_FALSE(pe_low_qualify5(pe_eval_low_a5(opponent), LOW_QUALIFIER_8),
                     "four distinct low ranks never qualify 8-or-better");
        ASSERT_TRUE(StdDeck_Lowball8_EVAL(opponent, 7) == LowHandVal_NOTHING,
                    "the 8-or-better evaluator agrees the opponent has no low");
        ASSERT_TRUE(pe_eval_low_a5(montlebon) < pe_eval_low_a5(opponent),
                    "the qualifying low beats the paired one");
    }

    /* Comparison is driven by the highest card first: A-2-3-5-6 beats
       A-2-3-4-8 even though its fourth card is higher. */
    {
        const char *lower_top_cards[] = {"Ah", "2c", "3d", "5s", "6h", "Kd", "Kc"};
        const char *higher_top_cards[] = {"Ah", "2c", "3d", "4s", "8h", "Kd", "Kc"};
        StdDeck_CardMask lower_top = hand_from_string(lower_top_cards, 7);
        StdDeck_CardMask higher_top = hand_from_string(higher_top_cards, 7);

        ASSERT_TRUE(pe_eval_low_a5(lower_top) < pe_eval_low_a5(higher_top),
                    "6-5-3-2-A beats 8-4-3-2-A");
    }

    /* A hand with fewer than five distinct ranks is a paired low, and is
       rejected by every qualifier. */
    {
        const char *five_distinct_cards[] = {"2s", "2h", "3h", "3d", "4c", "5h", "6d"};
        StdDeck_CardMask five_distinct = hand_from_string(five_distinct_cards, 7);
        LowHandVal value = pe_eval_low_a5(five_distinct);

        ASSERT_TRUE(LowHandVal_HANDTYPE(value) == StdRules_HandType_NOPAIR,
                    "five distinct ranks make a no-pair low whatever the duplicates");
        ASSERT_TRUE(pe_low_qualify5(value, LOW_QUALIFIER_8),
                    "6-5-4-3-2 qualifies 8-or-better");

        const char *four_distinct_cards[] = {"2s", "2h", "3h", "3d", "4c", "4s", "5h"};
        StdDeck_CardMask four_distinct = hand_from_string(four_distinct_cards, 7);
        LowHandVal short_value = pe_eval_low_a5(four_distinct);

        ASSERT_TRUE(LowHandVal_HANDTYPE(short_value) != StdRules_HandType_NOPAIR,
                    "four distinct ranks cannot make a no-pair low");
        ASSERT_FALSE(pe_low_qualify5(short_value, LOW_QUALIFIER_8),
                     "a paired low never qualifies 8-or-better");
    }

    /* Differential check against brute force over every five-card subset. */
    {
        srandom(20260802);
        for (int iteration = 0; iteration < 20000; ++iteration) {
            int cards[7];
            StdDeck_CardMask hand;
            StdDeck_CardMask_RESET(hand);
            for (int i = 0; i < 7;) {
                int card = (int)(random() % StdDeck_N_CARDS);
                if (StdDeck_CardMask_CARD_IS_SET(hand, card))
                    continue;
                StdDeck_CardMask_SET(hand, card);
                cards[i++] = card;
            }

            LowHandVal expected = brute_force_low_a5(cards, 7);
            if (pe_eval_low_a5(hand) != expected) {
                fprintf(stderr,
                        "seven-card low %u does not match brute force %u\n",
                        pe_eval_low_a5(hand), expected);
                return 1;
            }

            /* The 8-or-better qualifier must agree with the dedicated
               8-or-better evaluator on every hand. */
            int qualifies = pe_low_qualify5(pe_eval_low_a5(hand), LOW_QUALIFIER_8);
            int has_low8 = StdDeck_Lowball8_EVAL(hand, 7) != LowHandVal_NOTHING;
            if (qualifies != has_low8) {
                fprintf(stderr, "qualifier and 8-or-better evaluator disagree\n");
                return 1;
            }
        }
    }

    printf("Seven-card low evaluation tests passed.\n");
    return 0;
}
