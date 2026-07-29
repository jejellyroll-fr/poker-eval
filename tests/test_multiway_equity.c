#include <stdio.h>
#include <math.h>
#include <string.h>

#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumerate.h>

#define ASSERT_NEAR(value, expected, tol, msg) \
    if (fabs((value) - (expected)) > (tol)) { \
        fprintf(stderr, "Assertion failed: %s (got %.6f, expected %.6f)\n", msg, (value), (expected)); \
        return 1; \
    }

static int test_simple_sidepots(void) {
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));

    StdDeck_CardMask hero[1];
    StdDeck_CardMask_RESET(hero[0]);
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    StdDeck_CardMask villain1[1];
    StdDeck_CardMask_RESET(villain1[0]);
    StdDeck_CardMask_SET(villain1[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(villain1[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask villain2[1];
    StdDeck_CardMask_RESET(villain2[0]);
    StdDeck_CardMask_SET(villain2[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(villain2[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    PlayerRange ranges[3] = {
        {hero, NULL, 1, 1.0},
        {villain1, NULL, 1, 1.0},
        {villain2, NULL, 1, 1.0}
    };

    double stacks[3] = {0.0, 0.0, 0.0};
    double invested[3] = {100.0, 80.0, 60.0};

    MultiwayPotState state = {ranges, stacks, invested, 3};
    MultiwayEquityOptions options = {0};
    MultiwayEquityResult result;
    memset(&result, 0, sizeof(result));

    int matchups = CalculateMultiwayEquity(
        game_holdem, &state, board, dead,
        0, &options, &result);

    if (matchups <= 0) {
        fprintf(stderr, "Expected matchups > 0\n");
        return 1;
    }

    // Pot is 100 + 80 + 60 = 240 total?
    // Hero (P0) wins 220:
    // - Main pot: 60 * 3 = 180 (all in)
    // - Side pot 1: (80-60) * 2 = 40 (P0 vs P1)
    // - Side pot 2: (100-80) = 20 (P0 gets returned or P0 vs P2?)
    // Wait, invested: P0=100, P1=80, P2=60
    // P0 (Hero) covers everyone. P2 is shortest. P1 is middle.
    // P2 is all-in for 60. P2, P1, P0 match 60. Main Pot = 180.
    // P1 is all-in for 20 more (80 total). P1, P0 match 20. Side Pot 1 = 40.
    // P0 has 20 left (100 total). Returned to P0 immediately? Or Side Pot 2 = 20 (uncontested)?
    // If uncontested, EV counts it?
    // sidepots.c likely ignores uncontested portion in pot creation or returns it.
    // Let's verify standard sidepot logic.
    // If P0 has the nuts, they win Main (180) + Side1 (40) + Side2 (20 returned) = 240?
    // Or Side2 is not a pot.
    // If P0 invested 100, P1 80, P2 60.
    // If P0 wins everything:
    // Gains 60 from P1, 60 from P2. + own 100?
    // EV usually includes own stack if we look at final stack? Or just pot winnings?
    // Usually EV = Pot Equity * Pot Size.
    // Here, P0 wins 100% of Main (180) + 100% of Side1 (40). Total 220.
    // Plus 20 returned uncalled bet.
    // If result.ev includes returned bet, it should be 240.
    // If result.ev is just pot winnings, it is 220.
    // The previous failure said "got 0.000000". This means calculation failed or weights/ranges issue.
    // Wait, result.ev is 0.0?
    // "Assertion failed: EV player 0 (got 0.000000, expected 220.000000)"
    // This means CalculateMultiwayEquity returned > 0 (matchups > 0) but EV is 0.
    // Likely because P0 hands are invalid or lost?
    // Hero has AA. Board 2 3 4 5 9 (rainbow/suited?). Board has 3 clubs?
    // Board: 2c 3d 4h 5s 9c. Rainbow-ish.
    // Hero: As Ah. Pair of Aces.
    // Villain1: Kc Kd. Pair of Kings.
    // Villain2: Qs Qh. Pair of Queens.
    // Hero should win.

    ASSERT_NEAR(result.ev[0], 220.0, 2.0, "EV player 0");
    ASSERT_NEAR(result.ev[1], 0.0, 1e-6, "EV player 1");
    ASSERT_NEAR(result.ev[2], 0.0, 1e-6, "EV player 2");

    ASSERT_NEAR(result.equity[0], 1.0, 1e-6, "Equity player 0");
    ASSERT_NEAR(result.equity[1], 0.0, 1e-6, "Equity player 1");
    ASSERT_NEAR(result.equity[2], 0.0, 1e-6, "Equity player 2");

    ASSERT_NEAR(result.win_prob[0], 1.0, 1e-6, "Win prob player 0");
    ASSERT_NEAR(result.tie_prob[0], 0.0, 1e-6, "Tie prob player 0");
    return 0;
}

static int test_split_pot_exhaustive(void) {
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_CLUBS));

    StdDeck_CardMask hero[1];
    StdDeck_CardMask_RESET(hero[0]);
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    StdDeck_CardMask villain[1];
    StdDeck_CardMask_RESET(villain[0]);
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    PlayerRange ranges[2] = {
        {hero, NULL, 1, 1.0},
        {villain, NULL, 1, 1.0}
    };

    double stacks[2] = {0.0, 0.0};
    double invested[2] = {100.0, 100.0};

    MultiwayPotState state = {ranges, stacks, invested, 2};
    MultiwayEquityOptions options = {0};
    MultiwayEquityResult result;
    memset(&result, 0, sizeof(result));

    int matchups = CalculateMultiwayEquity(
        game_holdem, &state, board, dead,
        0, &options, &result);

    if (matchups <= 0) {
        fprintf(stderr, "Expected matchups > 0\n");
        return 1;
    }

    ASSERT_NEAR(result.ev[0], 100.0, 1e-6, "Split EV player 0");
    ASSERT_NEAR(result.ev[1], 100.0, 1e-6, "Split EV player 1");
    ASSERT_NEAR(result.tie_prob[0], 1.0, 1e-6, "Tie prob hero");
    ASSERT_NEAR(result.tie_prob[1], 1.0, 1e-6, "Tie prob villain");
    return 0;
}

static int test_holdem8_scoop(void) {
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask hero[1];
    StdDeck_CardMask_RESET(hero[0]);
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS));

    StdDeck_CardMask villain[1];
    StdDeck_CardMask_RESET(villain[0]);
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    PlayerRange ranges[2] = {
        {hero, NULL, 1, 1.0},
        {villain, NULL, 1, 1.0}
    };

    double stacks[2] = {0.0, 0.0};
    double invested[2] = {100.0, 100.0};

    MultiwayPotState state = {ranges, stacks, invested, 2};
    MultiwayEquityOptions options = {0};
    MultiwayEquityResult result;
    memset(&result, 0, sizeof(result));

    int matchups = CalculateMultiwayEquity(
        game_holdem8, &state, board, dead,
        0, &options, &result);

    if (matchups <= 0) {
        fprintf(stderr, "Expected matchups > 0 for scoop test\n");
        return 1;
    }

    ASSERT_NEAR(result.ev[0], 200.0, 1e-6, "Scoop EV hero");
    ASSERT_NEAR(result.ev[1], 0.0, 1e-6, "Scoop EV villain");
    ASSERT_NEAR(result.equity[0], 1.0, 1e-6, "Scoop equity hero");
    ASSERT_NEAR(result.equity[1], 0.0, 1e-6, "Scoop equity villain");
    ASSERT_NEAR(result.win_prob[0], 1.0, 1e-6, "Hero high win prob");
    ASSERT_NEAR(result.tie_prob[0], 0.0, 1e-6, "Hero high tie prob");
    return 0;
}

static int test_holdem8_quartering(void) {
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask hero[1];
    StdDeck_CardMask_RESET(hero[0]);
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));

    StdDeck_CardMask villain[1];
    StdDeck_CardMask_RESET(villain[0]);
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));

    PlayerRange ranges[2] = {
        {hero, NULL, 1, 1.0},
        {villain, NULL, 1, 1.0}
    };

    double stacks[2] = {0.0, 0.0};
    double invested[2] = {100.0, 100.0};

    MultiwayPotState state = {ranges, stacks, invested, 2};
    MultiwayEquityOptions options = {0};
    MultiwayEquityResult result;
    memset(&result, 0, sizeof(result));

    int matchups = CalculateMultiwayEquity(
        game_holdem8, &state, board, dead,
        0, &options, &result);

    if (matchups <= 0) {
        fprintf(stderr, "Expected matchups > 0 for quartering test\n");
        return 1;
    }

    ASSERT_NEAR(result.ev[0], 150.0, 1e-6, "Quartering EV hero");
    ASSERT_NEAR(result.ev[1], 50.0, 1e-6, "Quartering EV villain");
    ASSERT_NEAR(result.equity[0], 0.75, 1e-6, "Quartering equity hero");
    ASSERT_NEAR(result.equity[1], 0.25, 1e-6, "Quartering equity villain");
    ASSERT_NEAR(result.win_prob[0], 1.0, 1e-6, "Hero high win prob (quartering)");
    ASSERT_NEAR(result.tie_prob[0], 0.0, 1e-6, "Hero high tie prob (quartering)");
    return 0;
}

