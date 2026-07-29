/* test_triple_draw_exhaustive_multi.c - Validate exhaustive multi-hand enumeration for 27 Triple Draw */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/cardmask_compat.h>

static unsigned long long binom(unsigned n, unsigned k) {
    if (k > n) return 0;
    if (k > n - k) k = n - k;
    unsigned long long res = 1;
    for (unsigned i = 1; i <= k; ++i) {
        res = (res * (n - k + i)) / i;
    }
    return res;
}

int main(void) {
    /* Live set of 10 cards: Ten..Ace of clubs and spades (10 cards) */
    mask_t live = string_to_mask("Tc Jc Qc Kc Ac Ts Js Qs Ks As");
    /* Dead = full deck minus live */
    mask_t full = 0; for (int c = 0; c < 52; ++c) full |= (1ULL << c);
    mask_t deadm = full & ~live;
    StdDeck_CardMask dead = mask_t_to_cardmask(deadm);

    StdDeck_CardMask board; StdDeck_CardMask_RESET(board);
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);

    enum_result_t result;
    int rv = enumExhaustive_dispatch(game_27_triple_draw, pockets, board, dead, 2, 0, 0, &result);
    if (rv != 0) {
        fprintf(stderr, "enumExhaustive_dispatch failed: %d\n", rv);
        return 1;
    }

    /* Both players need 5 cards from 10 live, disjoint: C(10,5)*C(5,5) */
    unsigned long long expected = binom(10,5) * binom(5,5);
    if (result.nsamples != (unsigned)expected) {
        fprintf(stderr, "nsamples=%u expected=%llu\n", result.nsamples, expected);
        return 1;
    }
    printf("27 Triple Draw exhaustive multi-hand: nsamples=%u expected=%llu\n", result.nsamples, expected);
    return 0;
}
