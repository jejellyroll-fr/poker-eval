/* test_doubleflop_mc_small.c - Validate Double Flop Monte Carlo paths */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

static void setup_pockets(StdDeck_CardMask pockets[2]) {
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    /* Player 1: As Ah */
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    /* Player 2: Ks Kh */
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
}

static void test_case(int nboard) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;

    setup_pockets(pockets);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    /* Fill board with nboard spades (arbitrary) */
    int ranks[] = {StdDeck_Rank_TEN, StdDeck_Rank_JACK, StdDeck_Rank_QUEEN, StdDeck_Rank_KING, StdDeck_Rank_ACE};
    for (int i = 0; i < nboard; ++i) {
        StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(ranks[i], StdDeck_Suit_CLUBS));
        StdDeck_CardMask_SET(dead, StdDeck_MAKE_CARD(ranks[i], StdDeck_Suit_CLUBS));
    }

    int iters = 200;
    int rv = enumSample(game_doubleflop_holdem, pockets, board, dead, 2, nboard, iters, 0, &result);
    if (rv != 0) {
        fprintf(stderr, "enumSample failed: %d (nboard=%d)\n", rv, nboard);
        exit(1);
    }
    /* nsamples should equal iterations */
    assert(result.nsamples == (unsigned)iters);
    /* Sum of EV across players should be close to nsamples (one pot per iteration) */
    double sum_ev = result.ev[0] + result.ev[1];
    double diff = sum_ev - (double)iters;
    if (diff < 0) diff = -diff;
    assert(diff < iters * 0.05 + 1e-6); /* 5% tolerance */
}

int main(void) {
    test_case(0);
    test_case(3);
    test_case(4);
    printf("Double Flop MC small tests passed.\n");
    return 0;
}
