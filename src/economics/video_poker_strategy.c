/*
 * video_poker_strategy.c - Optimal-strategy derivation for video poker.
 *
 * See include/poker_eval/economics/video_poker_strategy.h for the model and
 * the counting convention (Wizard of Odds per-deal weights).
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 */

#include <poker_eval/economics/video_poker_strategy.h>

#include <poker_eval/equity/draw_optimizer.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* T..A rank bits (ranks 8..12). */
#define VP_ROYAL_RANKS 0x1F00u

/* Per-deal combination weights in the Wizard of Odds convention, indexed by
 * the number of cards discarded (0..5): per-deal total divided by the number
 * of draw combinations C(47,k) / C(48,k). */
static const long long VP_WEIGHT_52[6] = {
    7669695LL, 163185LL, 7095LL, 473LL, 43LL, 5LL
};
static const long long VP_WEIGHT_53[6] = {
    8561520LL, 178365LL, 7590LL, 495LL, 44LL, 5LL
};

/* Category names, in canonical row order per variant. */
static const char *const VP_NAMES_JOB[10] = {
    "Royal Flush",          "Straight Flush",  "Four of a Kind",
    "Full House",           "Flush",           "Straight",
    "Three of a Kind",      "Two Pair",        "Jacks or Better",
    "Nothing"
};

static const char *const VP_NAMES_DW[11] = {
    "Natural Royal Flush",  "Four Deuces",     "Wild Royal Flush",
    "Five of a Kind",       "Straight Flush",  "Four of a Kind",
    "Full House",           "Flush",           "Straight",
    "Three of a Kind",      "Nothing"
};

static const char *const VP_NAMES_JP[12] = {
    "Natural Royal Flush",  "Five of a Kind",  "Wild Royal Flush",
    "Straight Flush",       "Four of a Kind",  "Full House",
    "Flush",                "Straight",        "Three of a Kind",
    "Two Pair",             "Kings or Better", "Nothing"
};

/* ---- small helpers ----------------------------------------------------- */

static int vp_popcount(uint32 m)
{
    return (int)nBitsTable[m & StdDeck_RANK_MASK];
}

/* The joker (card index PE_VP_JOKER_CARD = 52) does not map to raw bit 52:
 * the StdDeck card mask packs spades (0..12), 3 pad, clubs (16..28), 3 pad,
 * diamonds (32..44), 3 pad, hearts (48..60), so bit 52 is inside the hearts
 * field (the 6 of hearts). The joker lives in the dedicated joker field at
 * bit 61 of the 64-bit layout (see JokerDeck_CardMask in deck_joker.h). */
#define VP_JOKER_BIT_LO 61u
#define VP_JOKER_BIT_HI (VP_JOKER_BIT_LO - 32u)

/* Set/clear/test a card index that may be the joker (PE_VP_JOKER_CARD). */
static void vp_set_card(StdDeck_CardMask *m, int card)
{
    if (card == PE_VP_JOKER_CARD) {
#ifdef USE_INT64
        (*m).cards_n |= (1ULL << VP_JOKER_BIT_LO);
#else
        (*m).cards_nn.n2 |= (1u << VP_JOKER_BIT_HI);
#endif
        return;
    }
    StdDeck_CardMask_SET(*m, card);
}

static void vp_unset_card(StdDeck_CardMask *m, int card)
{
    if (card == PE_VP_JOKER_CARD) {
#ifdef USE_INT64
        (*m).cards_n &= ~(1ULL << VP_JOKER_BIT_LO);
#else
        (*m).cards_nn.n2 &= ~(1u << VP_JOKER_BIT_HI);
#endif
        return;
    }
    StdDeck_CardMask_UNSET(*m, card);
}

static int vp_card_is_set(StdDeck_CardMask m, int card)
{
    if (card == PE_VP_JOKER_CARD) {
#ifdef USE_INT64
        return (m.cards_n & (1ULL << VP_JOKER_BIT_LO)) != 0;
#else
        return (m.cards_nn.n2 & (1u << VP_JOKER_BIT_HI)) != 0;
#endif
    }
    return StdDeck_CardMask_CARD_IS_SET(m, card);
}

static int vp_next_combination(int *c, int k, int n)
{
    int i = k - 1;
    while (i >= 0 && c[i] == n - k + i)
        i--;
    if (i < 0)
        return 0;
    c[i]++;
    for (int j = i + 1; j < k; j++)
        c[j] = c[j - 1] + 1;
    return 1;
}

/* ---- hand classification ----------------------------------------------- */

typedef struct {
    uint32 suit_ranks[4]; /* 13-bit rank mask per suit, wilds removed */
    uint32 ranks;         /* union of suit_ranks */
    int count[13];        /* per-rank multiplicity across suits */
    int w;                /* number of wilds */
    int max_count;        /* largest count[] */
    int n2;               /* number of ranks with count == 2 */
    int same_suit;        /* all natural cards share one suit */
} vp_hand_t;

