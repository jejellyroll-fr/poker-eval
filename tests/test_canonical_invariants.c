/*
 * test_canonical_invariants.c - Suit permutation invariants for canonical 5-card evaluator
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#include <poker_eval/core/canonical_5card.h>
#include <poker_eval/core/modern_cardmask.h>

static inline int card_rank(int c) { return c % 13; }
static inline int card_suit(int c) { return c / 13; }

static mask_t random_5card_hand(void) {
    mask_t hand = MASK_EMPTY;
    while (mask_popcount(hand) < 5) {
        int c = rand() % 52;
        if (!mask_is_set(hand, c)) hand = mask_set(hand, c);
    }
    return hand;
}

static mask_t apply_suit_perm(mask_t hand, const int perm[4]) {
    mask_t out = MASK_EMPTY;
    for (int c = 0; c < 52; ++c) {
        if (mask_is_set(hand, c)) {
            int r = card_rank(c);
            int s = card_suit(c);
            int ns = perm[s];
            int nc = r + ns * 13;
            out = mask_set(out, nc);
        }
    }
    return out;
}

int main(void) {
    srand(42);
    /* Some fixed permutations of suits */
    const int perms[][4] = {
        {0,1,2,3},
        {1,0,2,3},
        {2,3,0,1},
        {3,2,1,0},
        {1,2,3,0}
    };

    for (int t = 0; t < 200; ++t) {
        mask_t h = random_5card_hand();
        uint32_t e0 = evaluate_5card_canonical(h);
        for (int i = 0; i < (int)(sizeof(perms)/sizeof(perms[0])); ++i) {
            mask_t hp = apply_suit_perm(h, perms[i]);
            uint32_t ep = evaluate_5card_canonical(hp);
            if (e0 != ep) {
                char b0[128], b1[128];
                mask_to_string(h, b0, sizeof(b0));
                mask_to_string(hp, b1, sizeof(b1));
                printf("Suit perm mismatch: %s -> %s: %u vs %u\n", b0, b1, e0, ep);
            }
            assert(e0 == ep);
        }
    }
    printf("Canonical suit-permutation invariants: OK\n");
    return 0;
}

