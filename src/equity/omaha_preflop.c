/*
 * omaha_preflop.c - Omaha Preflop Equity Lookup Implementation
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include <poker_eval/equity/omaha_preflop.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poker_eval/deck/deck_std.h>

/* Helper struct for sorting */
typedef struct {
    int rank;
    int suit;
} card_t;

static int compare_cards(const void* a, const void* b) {
    const card_t* c1 = (const card_t*)a;
    const card_t* c2 = (const card_t*)b;
    if (c1->rank != c2->rank) {
        return c2->rank - c1->rank; /* Descending rank (12=Ace first) */
    }
    return c1->suit - c2->suit; /* Ascending suit (arbitrary canonical order) */
}

omaha_hand_key_t omaha_key_from_ranks_suits(const int ranks[4], const int suits[4]) {
    card_t cards[4];
    for(int i=0; i<4; ++i) {
        cards[i].rank = ranks[i];
        cards[i].suit = suits[i];
    }

    /* Sort to canonical order */
    qsort(cards, 4, sizeof(card_t), compare_cards);

    /* Normalize suits */
    int suit_map[4] = {-1, -1, -1, -1};
    int next_suit_id = 0;
    int norm_suits[4];

    for(int i=0; i<4; ++i) {
        int s = cards[i].suit;
        /* Handle 0-3 suit range; map arbitrary suit int to 0..3 if needed,
           but StdDeck suits are 0..3 */
        if (s < 0 || s > 3) s = 0;

        if (suit_map[s] == -1) {
            suit_map[s] = next_suit_id++;
        }
        norm_suits[i] = suit_map[s];
    }

    /* Pack Ranks: r1<<12 | r2<<8 | r3<<4 | r4 */
    uint32_t r_packed = ((uint32_t)cards[0].rank << 12) |
                        ((uint32_t)cards[1].rank << 8) |
                        ((uint32_t)cards[2].rank << 4) |
                        ((uint32_t)cards[3].rank);

    /* Pack Suits: s2<<4 | s3<<2 | s4 */
    /* s1 is always 0 (canonical), so we don't store it. */
    uint32_t s_packed = ((uint32_t)norm_suits[1] << 4) |
                        ((uint32_t)norm_suits[2] << 2) |
                        ((uint32_t)norm_suits[3]);

    return (r_packed << 6) | s_packed;
}

omaha_hand_key_t omaha_cards_to_key(StdDeck_CardMask hand) {
    int ranks[4];
    int suits[4];
    int count = 0;

    /* Extract cards */
    for (int i=0; i<52; ++i) {
        if (StdDeck_CardMask_CARD_IS_SET(hand, i)) {
            if (count < 4) {
                ranks[count] = StdDeck_RANK(i);
                suits[count] = StdDeck_SUIT(i);
                count++;
            }
        }
    }

    if (count < 4) return 0; /* Invalid */

    return omaha_key_from_ranks_suits(ranks, suits);
}

/* Table functions */
#define TABLE_SIZE (1 << 22)

omaha_preflop_table_t* omaha_preflop_table_create(void) {
    omaha_preflop_table_t* t = calloc(1, sizeof(omaha_preflop_table_t));
    if (!t) return NULL;
    t->size = TABLE_SIZE;
    t->data = calloc(t->size, sizeof(float));
    if (!t->data) {
        free(t);
        return NULL;
    }
    return t;
}

void omaha_preflop_table_free(omaha_preflop_table_t* t) {
    if (t) {
        free(t->data);
        free(t);
    }
}

int omaha_preflop_table_load(omaha_preflop_table_t* t, const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return -1;

    size_t n = fread(t->data, sizeof(float), t->size, f);
    fclose(f);

    if (n != t->size) return -1;
    t->loaded = 1;
    return 0;
}

int omaha_preflop_table_save(const omaha_preflop_table_t* t, const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) return -1;

    size_t n = fwrite(t->data, sizeof(float), t->size, f);
    fclose(f);

    return (n == t->size) ? 0 : -1;
}

float omaha_preflop_table_get(const omaha_preflop_table_t* t, omaha_hand_key_t key) {
    if (!t || !t->data || key >= t->size) return 0.5f;
    float v = t->data[key];
    /* Optional: handle uninitialized (0) values by returning average equity?
       But 0 might be a valid equity (very bad hand).
       However, equities are rarely exactly 0.0f. */
    return v;
}

void omaha_preflop_table_set(omaha_preflop_table_t* t, omaha_hand_key_t key, float equity) {
    if (t && t->data && key < t->size) {
        t->data[key] = equity;
    }
}