static int vp_analyze(pe_video_poker_variant_t variant, StdDeck_CardMask hand,
                      vp_hand_t *h)
{
    uint32 sr[4];
    sr[StdDeck_Suit_HEARTS] = StdDeck_CardMask_HEARTS(hand);
    sr[StdDeck_Suit_DIAMONDS] = StdDeck_CardMask_DIAMONDS(hand);
    sr[StdDeck_Suit_CLUBS] = StdDeck_CardMask_CLUBS(hand);
    sr[StdDeck_Suit_SPADES] = StdDeck_CardMask_SPADES(hand);

    int has_joker = 0;
#ifdef USE_INT64
    has_joker = (hand.cards_n & (1ULL << VP_JOKER_BIT_LO)) != 0;
#else
    has_joker = (hand.cards_nn.n2 & (1u << VP_JOKER_BIT_HI)) != 0;
#endif
    if (has_joker && variant != PE_VP_JOKER_POKER)
        return -1;

    memset(h, 0, sizeof(*h));
    switch (variant) {
    case PE_VP_JACKS_OR_BETTER:
        for (int i = 0; i < 4; i++)
            h->suit_ranks[i] = sr[i];
        break;
    case PE_VP_DEUCES_WILD:
        for (int i = 0; i < 4; i++) {
            h->suit_ranks[i] = sr[i] & ~1u; /* rank 2 is a deuce */
            if (sr[i] & 1u)
                h->w++;
        }
        break;
    case PE_VP_JOKER_POKER:
        h->w = has_joker ? 1 : 0;
        for (int i = 0; i < 4; i++)
            h->suit_ranks[i] = sr[i];
        break;
    default:
        return -1;
    case PE_VP_VARIANT_COUNT:
        return -1;
    }

    h->ranks = h->suit_ranks[0] | h->suit_ranks[1] | h->suit_ranks[2] |
               h->suit_ranks[3];
    int total = h->w;
    for (int i = 0; i < 4; i++)
        total += vp_popcount(h->suit_ranks[i]);
    if (total != 5)
        return -1;

    for (int r = 0; r < StdDeck_Rank_COUNT; r++) {
        int c = 0;
        for (int i = 0; i < 4; i++)
            c += (int)((h->suit_ranks[i] >> r) & 1u);
        h->count[r] = c;
        if (c > h->max_count)
            h->max_count = c;
        if (c == 2)
            h->n2++;
    }

    for (int i = 0; i < 4; i++)
        if (h->suit_ranks[i] == h->ranks &&
            vp_popcount(h->ranks) == 5 - h->w) {
            h->same_suit = 1;
            break;
        }

    return 0;
}

/* True if the natural ranks fit inside one 5-rank straight window with the
 * wilds filling the gaps. Aces play high or low. */
static int vp_is_straight(uint32 ranks, int w)
{
    if (vp_popcount(ranks) + w < 5)
        return 0;
    for (int top = StdDeck_Rank_ACE; top >= StdDeck_Rank_6; top--) {
        uint32 window = 0x1Fu << (top - 4);
        if ((ranks & ~window) == 0)
            return 1;
    }
    if ((ranks & ~StdRules_FIVE_STRAIGHT) == 0)
        return 1;
    return 0;
}

/* Wild royal flush: at least one wild, all natural cards in one suit, all of
 * distinct T..A ranks, and enough wilds to fill the royal (checked by the
 * caller's ordering for four deuces / four natural + joker cases). */
static int vp_wild_royal(const vp_hand_t *h)
{
    if (h->w < 1 || h->w > 3)
        return 0;
    if (!h->same_suit)
        return 0;
    if ((h->ranks & ~VP_ROYAL_RANKS) != 0)
        return 0;
    return vp_popcount(h->ranks) == 5 - h->w;
}

/* Full house (trips + pair) with wilds filling missing multiplicity. */
static int vp_full_house(const vp_hand_t *h)
{
    for (int a = 0; a < StdDeck_Rank_COUNT; a++) {
        if (h->count[a] == 0)
            continue;
        int need_a = 3 - h->count[a];
        if (need_a > h->w)
            continue;
        for (int b = 0; b < StdDeck_Rank_COUNT; b++) {
            if (b == a || h->count[b] == 0)
                continue;
            if (need_a + (2 - h->count[b]) <= h->w)
                return 1;
        }
    }
    return 0;
}

int pe_video_poker_num_categories(pe_video_poker_variant_t variant)
{
    switch (variant) {
    case PE_VP_JACKS_OR_BETTER:
        return 10;
    case PE_VP_DEUCES_WILD:
        return 11;
    case PE_VP_JOKER_POKER:
        return 12;
    case PE_VP_VARIANT_COUNT:
        return 0;
    default:
        return 0;
    }
}

