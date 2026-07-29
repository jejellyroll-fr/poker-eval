#include <math.h>
#include <stdio.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/RangeEquity.h>

#define ASSERT_MSG(cond, msg)                                                     \
    if (!(cond)) {                                                                \
        fprintf(stderr, "Assertion failed: %s\n", msg);                           \
        return 1;                                                                 \
    }

static double evaluate_single_matchup_equity(StdDeck_CardMask hero,
                                             StdDeck_CardMask villain,
                                             StdDeck_CardMask board) {
    enum_result_t matchup_result;
    if (enumResultAlloc(&matchup_result, 2, enum_ordering_mode_hi) != 0) {
        fprintf(stderr, "Failed to allocate enum_result_t\n");
        return 0.0;
    }

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, board);
    StdDeck_CardMask_OR(dead, dead, hero);
    StdDeck_CardMask_OR(dead, dead, villain);

    StdDeck_CardMask pockets[2];
    pockets[0] = hero;
    pockets[1] = villain;

    int nboard = StdDeck_numCards(board);
    int ret = enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2, nboard, 0, &matchup_result);
    ASSERT_MSG(ret == 0, "enumExhaustive failed");
    ASSERT_MSG(matchup_result.nsamples > 0, "No samples generated");

    double equity = matchup_result.ev[0] / matchup_result.nsamples;
    enumResultFree(&matchup_result);
    return equity;
}

int main(void) {
    // Hero range: {AsAh (weight 0.75), KsKh (weight 0.25)}
    StdDeck_CardMask hero_hands[2];
    StdDeck_CardMask_RESET(hero_hands[0]);
    StdDeck_CardMask_SET(hero_hands[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hero_hands[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_RESET(hero_hands[1]);
    StdDeck_CardMask_SET(hero_hands[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hero_hands[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    double hero_weights[2] = {0.75, 0.25};

    // Villain range: {KcKd}
    StdDeck_CardMask villain_hands[1];
    StdDeck_CardMask_RESET(villain_hands[0]);
    StdDeck_CardMask_SET(villain_hands[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(villain_hands[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));

    PlayerRange ranges[2];
    ranges[0].hand_masks = hero_hands;
    ranges[0].weights = hero_weights;
    ranges[0].count = 2;
    ranges[0].total_weight = hero_weights[0] + hero_weights[1];

    ranges[1].hand_masks = villain_hands;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_OR(dead, dead, board);

    enum_result_t aggregate_result;
    ASSERT_MSG(enumResultAlloc(&aggregate_result, 2, enum_ordering_mode_hi) == 0,
               "Failed to allocate aggregate result");

    int board_to_deal = 5 - StdDeck_numCards(board);
    int matchups = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead, board_to_deal, false, 0, 0, &aggregate_result
    );

    ASSERT_MSG(matchups == 2, "Weighted range should generate 2 matchups");
    ASSERT_MSG(aggregate_result.nsamples == (unsigned int)matchups, "nsamples should reflect matchup count");

    // Expected weighted equity from explicit enumeration
    double ev_aa_vs_kk = evaluate_single_matchup_equity(hero_hands[0], villain_hands[0], board);
    double ev_kk_vs_kk = evaluate_single_matchup_equity(hero_hands[1], villain_hands[0], board);
    double expected_ev = (hero_weights[0] * ev_aa_vs_kk + hero_weights[1] * ev_kk_vs_kk) /
                         (hero_weights[0] + hero_weights[1]);

    ASSERT_MSG(fabs(aggregate_result.ev[0] - expected_ev) < 1e-6, "EV should respect range weights");
    ASSERT_MSG(fabs(aggregate_result.ev[1] - (1.0 - expected_ev)) < 1e-6, "Villain EV should complement hero EV");

    // Cross-check using aggregated win/tie counts
    double total_scenarios = (double)aggregate_result.nwinhi[0] +
                             (double)aggregate_result.nwinhi[1] +
                             (double)aggregate_result.ntiehi[0];
    ASSERT_MSG(total_scenarios > 0.0, "Total scenarios should be positive");

    double hero_equity_from_counts =
        ((double)aggregate_result.nwinhi[0] + 0.5 * (double)aggregate_result.ntiehi[0]) / total_scenarios;
    ASSERT_MSG(fabs(hero_equity_from_counts - expected_ev) < 2e-3,
               "Count-based equity should match weighted expectation");

    enumResultFree(&aggregate_result);
    return 0;
}
