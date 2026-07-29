#include <unity.h>
#include <poker_eval/equity/flop_equity.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval.h>
#include <string.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper to parse cards */
static StdDeck_CardMask parse_hand(const char *str) {
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);
    /* Simple parser for tests (e.g. "Ah Kh") */
    /* Note: In real code use library parser */
    char s[3];
    int len = strlen(str);
    for (int i=0; i<len; i++) {
        if (str[i] == ' ') continue;
        s[0] = str[i];
        s[1] = str[i+1];
        s[2] = '\0';
        int rank = 0, suit = 0;

        /* Rank */
        switch(s[0]) {
            case 'A': rank=StdDeck_Rank_ACE; break;
            case 'K': rank=StdDeck_Rank_KING; break;
            case 'Q': rank=StdDeck_Rank_QUEEN; break;
            case 'J': rank=StdDeck_Rank_JACK; break;
            case 'T': rank=StdDeck_Rank_TEN; break;
            case '9': rank=StdDeck_Rank_9; break;
            case '8': rank=StdDeck_Rank_8; break;
            case '7': rank=StdDeck_Rank_7; break;
            case '6': rank=StdDeck_Rank_6; break;
            case '5': rank=StdDeck_Rank_5; break;
            case '4': rank=StdDeck_Rank_4; break;
            case '3': rank=StdDeck_Rank_3; break;
            case '2': rank=StdDeck_Rank_2; break;
        }
        /* Suit */
        switch(s[1]) {
            case 'h': suit=StdDeck_Suit_HEARTS; break;
            case 'd': suit=StdDeck_Suit_DIAMONDS; break;
            case 'c': suit=StdDeck_Suit_CLUBS; break;
            case 's': suit=StdDeck_Suit_SPADES; break;
        }

        int card = StdDeck_MAKE_CARD(rank, suit);
        StdDeck_CardMask_SET(mask, card);
        i++;
    }
    return mask;
}

void test_backdoor_flush_detection(void) {
    /* Ah Kd on Qh 7h 2c - 2 hearts on board, 1 in hand (Ah). Total 3. Backdoor Flush. */
    StdDeck_CardMask pocket = parse_hand("Ah Kd");
    StdDeck_CardMask flop = parse_hand("Qh 7h 2c");

    flop_equity_input_t input = { .pocket = pocket, .flop = flop, .n_opponents = 1, .n_samples = 0 };
    flop_equity_result_t result;

    flop_calc_equity(&input, &result);

    TEST_ASSERT_EQUAL_DOUBLE(1.0, result.prob_backdoor_flush);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, result.prob_flush_draw); /* Only 3 cards, not 4 */
}

void test_backdoor_straight_detection(void) {
    /* 5d 6d on Ah 9s 2c - 5,6. Board A,9,2. No relation.
       Wait. 5,6 on board 8. 5,6,8. Gap 5-6 (1), 6-8 (2). 5-8 span 4.
       Backdoor straight: 5-6-8 needs 7,9 or 4,7.
    */
    StdDeck_CardMask pocket = parse_hand("5d 6d");
    StdDeck_CardMask flop = parse_hand("8s Ah 2c"); /* 5,6,8 is 3 to straight */

    flop_equity_input_t input = { .pocket = pocket, .flop = flop, .n_opponents = 1, .n_samples = 0 };
    flop_equity_result_t result;

    flop_calc_equity(&input, &result);

    TEST_ASSERT_EQUAL_DOUBLE(1.0, result.prob_backdoor_straight);
}