int pe_video_poker_category(pe_video_poker_variant_t variant,
                            StdDeck_CardMask hand)
{
    vp_hand_t h;
    if (vp_analyze(variant, hand, &h) != 0)
        return -1;

    int flush = h.same_suit;
    int straight = vp_is_straight(h.ranks, h.w);

    switch (variant) {
    case PE_VP_JACKS_OR_BETTER:
        if (flush && h.ranks == VP_ROYAL_RANKS)
            return PE_VP_JOB_ROYAL_FLUSH;
        if (flush && straight)
            return PE_VP_JOB_STRAIGHT_FLUSH;
        if (h.max_count == 4)
            return PE_VP_JOB_FOUR_OF_A_KIND;
        if (h.max_count == 3 && h.n2 == 1)
            return PE_VP_JOB_FULL_HOUSE;
        if (flush)
            return PE_VP_JOB_FLUSH;
        if (straight)
            return PE_VP_JOB_STRAIGHT;
        if (h.max_count == 3)
            return PE_VP_JOB_THREE_OF_A_KIND;
        if (h.n2 == 2)
            return PE_VP_JOB_TWO_PAIR;
        for (int r = StdDeck_Rank_ACE; r >= 0; r--)
            if (h.count[r] == 2)
                return r >= StdDeck_Rank_JACK ? PE_VP_JOB_JACKS_OR_BETTER
                                              : PE_VP_JOB_NOTHING;
        return PE_VP_JOB_NOTHING;

    case PE_VP_DEUCES_WILD:
        if (h.w == 0 && flush && h.ranks == VP_ROYAL_RANKS)
            return PE_VP_DW_NATURAL_ROYAL_FLUSH;
        if (h.w == 4)
            return PE_VP_DW_FOUR_DEUCES;
        if (vp_wild_royal(&h))
            return PE_VP_DW_WILD_ROYAL_FLUSH;
        if (h.w >= 1 && h.max_count + h.w >= 5)
            return PE_VP_DW_FIVE_OF_A_KIND;
        if (flush && straight)
            return PE_VP_DW_STRAIGHT_FLUSH;
        if (h.max_count + h.w >= 4)
            return PE_VP_DW_FOUR_OF_A_KIND;
        if (vp_full_house(&h))
            return PE_VP_DW_FULL_HOUSE;
        if (flush)
            return PE_VP_DW_FLUSH;
        if (straight)
            return PE_VP_DW_STRAIGHT;
        if (h.max_count + h.w >= 3)
            return PE_VP_DW_THREE_OF_A_KIND;
        return PE_VP_DW_NOTHING;

    case PE_VP_JOKER_POKER:
        if (h.w == 0 && flush && h.ranks == VP_ROYAL_RANKS)
            return PE_VP_JP_NATURAL_ROYAL_FLUSH;
        if (h.w >= 1 && h.max_count + h.w >= 5)
            return PE_VP_JP_FIVE_OF_A_KIND;
        if (vp_wild_royal(&h))
            return PE_VP_JP_WILD_ROYAL_FLUSH;
        if (flush && straight)
            return PE_VP_JP_STRAIGHT_FLUSH;
        if (h.max_count + h.w >= 4)
            return PE_VP_JP_FOUR_OF_A_KIND;
        if (vp_full_house(&h))
            return PE_VP_JP_FULL_HOUSE;
        if (flush)
            return PE_VP_JP_FLUSH;
        if (straight)
            return PE_VP_JP_STRAIGHT;
        if (h.max_count + h.w >= 3)
            return PE_VP_JP_THREE_OF_A_KIND;
        if (h.w == 0 && h.n2 == 2)
            return PE_VP_JP_TWO_PAIR;
        if (h.max_count + h.w == 2) {
            /* Single pair. With the joker the pair is the joker plus the
             * highest natural card; without it, the pair rank itself. */
            for (int r = StdDeck_Rank_ACE; r >= 0; r--)
                if (h.count[r] == (h.w == 0 ? 2 : 1))
                    return r >= StdDeck_Rank_KING ? PE_VP_JP_KINGS_OR_BETTER
                                                  : PE_VP_JP_NOTHING;
        }
        return PE_VP_JP_NOTHING;

    default:
        return -1;
    case PE_VP_VARIANT_COUNT:
        return -1;
    }
}

/* ---- isomorphism class table ------------------------------------------- */

#define VP_CLASS_TABLE_BITS 20
#define VP_CLASS_TABLE_SIZE (1u << VP_CLASS_TABLE_BITS)

typedef struct {
    uint64_t hash;      /* FNV-1a of the canonical key */
    char key[11];       /* canonical key (10 chars + NUL; 8 for 4-card) */
    StdDeck_CardMask rep; /* representative deal of the class */
    long long orbit;    /* number of deals in the class */
} vp_class_slot_t;

static vp_class_slot_t *vp_class_insert(vp_class_slot_t *table, uint64_t hash,
                                        const char *key)
{
    size_t idx = (size_t)hash & (VP_CLASS_TABLE_SIZE - 1);
    for (;;) {
        vp_class_slot_t *s = &table[idx];
        if (s->key[0] == '\0') { /* empty slot */
            s->hash = hash;
            size_t key_len = strnlen(key, sizeof(s->key) - 1);
            for (size_t i = 0; i < key_len; i++)
                s->key[i] = key[i];
            s->key[key_len] = '\0';
            s->orbit = 0;
            return s;
        }
        if (s->hash == hash && strncmp(s->key, key, 10) == 0)
            return s;
        idx = (idx + 1) & (VP_CLASS_TABLE_SIZE - 1);
    }
}

/* Next lexicographic permutation of a[0..n-1]; returns 0 when the sequence
 * is already maximally sorted. */
static int vp_next_perm(int *a, int n)
{
    int i = n - 2;
    while (i >= 0 && a[i] >= a[i + 1])
        i--;
    if (i < 0)
        return 0;
    int j = n - 1;
    while (a[j] <= a[i])
        j--;
    int t = a[i];
    a[i] = a[j];
    a[j] = t;
    for (int l = i + 1, r = n - 1; l < r; l++, r--) {
        t = a[l];
        a[l] = a[r];
        a[r] = t;
    }
    return 1;
}

/* Suit-isomorphism canonical key of the natural cards described by the
 * (rank, suit) arrays (n <= 5): the lexicographically smallest
 * (rank, relabeled-suit) pair string over the 24 suit relabelings, with each
 * equal-rank group ordered by relabeled suit. Two hands produce the same key
 * exactly when one is a suit permutation of the other. Every video-poker
 * category is suit symmetric, so all deals in a class share the same optimal
 * discard and the same completion distribution, which is what makes the
 * per-class derivation exact. */
