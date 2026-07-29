/* test_5drawnsq_exhaustive_multi.c - Validate exhaustive multi-hand enumeration for 5-Draw no qualifier (JokerDeck) */

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
    for (unsigned i = 1; i <= k; ++i) res = (res * (n - k + i)) / i;
    return res;
}

int main(void) {
    StdDeck_CardMask board; StdDeck_CardMask_RESET(board);
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);

    /* Live StdDeck set: 10 cards; Joker remains available implicitly */
    mask_t live = string_to_mask("Tc Jc Qc Kc Ac Ts Js Qs Ks As");
    mask_t full = 0; for (int c = 0; c < 52; ++c) full |= (1ULL << c);
    mask_t deadm = full & ~live;
    StdDeck_CardMask dead_std = mask_t_to_cardmask(deadm);

    enum_result_t result;
    int rv = enumExhaustive_dispatch(game_5drawnsq, pockets, board, dead_std, 2, 0, 0, &result);
    if (rv != 0) {
        fprintf(stderr, "enumExhaustive_dispatch failed: %d\n", rv);
        return 1;
    }

    /* Expect C(11,5) * C(6,5) outcomes (10 live + Joker, then disjoint) */
    unsigned long long expected = binom(11,5) * binom(6,5);
    if (result.nsamples != (unsigned)expected) {
        fprintf(stderr, "nsamples=%u expected=%llu\n", result.nsamples, expected);
        return 1;
    }
    printf("5-drawnsq exhaustive multi-hand: nsamples=%u expected=%llu\n", result.nsamples, expected);
    return 0;
}