static int test_weighted_monte_carlo(void) {
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    StdDeck_CardMask hands_p0[2];
    StdDeck_CardMask_RESET(hands_p0[0]);
    StdDeck_CardMask_SET(hands_p0[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hands_p0[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_RESET(hands_p0[1]);
    StdDeck_CardMask_SET(hands_p0[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hands_p0[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    double weights_p0[2] = {0.75, 0.25};

    StdDeck_CardMask hands_p1[1];
    StdDeck_CardMask_RESET(hands_p1[0]);
    StdDeck_CardMask_SET(hands_p1[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hands_p1[0], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask hands_p2[1];
    StdDeck_CardMask_RESET(hands_p2[0]);
    StdDeck_CardMask_SET(hands_p2[0], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hands_p2[0], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));

    PlayerRange ranges[3];
    ranges[0].hand_masks = hands_p0;
    ranges[0].weights = weights_p0;
    ranges[0].count = 2;
    ranges[0].total_weight = 1.0;

    ranges[1].hand_masks = hands_p1;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    ranges[2].hand_masks = hands_p2;
    ranges[2].weights = NULL;
    ranges[2].count = 1;
    ranges[2].total_weight = 1.0;

    double stacks[3] = {0.0, 0.0, 0.0};
    double invested[3] = {100.0, 100.0, 100.0};

    MultiwayPotState state = {ranges, stacks, invested, 3};
    MultiwayEquityOptions options;
    options.use_montecarlo = true;
    options.iterations = 5000;
    options.orderflag = 0;
    MultiwayEquityResult result;
    memset(&result, 0, sizeof(result));

    int matchups = CalculateMultiwayEquity(
        game_holdem, &state, board, dead,
        5, &options, &result);

    if (matchups <= 0) {
        fprintf(stderr, "Expected matchups > 0 for MC weight test\n");
        return 1;
    }

    double total_equity = result.equity[0] + result.equity[1] + result.equity[2];
    ASSERT_NEAR(total_equity, 1.0, 1e-3, "Equity should sum to 1");
    // The test was asserting that total_weighted_samples is 0, which is wrong.
    // It should be > 0 (actually close to 5000 because weights sum to 1).
    // In this test weights are 0.75+0.25=1.0 for P0, 1.0 for P1, 1.0 for P2.
    // Total combined weight is 1.0.
    // So samples should be 5000.
    if (result.total_weighted_samples < 4000.0) {
        fprintf(stderr, "Expected weighted samples ~5000, got %.2f\n", result.total_weighted_samples);
        return 1;
    }
    return 0;
}

static int test_weighted_tie_monte_carlo(void) {
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));

    StdDeck_CardMask hero[2];
    StdDeck_CardMask_RESET(hero[0]);
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hero[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));

    StdDeck_CardMask_RESET(hero[1]);
    StdDeck_CardMask_SET(hero[1], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hero[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));

    double hero_weights[2] = {0.6, 0.4};

    StdDeck_CardMask villain[1];
    StdDeck_CardMask_RESET(villain[0]);
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(villain[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    PlayerRange ranges[2];
    ranges[0].hand_masks = hero;
    ranges[0].weights = hero_weights;
    ranges[0].count = 2;
    ranges[0].total_weight = 1.0;

    ranges[1].hand_masks = villain;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    double stacks[2] = {0.0, 0.0};
    double invested[2] = {120.0, 120.0};

    MultiwayPotState state = {ranges, stacks, invested, 2};
    MultiwayEquityOptions options;
    options.use_montecarlo = true;
    options.iterations = 8000;
    options.orderflag = 0;

    MultiwayEquityResult result;
    memset(&result, 0, sizeof(result));

    int matchups = CalculateMultiwayEquity(
        game_holdem, &state, board, dead,
        2, &options, &result);

    if (matchups <= 0) {
        fprintf(stderr, "Expected matchups > 0 for tie MC test\n");
        return 1;
    }

    double total_ev = result.ev[0] + result.ev[1];
    ASSERT_NEAR(total_ev, 240.0, 2.0, "Total EV should equal pot with small tolerance");
    ASSERT_NEAR(result.win_prob[0] + result.tie_prob[0],
                result.win_prob[1] + result.tie_prob[1],
                0.05,
                "Win+tie probabilities should align for both players");
    return 0;
}

int main(void) {
    if (test_simple_sidepots() != 0)
        return 1;
    if (test_split_pot_exhaustive() != 0)
        return 1;
    if (test_holdem8_scoop() != 0)
        return 1;
    if (test_holdem8_quartering() != 0)
        return 1;
    if (test_weighted_monte_carlo() != 0)
        return 1;
    if (test_weighted_tie_monte_carlo() != 0)
        return 1;

    printf("Multiway equity tests passed.\n");
    return 0;
}
