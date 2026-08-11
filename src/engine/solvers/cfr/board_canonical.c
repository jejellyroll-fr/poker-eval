/*
 * board_canonical.c - Suit-permutation canonicalization (board isomorphism)
 *
 * FEAT-02: map any set of board/hole cards to a canonical suit representative.
 * Isomorphic boards (AhKh7c vs AdKd7s) collapse onto one key, and the suit
 * permutation is recorded so strategies can be translated back to the
 * original suits at export time.
 */

#include "poker_eval/engine/solvers/cfr/board_canonical.h"

#include <string.h>

typedef struct
{
    int rank;
    int suit;
} bc_card_t;

static int cmp_card_desc(const void *a, const void *b)
{
    const bc_card_t *A = (const bc_card_t *)a;
    const bc_card_t *B = (const bc_card_t *)b;
    if (A->rank != B->rank)
        return (B->rank - A->rank);
    return (A->suit - B->suit);
}

static int bc_collect(mask_t cards, int n, bc_card_t *out, int max)
{
    if (n <= 0 || n > max || n > 52)
        return -1;
    int count = 0;
    for (int card = 0; card < MODERN_DECK_SIZE && count < n; ++card)
    {
        if (mask_is_set(cards, card))
        {
            out[count].rank = MODERN_GET_RANK(card);
            out[count].suit = MODERN_GET_SUIT(card);
            ++count;
        }
    }
    if (count != n)
        return -1;
    return 0;
}

int pe_board_count_cards(mask_t cards)
{
    int count = 0;
    for (int card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card))
            ++count;
    return count;
}

int pe_board_canonical_key(mask_t cards, int n, char *out_key, size_t out_size)
{
    bc_card_t list[52];
    if (!out_key || out_size < (size_t)(2 * n + 1))
        return -1;
    if (bc_collect(cards, n, list, 52) != 0)
        return -1;

    for (int i = 1; i < n; ++i)
    {
        bc_card_t key = list[i];
        int j = i - 1;
        while (j >= 0 && cmp_card_desc(&list[j], &key) > 0)
        {
            list[j + 1] = list[j];
            --j;
        }
        list[j + 1] = key;
    }

    int suit_map[4];
    for (int s = 0; s < 4; ++s)
        suit_map[s] = -1;
    int next_label = 0;
    int pos = 0;
    for (int i = 0; i < n; ++i)
    {
        int r = list[i].rank;
        int s = list[i].suit;
        if (suit_map[s] < 0)
            suit_map[s] = next_label++;
        char rank_ch = "23456789TJQKA"[r];
        out_key[pos++] = rank_ch;
        out_key[pos++] = (char)('a' + suit_map[s]);
    }
    out_key[pos] = '\0';
    return 0;
}

int pe_board_canonicalize(mask_t cards, int n, mask_t *out_canon, int suit_perm[4])
{
    bc_card_t list[52];
    mask_t canon = MASK_EMPTY;

    if (!out_canon)
        return -1;
    if (bc_collect(cards, n, list, 52) != 0)
        return -1;

    for (int i = 1; i < n; ++i)
    {
        bc_card_t key = list[i];
        int j = i - 1;
        while (j >= 0 && cmp_card_desc(&list[j], &key) > 0)
        {
            list[j + 1] = list[j];
            --j;
        }
        list[j + 1] = key;
    }

    int suit_map[4];
    for (int s = 0; s < 4; ++s)
        suit_map[s] = -1;
    if (suit_perm)
    {
        for (int s = 0; s < 4; ++s)
            suit_perm[s] = -1;
    }
    int next_label = 0;
    for (int i = 0; i < n; ++i)
    {
        int s = list[i].suit;
        if (suit_map[s] < 0)
        {
            suit_map[s] = next_label++;
            if (suit_perm && suit_map[s] < 4)
                suit_perm[suit_map[s]] = s;
        }
        canon = mask_set(canon, MODERN_MAKE_CARD(list[i].rank, suit_map[s]));
    }

    *out_canon = canon;
    return 0;
}