/* test_5draw_mc_small.c - sanity check for 5-draw Monte Carlo (JokerDeck) */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

static void setup_pockets(StdDeck_CardMask pockets[2]) {
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    /* Use two simple disjoint pairs */
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
}

int main(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;

    setup_pockets(pockets);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    int iters = 200;
    int rv = enumSample(game_5draw, pockets, board, dead, 2, 0, iters, 0, &result);
    if (rv != 0) {
        fprintf(stderr, "enumSample failed: %d\n", rv);
        return 1;
    }
    if (result.nsamples != (unsigned)iters) {
        fprintf(stderr, "nsamples=%u expected=%d\n", result.nsamples, iters);
        return 1;
    }
    /* Sum EV around 1 pot per iteration */
    double sum_ev = 0.0; for (int i = 0; i < 2; ++i) sum_ev += result.ev[i];
    double diff = sum_ev - (double)iters; if (diff < 0) diff = -diff;
    if (diff > iters * 0.05 + 1e-6) {
        fprintf(stderr, "sum_ev=%.3f expected≈%d\n", sum_ev, iters);
        return 1;
    }
    printf("5-draw MC small test passed.\n");
    return 0;
}