static void vp_suit_key_core(const int *r, const int *s, int n, char *key)
{
    int perm[4] = { 0, 1, 2, 3 };
    char best[11];
    best[0] = '\0';
    do {
        char buf[11];
        char *p = buf;
        int g = 0;
        while (g < n) {
            int h = g;
            while (h < n && r[h] == r[g])
                h++;
            int idx[5];
            for (int a = g; a < h; a++)
                idx[a - g] = a;
            for (int a = 1; a < h - g; a++) {
                int v = idx[a], b = a;
                while (b > 0 && perm[s[idx[b - 1]]] > perm[s[v]]) {
                    idx[b] = idx[b - 1];
                    b--;
                }
                idx[b] = v;
            }
            for (int a = 0; a < h - g; a++) {
                *p++ = (char)('0' + r[idx[a]]);
                *p++ = (char)('a' + perm[s[idx[a]]]);
            }
            g = h;
        }
        *p = '\0';
        if (best[0] == '\0' || strcmp(buf, best) < 0) {
            size_t best_len = (size_t)(p - buf) + 1;
            if (best_len <= sizeof(best)) {
                for (size_t i = 0; i < best_len; i++)
                    best[i] = buf[i];
            }
        }
    } while (vp_next_perm(perm, 4));

    /* Copy only the encoded key and its terminator.  Callers may reserve a
     * prefix byte (the joker marker), so copying the full local buffer here
     * would write one byte past their destination. */
    size_t key_len = (size_t)(2 * n) + 1;
    if (key_len <= sizeof(best)) {
        for (size_t i = 0; i < key_len; i++)
            key[i] = best[i];
    }
}

static void vp_suit_key(StdDeck_CardMask hand, int n, char *key)
{
    int r[5], s[5];
    int i = 0;
    for (int c = 0; c < StdDeck_N_CARDS && i < n; c++)
        if (StdDeck_CardMask_CARD_IS_SET(hand, c)) {
            r[i] = StdDeck_RANK(c);
            s[i] = StdDeck_SUIT(c);
            i++;
        }
    if (i != n)
        return;

    for (int a = 1; a < n; a++) {
        int rr = r[a], ss = s[a], b = a;
        while (b > 0 && (r[b - 1] > rr || (r[b - 1] == rr && s[b - 1] > ss))) {
            r[b] = r[b - 1];
            s[b] = s[b - 1];
            b--;
        }
        r[b] = rr;
        s[b] = ss;
    }

    vp_suit_key_core(r, s, n, key);
}

/* Key of a t-card set that may contain the joker. The joker is encoded as a
 * fixed 'j' prefix plus the suit-isomorphism key of the natural cards, so a
 * natural suit relabeling can never collide with it. */
static void vp_set_key(StdDeck_CardMask set, int t, char *key)
{
    int r[5], s[5];
    int i = 0;
    int has_joker = 0;
#ifdef USE_INT64
    has_joker = (set.cards_n & (1ULL << VP_JOKER_BIT_LO)) != 0;
#else
    has_joker = (set.cards_nn.n2 & (1u << VP_JOKER_BIT_HI)) != 0;
#endif
    for (int c = 0; c < StdDeck_N_CARDS && i < t; c++)
        if (StdDeck_CardMask_CARD_IS_SET(set, c)) {
            r[i] = StdDeck_RANK(c);
            s[i] = StdDeck_SUIT(c);
            i++;
        }
    if (has_joker) {
        if (i != t - 1)
            return;
        *key++ = 'j';
    } else if (i != t) {
        return;
    }

    for (int a = 1; a < i; a++) {
        int rr = r[a], ss = s[a], b = a;
        while (b > 0 && (r[b - 1] > rr || (r[b - 1] == rr && s[b - 1] > ss))) {
            r[b] = r[b - 1];
            s[b] = s[b - 1];
            b--;
        }
        r[b] = rr;
        s[b] = ss;
    }

    vp_suit_key_core(r, s, i, key);
    if (key[0] == '\0') { /* the empty set (t = 0): give it a non-empty key,
                           * otherwise it collides with the table sentinel */
        key[0] = 'e';
        key[1] = '\0';
    }
}

