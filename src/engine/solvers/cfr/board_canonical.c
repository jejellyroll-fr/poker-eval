/*
 * board_canonical.c - Suit-permutation canonicalization (board isomorphism)
 *
 * FEAT-02: map any set of board/hole cards to a canonical suit representative.
 * Isomorphic boards (AhKh7c vs AdKd7s) collapse onto one key, and the suit
 * permutation is recorded so strategies can be translated back to the
 * original suits at export time.
 *
 * ISO-01: the canonical form is a true orbit representative under the group
 * of the 24 suit permutations. The obvious scheme — sort cards by rank, then
 * relabel the surviving suits by first appearance — has a subtle flaw for a
 * pair plus a kicker: two equal-rank cards (the pair) tie on the sort, the
 * tie is broken by raw suit, and raw suit is not invariant under suit
 * permutation. Isomorphic hands (As Ah Kc vs Ad Ah Ks, say) therefore got
 * different keys. That over-split inflates the flop class table from the true
 * 1755 isomorphism classes to 1911, one spurious class for each of the 156
 * pair-plus-kicker rank pairs.
 *
 * The robust representative is the lexicographically smallest encoding over
 * the whole orbit of the 24 suit permutations; the cost (24 small sorts) is
 * trivial next to the CFR walks the keys feed.
 */

#include "poker_eval/engine/solvers/cfr/board_canonical.h"

#include <string.h>

typedef struct
{
    int rank;
    int suit;
} bc_card_t;

/* All 24 permutations of {0,1,2,3} in lexicographic order. The representative
   is the minimum over the whole table, so the order inside it is irrelevant. */
static const int k_bc_suit_perms[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
    {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
    {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}
};

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

/* Is card a strictly before card b under (rank desc, permuted suit asc)?
   The sort key must use the permuted suit, never the raw suit: raw suits are
   exactly what a suit permutation moves, and using them would make the
   canonical representative depend on the orbit representative. */
static int bc_lt(const bc_card_t *list, int a, int b, const int *perm)
{
    int ra = list[a].rank;
    int rb = list[b].rank;
    if (ra != rb)
        return ra > rb;
    return perm[list[a].suit] < perm[list[b].suit];
}

/* Insertion-sort `order` into (rank desc, permuted suit asc). */
static void bc_order(const bc_card_t *list, int n, const int *perm, int *order)
{
    for (int i = 0; i < n; ++i)
        order[i] = i;
    for (int i = 1; i < n; ++i)
    {
        int key = order[i];
        int j = i - 1;
        /* Move an existing card right only when it comes after the key. */
        while (j >= 0 && bc_lt(list, key, order[j], perm))
        {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
}

/* Write the 2n-character key of the sorted cards: rank letter followed by
   the first-appearance label of the permuted suit. The labels make the key
   depend only on the orbit, not on which concrete suits are used. Returns 0
   on success, -1 on a short buffer. */
static int bc_encode(const bc_card_t *list, int n, const int *order,
                     const int *perm, char *out_key, size_t out_size)
{
    int label_of[4];
    int pos = 0;
    int next = 0;

    if (!out_key || out_size < (size_t)(2 * n + 1))
        return -1;
    for (int s = 0; s < 4; ++s)
        label_of[s] = -1;
    for (int i = 0; i < n; ++i)
    {
        int card = order[i];
        int s = perm[list[card].suit];
        if (label_of[s] < 0)
            label_of[s] = next++;
        out_key[pos++] = "23456789TJQKA"[list[card].rank];
        out_key[pos++] = (char)('a' + label_of[s]);
    }
    out_key[pos] = '\0';
    return 0;
}

/* The lexicographically smallest encoding over the 24 permutations, and the
   permutation that produced it. */
static int bc_best_perm(const bc_card_t *list, int n, int *out_perm,
                        char *out_best)
{
    int have = 0;
    int best_pi = 0;

    for (int pi = 0; pi < 24; ++pi)
    {
        const int *perm = k_bc_suit_perms[pi];
        int order[52];
        char enc[1 + 2 * 52];

        bc_order(list, n, perm, order);
        if (bc_encode(list, n, order, perm, enc, sizeof(enc)) != 0)
            continue;
        if (!have || strcmp(enc, out_best) < 0)
        {
            memcpy(out_best, enc, sizeof(enc));
            best_pi = pi;
            have = 1;
        }
    }
    if (!have)
        return -1;
    *out_perm = best_pi;
    return 0;
}

int pe_board_canonical_key(mask_t cards, int n, char *out_key, size_t out_size)
{
    bc_card_t list[52];
    char best[1 + 2 * 52];
    int best_perm = 0;

    if (!out_key || bc_collect(cards, n, list, 52) != 0)
        return -1;
    if (bc_best_perm(list, n, &best_perm, best) != 0)
        return -1;
    if (out_size < strlen(best) + 1)
        return -1;
    memcpy(out_key, best, strlen(best) + 1);
    return 0;
}

int pe_board_canonicalize(mask_t cards, int n, mask_t *out_canon,
                          int suit_perm[4])
{
    bc_card_t list[52];
    char best[1 + 2 * 52];
    int best_perm = 0;
    int order[52];
    int label_of[4];
    int next_label = 0;
    mask_t canon = MASK_EMPTY;

    if (!out_canon || bc_collect(cards, n, list, 52) != 0)
        return -1;
    if (bc_best_perm(list, n, &best_perm, best) != 0)
        return -1;

    /* Rebuild the representative with the same winning permutation and order
       used for the key. `suit_perm[label]` maps each canonical suit label back
       to the concrete input suit, which lets callers translate the result. */
    bc_order(list, n, k_bc_suit_perms[best_perm], order);
    for (int s = 0; s < 4; ++s)
    {
        label_of[s] = -1;
        if (suit_perm)
            suit_perm[s] = -1;
    }

    for (int i = 0; i < n; ++i)
    {
        int card = order[i];
        int raw_suit = list[card].suit;
        int permuted_suit = k_bc_suit_perms[best_perm][raw_suit];
        if (label_of[permuted_suit] < 0)
        {
            label_of[permuted_suit] = next_label++;
            if (suit_perm)
                suit_perm[label_of[permuted_suit]] = raw_suit;
        }
        canon = mask_set(canon,
                         MODERN_MAKE_CARD(list[card].rank,
                                          label_of[permuted_suit]));
    }

    *out_canon = canon;
    return 0;
}
