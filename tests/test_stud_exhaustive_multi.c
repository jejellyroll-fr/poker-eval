/* test_stud_exhaustive_multi.c - Validate exhaustive multi-hand enumeration for 7-stud */

#include <assert.h>
#include <stdio.h>
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
    /* Choose a small live set of 8 cards (e.g., spades 7..A) */
    mask_t live = string_to_mask("7s 8s 9s Ts Js Qs Ks As");
    StdDeck_CardMask board; StdDeck_CardMask_RESET(board);

    /* Build dead as full deck minus live */
    mask_t full = 0; for (int c = 0; c < 52; ++c) full |= (1ULL << c);
    mask_t deadm = full & ~live;
    StdDeck_CardMask dead = mask_t_to_cardmask(deadm);

    /* Two players, each with 3 fixed pocket cards (chosen from dead to avoid overlap with live) */
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    /* Arbitrary pockets from hearts */
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    enum_result_t result;
    int rv = enumExhaustive_dispatch(game_7stud, pockets, board, dead, 2, 0, 0, &result);
    if (rv != 0) {
        fprintf(stderr, "enumExhaustive failed: %d\n", rv);
        return 1;
    }

    /* Each player needs 4 more cards from the 8-card live set disjointly: C(8,4)*C(4,4) */
    unsigned long long expected = binom(8,4) * binom(4,4);
    /* nsamples increments per outcome */
    assert(result.nsamples == (unsigned)expected);
    printf("7-stud exhaustive multi-hand: nsamples=%u expected=%llu\n", result.nsamples, expected);
    return 0;
}
