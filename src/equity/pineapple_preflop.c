/*
 * pineapple_preflop.c - Pineapple Preflop Key Implementation
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include <poker_eval/equity/pineapple_preflop.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/deck/deck_std.h>

typedef struct {
    int rank;
    int suit;
} card_t;

static int compare_cards(const void* a, const void* b) {
    const card_t* c1 = (const card_t*)a;
    const card_t* c2 = (const card_t*)b;
    if (c1->rank != c2->rank) {
        return c2->rank - c1->rank;
    }
    return c1->suit - c2->suit;
}

pineapple_hand_key_t pineapple_key_from_ranks_suits(const int ranks[3], const int suits[3]) {
    card_t cards[3];
    for (int i = 0; i < 3; ++i) {
        cards[i].rank = ranks[i];
        cards[i].suit = suits[i];
    }

    qsort(cards, 3, sizeof(card_t), compare_cards);

    int suit_map[4] = {-1, -1, -1, -1};
    int next_suit_id = 0;
    int norm_suits[3];

    for (int i = 0; i < 3; ++i) {
        int s = cards[i].suit;
        if (s < 0 || s > 3) s = 0;
        if (suit_map[s] == -1) {
            suit_map[s] = next_suit_id++;
        }
        norm_suits[i] = suit_map[s];
    }

    uint32_t r_packed = ((uint32_t)cards[0].rank << 8) |
                          ((uint32_t)cards[1].rank << 4) |
                          ((uint32_t)cards[2].rank);

    uint32_t s_packed = ((uint32_t)norm_suits[1] << 2) |
                          ((uint32_t)norm_suits[2]);

    return (r_packed << 4) | s_packed;
}

pineapple_hand_key_t pineapple_cards_to_key(StdDeck_CardMask hand) {
    int ranks[3];
    int suits[3];
    int count = 0;

    for (int i = 0; i < 52; ++i) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, i)) {
            if (count < 3) {
                ranks[count] = StdDeck_RANK(i);
                suits[count] = StdDeck_SUIT(i);
                count++;
            }
        }
    }

    if (count < 3) return 0;

    return pineapple_key_from_ranks_suits(ranks, suits);
}