static uint64_t vp_key_hash(const char *key)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *key; key++) {
        h ^= (unsigned char)*key;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Enumerate every 5-card deal of the 52-card deck, group it by suit
 * isomorphism (vp_suit_key) and count orbit sizes. Returns the number of
 * deals (C(52,5)) or -1 on failure. */
static long long vp_build_classes(vp_class_slot_t *table, int *nclasses_out)
{
    long long ndeals = 0;
    int nclasses = 0;
    for (int a = 0; a < StdDeck_N_CARDS - 4; a++)
        for (int b = a + 1; b < StdDeck_N_CARDS - 3; b++)
            for (int c = b + 1; c < StdDeck_N_CARDS - 2; c++)
                for (int d = c + 1; d < StdDeck_N_CARDS - 1; d++)
                    for (int e = d + 1; e < StdDeck_N_CARDS; e++) {
                        StdDeck_CardMask m;
                        StdDeck_CardMask_RESET(m);
                        StdDeck_CardMask_SET(m, a);
                        StdDeck_CardMask_SET(m, b);
                        StdDeck_CardMask_SET(m, c);
                        StdDeck_CardMask_SET(m, d);
                        StdDeck_CardMask_SET(m, e);
                        char key[11];
                        vp_suit_key(m, 5, key);
                        vp_class_slot_t *s =
                            vp_class_insert(table, vp_key_hash(key), key);
                        if (s->orbit == 0) {
                            s->rep = m;
                            nclasses++;
                        }
                        s->orbit++;
                        ndeals++;
                    }
    *nclasses_out = nclasses;
    return ndeals;
}

/* Enumerate every 4-card deal of the 52-card deck plus the joker (joker
 * poker only), grouped by suit isomorphism of the 4 natural cards. Returns
 * the number of deals (C(52,4)) or -1 on failure. */
static long long vp_build_joker_classes(vp_class_slot_t *table,
                                        int *nclasses_out)
{
    long long ndeals = 0;
    int nclasses = 0;
    for (int a = 0; a < StdDeck_N_CARDS - 3; a++)
        for (int b = a + 1; b < StdDeck_N_CARDS - 2; b++)
            for (int c = b + 1; c < StdDeck_N_CARDS - 1; c++)
                for (int d = c + 1; d < StdDeck_N_CARDS; d++) {
                    StdDeck_CardMask m;
                    StdDeck_CardMask_RESET(m);
                    StdDeck_CardMask_SET(m, a);
                    StdDeck_CardMask_SET(m, b);
                    StdDeck_CardMask_SET(m, c);
                    StdDeck_CardMask_SET(m, d);
                    char key[11];
                    vp_suit_key(m, 4, key);
                    vp_class_slot_t *s =
                        vp_class_insert(table, vp_key_hash(key), key);
                    if (s->orbit == 0) {
                        s->rep = m;
                        vp_set_card(&s->rep, PE_VP_JOKER_CARD);
                        nclasses++;
                    }
                    s->orbit++;
                    ndeals++;
                }
    *nclasses_out = nclasses;
    return ndeals;
}

/* ---- per-class optimal strategy ---------------------------------------- */

/* Value function context: the variant plus the payout vector under which the
 * optimizer maximises expected return. */
typedef struct {
    pe_video_poker_variant_t variant;
    const double *payouts;
    int num_categories;
} vp_ctx_t;

static double vp_value(StdDeck_CardMask hand, void *vctx)
{
    vp_ctx_t *ctx = (vp_ctx_t *)vctx;
    int cat = pe_video_poker_category(ctx->variant, hand);
    return cat >= 0 ? ctx->payouts[cat] : 0.0;
}

/* ---- completion table --------------------------------------------------- */

/* The optimal discard of a deal is the mask maximizing the expected paytable
 * payout over the 32 keep/discard masks. Doing this by enumerating the
 * C(47,k) draws of every mask of every deal costs 6.75e12 evaluations for
 * the full deal space, so instead we precompute, for every suit-isomorphism
 * class of t-card sets (t = 0..5), the category counts of all completions to
 * a 5-card hand. The counts of a specific (deal, mask) are then recovered
 * exactly by inclusion-exclusion over the discarded natural cards (at most
 * 2^5 subsets). All video-poker categories are suit symmetric, so the
 * completion distribution of a set depends only on its isomorphism class;
 * this is what collapses the per-deal enumeration to a per-class one.
 *
 * The joker is never drawn, so a discarded joker contributes to the draw
 * count but never appears in the excluded subsets (its "finals" are not
 * phantom completions of the kept set). */
#define VP_CNT_TABLE_BITS 21
#define VP_CNT_TABLE_SIZE (1u << VP_CNT_TABLE_BITS)

typedef struct {
    uint64_t hash;
    char key[11];
    StdDeck_CardMask rep;
    long long counts[PE_PAYTABLE_MAX_ROWS];
    int used;
} vp_cnt_slot_t;

static vp_cnt_slot_t *vp_cnt_insert(vp_cnt_slot_t *table, uint64_t hash,
                                    const char *key)
{
    size_t idx = (size_t)hash & (VP_CNT_TABLE_SIZE - 1);
    for (;;) {
        vp_cnt_slot_t *s = &table[idx];
        if (s->key[0] == '\0') {
            s->hash = hash;
            strncpy(s->key, key, 10);
            s->key[10] = '\0';
            return s;
        }
        if (s->hash == hash && strncmp(s->key, key, 10) == 0)
            return s;
        idx = (idx + 1) & (VP_CNT_TABLE_SIZE - 1);
    }
}

static vp_cnt_slot_t *vp_cnt_find(vp_cnt_slot_t *table, uint64_t hash,
                                  const char *key)
{
    size_t idx = (size_t)hash & (VP_CNT_TABLE_SIZE - 1);
    for (;;) {
        vp_cnt_slot_t *s = &table[idx];
        if (s->key[0] == '\0')
            return NULL;
        if (s->hash == hash && strncmp(s->key, key, 10) == 0)
            return s;
        idx = (idx + 1) & (VP_CNT_TABLE_SIZE - 1);
    }
}

/* Enumerate the completions of the t-card set T (draws of 5-t natural cards;
 * the joker, if present in T, is never drawn) and accumulate the category
 * counts into the slot. */
static int vp_slot_completions(vp_cnt_slot_t *slot, StdDeck_CardMask T, int t,
                               pe_video_poker_variant_t variant)
{
    int pool[52];
    int npool = 0;
    for (int c = 0; c < StdDeck_N_CARDS; c++)
        if (!StdDeck_CardMask_CARD_IS_SET(T, c))
            pool[npool++] = c;

    int draw = 5 - t;
    if (draw == 0) {
        int cat = pe_video_poker_category(variant, T);
        if (cat < 0)
            return -1;
        slot->counts[cat]++;
        return 0;
    }

    int combo[5];
    for (int b = 0; b < draw; b++)
        combo[b] = b;
    do {
        StdDeck_CardMask drawn;
        StdDeck_CardMask_RESET(drawn);
        for (int b = 0; b < draw; b++)
            StdDeck_CardMask_SET(drawn, pool[combo[b]]);
        StdDeck_CardMask res;
        StdDeck_CardMask_OR(res, T, drawn);
        int cat = pe_video_poker_category(variant, res);
        if (cat < 0)
            return -1;
        slot->counts[cat]++;
    } while (vp_next_combination(combo, draw, npool));
    return 0;
}

typedef struct {
    vp_cnt_slot_t *cnt;
    pe_video_poker_variant_t variant;
    int ncats;
    int nat;
    int has_j;
    int err;
} vp_cnt_build_t;

static void vp_cnt_set_cb(int *set, void *ud)
{
    vp_cnt_build_t *b = (vp_cnt_build_t *)ud;
    StdDeck_CardMask T;
    StdDeck_CardMask_RESET(T);
    for (int i = 0; i < b->nat; i++)
        StdDeck_CardMask_SET(T, set[i]);
    int t = b->nat + b->has_j;
    if (b->has_j)
        vp_set_card(&T, PE_VP_JOKER_CARD);

    char key[11];
    vp_set_key(T, t, key);
    vp_cnt_slot_t *slot = vp_cnt_insert(b->cnt, vp_key_hash(key), key);
    if (!slot->used) {
        slot->used = 1;
        slot->rep = T;
        if (vp_slot_completions(slot, T, t, b->variant) != 0)
            b->err = 1;
    }
}

static void vp_gen_combos(int n, int k, int *out, int pos, int start,
                          void (*cb)(int *, void *), void *ud)
{
    if (pos == k) {
        cb(out, ud);
        return;
    }
    for (int i = start; i < n; i++) {
        out[pos] = i;
        vp_gen_combos(n, k, out, pos + 1, i + 1, cb, ud);
    }
}

/* Build the completion table for the variant: every suit-isomorphism class
 * of t-card sets (t = 0..5) over the 52-card natural deck, optionally
 * including the joker (joker poker only). Returns 0 on success. */
static int vp_build_completion_table(vp_cnt_slot_t *cnt,
                                     pe_video_poker_variant_t variant,
                                     int ncats)
{
    vp_cnt_build_t b = { cnt, variant, ncats, 0, 0, 0 };
    int joker_variant = (variant == PE_VP_JOKER_POKER);
    for (int t = 0; t <= 5 && !b.err; t++) {
        for (int has_j = 0; has_j <= (joker_variant ? 1 : 0); has_j++) {
            b.nat = t - has_j;
            b.has_j = has_j;
            if (b.nat < 0 || b.nat > 5)
                continue;
            int set[5];
            vp_gen_combos(52, b.nat, set, 0, 0, vp_cnt_set_cb, &b);
            if (b.err)
                return -1;
        }
    }
    return 0;
}

/* C(n,k) for the small n/k that appear here (n <= 52). */
static long long vp_choose(int n, int k)
{
    if (k < 0 || k > n)
        return 0;
    long long r = 1;
    for (int i = 1; i <= k; i++)
        r = r * (long long)(n - (k - i)) / i;
    return r;
}

/* Optimal discard of one deal class: compare the 32 masks on expected
 * paytable payout, computed from the completion table by inclusion-exclusion
 * over the discarded natural cards. Accumulates the weighted per-category
 * outcome frequencies into counts[] and stores the optimal mask. */
static int vp_derive_class(vp_ctx_t *ctx, vp_cnt_slot_t *cnt,
                           StdDeck_CardMask deal, long long *counts,
                           int *best_mask_out)
{
    int ncats = ctx->num_categories;
    int deal_has_joker = vp_card_is_set(deal, PE_VP_JOKER_CARD);
    int hc[5];
    int nh = 0;
    int hi = deal_has_joker ? PE_VP_JOKER_CARD : (StdDeck_N_CARDS - 1);
    for (int c = 0; c <= hi && nh < 5; c++)
        if (vp_card_is_set(deal, c))
            hc[nh++] = c;
    if (nh != 5)
        return -1;

    int best_mask = 0;
    double best_ev = -1.0;
    long long best_n[PE_PAYTABLE_MAX_ROWS];
    memset(best_n, 0, sizeof(best_n));

    for (int mask = 0; mask < 32; mask++) {
        int ndisc = 0;
        int discarded[5];
        StdDeck_CardMask K;
        StdDeck_CardMask_RESET(K);
        for (int b = 0; b < 5; b++) {
            if (mask & (1 << b))
                discarded[ndisc++] = b;
            else
                vp_set_card(&K, hc[b]);
        }

        /* Subsets of the discarded NATURAL cards (the joker is never drawn,
         * so its finals are not phantom completions to exclude). */
        int ndisc_nat = 0;
        int discarded_nat[5];
        for (int b = 0; b < ndisc; b++)
            if (hc[discarded[b]] != PE_VP_JOKER_CARD)
                discarded_nat[ndisc_nat++] = discarded[b];

        long long N[PE_PAYTABLE_MAX_ROWS];
        memset(N, 0, sizeof(N));
        for (int sub = 0; sub < (1 << ndisc_nat); sub++) {
            StdDeck_CardMask T = K;
            int bits = 0;
            for (int b = 0; b < ndisc_nat; b++)
                if (sub & (1 << b)) {
                    vp_set_card(&T, hc[discarded_nat[b]]);
                    bits++;
                }
            char key[11];
            vp_set_key(T, 5 - ndisc + bits, key);
            vp_cnt_slot_t *s = vp_cnt_find(cnt, vp_key_hash(key), key);
            if (s == NULL)
                return -1;
            /* For a natural deal in joker poker the joker is still in the
             * draw pool, so the finals of the kept set include the draws
             * that bring the joker: the joker-set slot of T. It is empty
             * when T already has 5 cards (nothing left to draw). */
            vp_cnt_slot_t *sj = NULL;
            if (ctx->variant == PE_VP_JOKER_POKER && !deal_has_joker &&
                bits < ndisc) {
                char jkey[11];
                size_t key_len = (5 - ndisc + bits) == 0
                                     ? 1
                                     : (size_t)(2 * (5 - ndisc + bits));
                jkey[0] = 'j';
                if (key_len + 2 > sizeof(jkey))
                    return -1;
                for (size_t i = 0; i < key_len; i++)
                    jkey[i + 1] = key[i];
                jkey[key_len + 1] = '\0';
                sj = vp_cnt_find(cnt, vp_key_hash(jkey), jkey);
                if (sj == NULL)
                    return -1;
            }
            if (bits & 1) {
                for (int c = 0; c < ncats; c++)
                    N[c] -= s->counts[c];
                if (sj != NULL)
                    for (int c = 0; c < ncats; c++)
                        N[c] -= sj->counts[c];
            } else {
                for (int c = 0; c < ncats; c++)
                    N[c] += s->counts[c];
                if (sj != NULL)
                    for (int c = 0; c < ncats; c++)
                        N[c] += sj->counts[c];
            }
        }

        long long ndraws = 0;
        double ev = 0.0;
        for (int c = 0; c < ncats; c++) {
            ndraws += N[c];
            ev += ctx->payouts[c] * (double)N[c];
        }
        long long expected =
            vp_choose(ctx->variant == PE_VP_JOKER_POKER ? 48 : 47, ndisc);
        if (ndraws != expected)
            return -1; /* inclusion-exclusion identity violated */
        if (ndraws > 0)
            ev /= (double)ndraws;
        if (ev > best_ev) {
            best_ev = ev;
            best_mask = mask;
            for (int c = 0; c < ncats; c++)
                best_n[c] = N[c];
        }
    }

    int ndisc = 0;
    for (int b = 0; b < 5; b++)
        if (best_mask & (1 << b))
            ndisc++;
    long long w = ctx->variant == PE_VP_JOKER_POKER
                      ? VP_WEIGHT_53[ndisc]
                      : VP_WEIGHT_52[ndisc];
    for (int c = 0; c < ncats; c++)
        counts[c] = w * best_n[c];
    if (best_mask_out != NULL)
        *best_mask_out = best_mask;
    return 0;
}

/* Cross-check the table method against the direct draw-optimizer enumeration
 * (pe_compute_draw_optima_fn for 52-card games; the local 53-card enumerator
 * for joker poker, which the optimizer does not support). Verifies that the
 * table's optimal mask is also optimal under the direct enumeration and that
 * the expected values agree. Returns 0 on match. */
static int vp_crosscheck_direct(vp_ctx_t *ctx, vp_cnt_slot_t *cnt,
                                StdDeck_CardMask deal)
{
    long long counts[PE_PAYTABLE_MAX_ROWS];
    int table_mask;
    if (vp_derive_class(ctx, cnt, deal, counts, &table_mask) != 0)
        return -1;
    double table_ev = 0.0;
    long long ndraws = 0;
    for (int c = 0; c < ctx->num_categories; c++) {
        table_ev += ctx->payouts[c] * (double)counts[c];
        ndraws += counts[c];
    }
    if (ndraws <= 0)
        return -1;
    table_ev /= (double)ndraws;

    if (ctx->variant != PE_VP_JOKER_POKER) {
        StdDeck_CardMask board, dead;
        StdDeck_CardMask_RESET(board);
        StdDeck_CardMask_RESET(dead);
        pe_draw_result_t res;
        if (pe_compute_draw_optima_fn(deal, board, dead, vp_value, ctx, &res) != 0)
            return -1;
        if (fabs(res.max_equity - table_ev) > 1e-9)
            return -1;
        /* The table's mask must be optimal under the direct enumeration too
         * (tolerating floating-point tie flips between equal-EV masks). */
        if (fabs(res.options[table_mask].expected_equity - res.max_equity) >
            1e-9)
            return -1;
        return 0;
    }

    /* Joker poker: direct 53-card enumeration mirroring the optimizer. */
    double ev[32];
    int best_direct = 0;
    int pool[48];
    int npool = 0;
    int hc[5];
    int nh = 0;
    for (int c = 0; c <= PE_VP_JOKER_CARD; c++)
        if (vp_card_is_set(deal, c))
            hc[nh++] = c;
        else
            pool[npool++] = c;
    if (nh != 5)
        return -1;
    for (int mask = 0; mask < 32; mask++) {
        int k = 0;
        StdDeck_CardMask kept = deal;
        for (int b = 0; b < 5; b++)
            if (mask & (1 << b)) {
                k++;
                vp_unset_card(&kept, hc[b]);
            }
        double sum = 0.0;
        long long n = 0;
        if (k == 0) {
            sum = vp_value(kept, ctx);
            n = 1;
        } else {
            int combo[5];
            for (int b = 0; b < k; b++)
                combo[b] = b;
            do {
                StdDeck_CardMask drawn;
                StdDeck_CardMask_RESET(drawn);
                for (int b = 0; b < k; b++)
                    vp_set_card(&drawn, pool[combo[b]]);
                StdDeck_CardMask res;
                StdDeck_CardMask_OR(res, kept, drawn);
                sum += vp_value(res, ctx);
                n++;
            } while (vp_next_combination(combo, k, npool));
        }
        ev[mask] = (n > 0) ? (sum / (double)n) : 0.0;
        if (ev[mask] > ev[best_direct])
            best_direct = mask;
    }
    if (fabs(ev[best_direct] - table_ev) > 1e-9)
        return -1;
    if (fabs(ev[table_mask] - ev[best_direct]) > 1e-9)
        return -1;
    return 0;
}

/* ---- derivation -------------------------------------------------------- */

int pe_video_poker_derive_strategy(pe_video_poker_variant_t variant,
                                   const double *payouts, int num_payouts,
                                   pe_vp_derived_strategy_t *out)
{
    int ncats = pe_video_poker_num_categories(variant);
    if (ncats <= 0)
        return -1;
    if (payouts == NULL || out == NULL)
        return -1;
    if (num_payouts != ncats)
        return -1;
    for (int i = 0; i < ncats; i++)
        if (!(payouts[i] >= 0.0))
            return -1;

    vp_ctx_t ctx = { variant, payouts, ncats };

    vp_class_slot_t *table =
        (vp_class_slot_t *)calloc(VP_CLASS_TABLE_SIZE, sizeof(vp_class_slot_t));
    if (table == NULL)
        return -1;
    vp_cnt_slot_t *cnt =
        (vp_cnt_slot_t *)calloc(VP_CNT_TABLE_SIZE, sizeof(vp_cnt_slot_t));
    if (cnt == NULL) {
        free(table);
        return -1;
    }

    if (vp_build_completion_table(cnt, variant, ncats) != 0) {
        free(table);
        free(cnt);
        return -1;
    }

    int nclasses = 0;
    long long ndeals = vp_build_classes(table, &nclasses);
    if (ndeals < 0) {
        free(table);
        free(cnt);
        return -1;
    }
    long long joker_deals = 0;
    if (variant == PE_VP_JOKER_POKER) {
        joker_deals = vp_build_joker_classes(table, &nclasses);
        if (joker_deals < 0) {
            free(table);
            free(cnt);
            return -1;
        }
    }

    /* Cross-check a few class representatives against the direct draw
     * optimizer to tie the table method to the optimizer semantics. */
    {
        int checked = 0;
        for (size_t i = 0; i < VP_CLASS_TABLE_SIZE && checked < 8; i++) {
            vp_class_slot_t *s = &table[i];
            if (s->key[0] == '\0')
                continue;
            if (vp_crosscheck_direct(&ctx, cnt, s->rep) != 0) {
                free(table);
                free(cnt);
                return -1;
            }
            checked++;
        }
    }

    long long total_counts[PE_PAYTABLE_MAX_ROWS] = { 0 };
    int err = 0;

#ifdef _OPENMP
#pragma omp parallel
    {
        long long local[PE_PAYTABLE_MAX_ROWS] = { 0 };
#pragma omp for schedule(dynamic, 8)
        for (int i = 0; i < (int)VP_CLASS_TABLE_SIZE; i++) {
            vp_class_slot_t *s = &table[i];
            if (s->key[0] == '\0')
                continue;
            long long per_class[PE_PAYTABLE_MAX_ROWS] = { 0 };
            if (vp_derive_class(&ctx, cnt, s->rep, per_class, NULL) != 0) {
#pragma omp critical(vp_derivation_error)
                err = 1;
                continue;
            }
            for (int c = 0; c < ncats; c++)
                local[c] += per_class[c] * s->orbit;
        }
#pragma omp critical
        for (int c = 0; c < ncats; c++)
            total_counts[c] += local[c];
    }
#else
    for (size_t i = 0; i < VP_CLASS_TABLE_SIZE; i++) {
        vp_class_slot_t *s = &table[i];
        if (s->key[0] == '\0')
            continue;
        long long per_class[PE_PAYTABLE_MAX_ROWS] = { 0 };
        if (vp_derive_class(&ctx, cnt, s->rep, per_class, NULL) != 0) {
            err = 1;
            continue;
        }
        for (int c = 0; c < ncats; c++)
            total_counts[c] += per_class[c] * s->orbit;
    }
#endif

    free(table);
    free(cnt);
    if (err)
        return -1;

    long long total = 0;
    for (int c = 0; c < ncats; c++)
        total += total_counts[c];
    if (total <= 0)
        return -1;

    const char *const *names = NULL;
    switch (variant) {
    case PE_VP_JACKS_OR_BETTER:
        names = VP_NAMES_JOB;
        break;
    case PE_VP_DEUCES_WILD:
        names = VP_NAMES_DW;
        break;
    case PE_VP_JOKER_POKER:
        names = VP_NAMES_JP;
        break;
    default:
        return -1;
    case PE_VP_VARIANT_COUNT:
        return -1;
    }

    out->num_categories = ncats;
    out->total_combinations = total;
    out->num_deals = ndeals + joker_deals;
    double ev_sum = 0.0;
    for (int c = 0; c < ncats; c++) {
        out->categories[c].name = names[c];
        out->categories[c].payout = payouts[c];
        out->categories[c].combinations = total_counts[c];
        ev_sum += payouts[c] * (double)total_counts[c];
    }
    out->total_ev = ev_sum / (double)total;

    return 0;
}