void test_oesd_vs_gutshot(void) {
    /* T9 on 8 7 2 - T,9,8,7 connected. OESD. */
    StdDeck_CardMask pocket = parse_hand("Td 9d");
    StdDeck_CardMask flop = parse_hand("8s 7h 2c");

    flop_equity_input_t input = { .pocket = pocket, .flop = flop, .n_opponents = 1, .n_samples = 0 };
    flop_equity_result_t result;

    flop_calc_equity(&input, &result);

    TEST_ASSERT_EQUAL_DOUBLE(1.0, result.prob_oesd);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, result.prob_gutshot);

    /* Gutshot: T9 on Q 8 2 - T,9,8... Q. Gap J. Q,T,9,8 is 4 to straight. Gap. Gutshot. */
    /* Wait. Q-T-9-8. Needs J. 4 outs (J). */
    pocket = parse_hand("Td 9d");
    flop = parse_hand("Qs 8h 2c");
    input.pocket = pocket; input.flop = flop;

    flop_calc_equity(&input, &result);

    TEST_ASSERT_EQUAL_DOUBLE(0.0, result.prob_oesd);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, result.prob_gutshot);
}

void test_showdown_probabilities(void) {
    /* 22 vs Random on A K Q.
       Current: Pair (22).
       Showdown:
       - Pair: High (22 is low pair).
       - Trips (Set): If 2 comes (2 outs).
       - Quads: If 22 comes.
       - Full House: If board pairs or 2 comes and board pairs.
    */
    StdDeck_CardMask pocket = parse_hand("2d 2c");
    StdDeck_CardMask flop = parse_hand("As Ks Qs");

    flop_equity_input_t input = { .pocket = pocket, .flop = flop, .n_opponents = 1, .n_samples = 0 };
    flop_equity_result_t result;

    flop_calc_equity(&input, &result);

    /* Prob Pair should be roughly 1.0 (we have 22) minus improvement?
       Wait. Hand Type logic:
       If we make Trips, HandType is TRIPS. Pair is 0.
       If we make Two Pair, HandType is TWOPAIR. Pair is 0.
       So Prob Pair + Prob TwoPair + Prob Trips... = 1.0.
    */

    /* We start with Pair. */
    /* Chances to improve:
       Set (2 outs x 2 streets approx 8%)
       Two Pair (9 outs (pairs board) x 2 streets approx 30%? No.
       Board A K Q. Any A, K, Q pairs the board. 9 cards.
       If board pairs, we have Two Pair (Board Pair + 22).
       Wait, Board Pair (AA) + 22 is Two Pair.
       So Prob Two Pair should be significant.
    */

    double total = result.prob_pair + result.prob_two_pair + result.prob_trips +
                   result.prob_straight + result.prob_flush + result.prob_full_house +
                   result.prob_quads + result.prob_high_card;

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.0, total);

    TEST_ASSERT_TRUE(result.prob_two_pair > 0.1); /* Board pairing is likely */
    TEST_ASSERT_TRUE(result.prob_trips > 0.0);    /* Hitting a set */
}

void test_variance_calculation(void) {
    /* AK on Q J T - Straight. Equity 100% vs random?
       No, opponent might have split or flush draw. But equity is high.
       Variance should be low if we are nut/near nut?
       Or variance is p(1-p). If p ~ 1, variance ~ 0.
       Let's try a flip. 22 vs AK board.
       Equity ~50%. Variance should be ~0.25 (p*(1-p)).
    */
    /* But we don't control opponent. We calculate Hero vs Random.
       If Hero vs Random is 50%, variance of the RESULT (Win/Loss) is 0.25.
    */

    StdDeck_CardMask pocket = parse_hand("Ah Kh");
    StdDeck_CardMask flop = parse_hand("2d 3c 4s");
    /* AK vs Random on 2 3 4. Hero is Ace high.
       Opponent can hit pair.
    */

    flop_equity_input_t input = { .pocket = pocket, .flop = flop, .n_opponents = 1, .n_samples = 10000 };
    flop_equity_result_t result;

    flop_calc_equity(&input, &result);

    TEST_ASSERT_TRUE(result.variance > 0.0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_backdoor_flush_detection);
    RUN_TEST(test_backdoor_straight_detection);
    RUN_TEST(test_oesd_vs_gutshot);
    RUN_TEST(test_showdown_probabilities);
    RUN_TEST(test_variance_calculation);
    return UNITY_END();
}
