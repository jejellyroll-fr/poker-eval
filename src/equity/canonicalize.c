/*
 * canonicalize.c - Canonicalization utilities for board equivalence classes
 */

#include <poker_eval/equity/canonicalize.h>
#include <poker_eval/deck/deck_std.h>
#include <string.h>

static const char k_rank_chars[] = "23456789TJQKA"; /* 0..12 */

typedef struct { int idx; int rank; int suit; } card_t;

static int cmp_card_desc(const void* a, const void* b) {
    const card_t* A = (const card_t*)a;
    const card_t* B = (const card_t*)b;
    if (A->rank != B->rank) return (B->rank - A->rank);
    return (A->suit - B->suit);
}

int pe_canonical_key(StdDeck_CardMask cards, int n, char* out_key, size_t out_size) {
    if (!out_key || out_size < (size_t)(2 * n + 1)) return -1;
    if (StdDeck_numCards(cards) != n) return -1;

    card_t card_list[52];
    int count = 0;
    for (int c = 0; c < StdDeck_N_CARDS && count < n; c++) {
        if (StdDeck_CardMask_CARD_IS_SET(cards, c)) {
            card_list[count].idx = c;
            card_list[count].rank = StdDeck_RANK(c);
            card_list[count].suit = StdDeck_SUIT(c);
            count++;
        }
    }
    if (count != n) return -1;

    /* Simple insertion sort */
    for (int i = 1; i < n; i++) {
        card_t key = card_list[i];
        int j = i - 1;
        while (j >= 0 && cmp_card_desc(&card_list[j], &key) > 0) {
            card_list[j + 1] = card_list[j];
            j--;
        }
        card_list[j + 1] = key;
    }

    int suit_map[4]; for (int s = 0; s < 4; s++) suit_map[s] = -1;
    int next_label = 0;
    int pos = 0;
    for (int i = 0; i < n; i++) {
        int r = card_list[i].rank;
        int s = card_list[i].suit;
        if (suit_map[s] < 0) suit_map[s] = next_label++;
        int sl = suit_map[s];
        out_key[pos++] = k_rank_chars[r];
        out_key[pos++] = (char)('a' + sl);
    }
    out_key[pos] = '\0';
    return 0;
}

uint64_t pe_canonical_hash64(StdDeck_CardMask cards, int n) {
    char key[128]; /* Sufficient for up to ~60 cards */
    if (pe_canonical_key(cards, n, key, sizeof(key)) != 0) return 0ULL;
    uint64_t h = 1469598103934665603ULL;
    for (const char* p = key; *p; ++p) {
        h ^= (unsigned char)(*p);
        h *= 1099511628211ULL;
    }
    return h;
}

int pe_board5_canonical_key(StdDeck_CardMask board, char* out_key, size_t out_size) {
    return pe_canonical_key(board, 5, out_key, out_size);
}

uint64_t pe_board5_canonical_hash64(StdDeck_CardMask board) {
    return pe_canonical_hash64(board, 5);
}

int pe_hand7_canonical_key(StdDeck_CardMask cards7, char* out_key, size_t out_size) {
    return pe_canonical_key(cards7, 7, out_key, out_size);
}

uint64_t pe_hand7_canonical_hash64(StdDeck_CardMask cards7) {
    return pe_canonical_hash64(cards7, 7);
}
