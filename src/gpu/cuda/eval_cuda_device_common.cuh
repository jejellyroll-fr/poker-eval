#pragma once

#include <stdint.h>

typedef uint32_t HandVal;

struct CudaCardMask {
    uint32_t cards_n;
    uint32_t cards[4]; /* spades, hearts, diamonds, clubs */
};

enum {
    StdRules_HandType_NOPAIR = 0,
    StdRules_HandType_ONEPAIR = 1,
    StdRules_HandType_TWOPAIR = 2,
    StdRules_HandType_TRIPS = 3,
    StdRules_HandType_STRAIGHT = 4,
    StdRules_HandType_FLUSH = 5,
    StdRules_HandType_FULLHOUSE = 6,
    StdRules_HandType_QUADS = 7,
    StdRules_HandType_STFLUSH = 8
};

#define HandVal_HANDTYPE_SHIFT 24
#define HandVal_HANDTYPE_MASK (0x0f << HandVal_HANDTYPE_SHIFT)
#define HandVal_CARDS_SHIFT 0
#define HandVal_CARDS_MASK (0x000fffff << HandVal_CARDS_SHIFT)
#define HandVal_TOP_CARD_SHIFT 16
#define HandVal_TOP_CARD_MASK (0x0f << HandVal_TOP_CARD_SHIFT)
#define HandVal_SECOND_CARD_SHIFT 12
#define HandVal_SECOND_CARD_MASK (0x0f << HandVal_SECOND_CARD_SHIFT)
#define HandVal_THIRD_CARD_SHIFT 8
#define HandVal_THIRD_CARD_MASK (0x0f << HandVal_THIRD_CARD_SHIFT)
#define HandVal_FOURTH_CARD_SHIFT 4
#define HandVal_FOURTH_CARD_MASK (0x0f << HandVal_FOURTH_CARD_SHIFT)
#define HandVal_FIFTH_CARD_SHIFT 0
#define HandVal_FIFTH_CARD_MASK (0x0f << HandVal_FIFTH_CARD_SHIFT)
#define HandVal_CARD_WIDTH 4
#define HandVal_CARD_MASK 0x0f

#define HandVal_HANDTYPE_VALUE(ht) ((ht) << HandVal_HANDTYPE_SHIFT)
#define HandVal_TOP_CARD_VALUE(c) ((c) << HandVal_TOP_CARD_SHIFT)
#define HandVal_SECOND_CARD_VALUE(c) ((c) << HandVal_SECOND_CARD_SHIFT)
#define HandVal_THIRD_CARD_VALUE(c) ((c) << HandVal_THIRD_CARD_SHIFT)

#if defined(__CUDACC__)
#pragma nv_diag_push
#pragma nv_diag_suppress 20044
#endif

#if defined(EVAL_CUDA_DEFINE_CONSTANTS)
#define CUDA_CONSTANT_DECL __device__ __constant__
#else
#define CUDA_CONSTANT_DECL extern __device__ __constant__
#endif

CUDA_CONSTANT_DECL uint8_t d_nBitsTable[8192];
CUDA_CONSTANT_DECL uint8_t d_straightTable[8192];
CUDA_CONSTANT_DECL uint32_t d_topFiveCardsTable[8192];
CUDA_CONSTANT_DECL uint8_t d_topCardTable[8192];

#undef CUDA_CONSTANT_DECL

#if defined(__CUDACC__)
#pragma nv_diag_pop
#endif

static __device__ __forceinline__ HandVal cuda_eval_5cards(CudaCardMask cards, int n_cards) {
    HandVal retval;
    uint32_t ranks, four_mask, three_mask, two_mask;
    uint32_t n_dups, n_ranks;
    uint32_t sc, sd, sh, ss;

    ss = cards.cards[0]; /* spades */
    sh = cards.cards[1]; /* hearts */
    sd = cards.cards[2]; /* diamonds */
    sc = cards.cards[3]; /* clubs */

    retval = 0;
    ranks = sc | sd | sh | ss;
    n_ranks = d_nBitsTable[ranks];
    n_dups = n_cards - n_ranks;

    /* Check for straight, flush, or straight flush */
    if (n_ranks >= 5) {
        if (d_nBitsTable[ss] >= 5) {
            if (d_straightTable[ss])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(d_straightTable[ss]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + d_topFiveCardsTable[ss];
        }
        else if (d_nBitsTable[sc] >= 5) {
            if (d_straightTable[sc])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(d_straightTable[sc]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + d_topFiveCardsTable[sc];
        }
        else if (d_nBitsTable[sd] >= 5) {
            if (d_straightTable[sd])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(d_straightTable[sd]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + d_topFiveCardsTable[sd];
        }
        else if (d_nBitsTable[sh] >= 5) {
            if (d_straightTable[sh])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(d_straightTable[sh]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + d_topFiveCardsTable[sh];
        }
        else {
            int st = d_straightTable[ranks];
            if (st)
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_STRAIGHT)
                    + HandVal_TOP_CARD_VALUE(st);
        }

        if (retval && n_dups < 3)
            return retval;
    }

    /* Handle different numbers of duplicates */
    switch (n_dups) {
    case 0:
        /* No pair */
        return HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)
            + d_topFiveCardsTable[ranks];

    case 1: {
        /* One pair */
        uint32_t t, kickers;
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR)
            + HandVal_TOP_CARD_VALUE(d_topCardTable[two_mask]);
        t = ranks ^ two_mask;
        kickers = (d_topFiveCardsTable[t] >> HandVal_CARD_WIDTH)
            & ~HandVal_FIFTH_CARD_MASK;
        retval += kickers;
        return retval;
    }

    case 2:
        /* Two pair or trips */
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (two_mask) {
            uint32_t t;
            t = ranks ^ two_mask;
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR)
                + (d_topFiveCardsTable[two_mask]
                   & (HandVal_TOP_CARD_MASK | HandVal_SECOND_CARD_MASK))
                + HandVal_THIRD_CARD_VALUE(d_topCardTable[t]);
            return retval;
        }
        else {
            int t, second;
            three_mask = ((sc&sd)|(sh&ss)) & ((sc&sh)|(sd&ss));
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS)
                + HandVal_TOP_CARD_VALUE(d_topCardTable[three_mask]);
            t = ranks ^ three_mask;
            second = d_topCardTable[t];
            retval += HandVal_SECOND_CARD_VALUE(second);
            t ^= (1 << second);
            retval += HandVal_THIRD_CARD_VALUE(d_topCardTable[t]);
            return retval;
        }

    default:
        /* Quads, full house, or two pair */
        four_mask = sh & sd & sc & ss;
        if (four_mask) {
            int tc;
            tc = d_topCardTable[four_mask];
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS)
                + HandVal_TOP_CARD_VALUE(tc)
                + HandVal_SECOND_CARD_VALUE(d_topCardTable[ranks ^ (1 << tc)]);
            return retval;
        }

        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (d_nBitsTable[two_mask] != n_dups) {
            /* Full house */
            int tc, t;
            three_mask = ((sc&sd)|(sh&ss)) & ((sc&sh)|(sd&ss));
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE);
            tc = d_topCardTable[three_mask];
            retval += HandVal_TOP_CARD_VALUE(tc);
            t = (two_mask | three_mask) ^ (1 << tc);
            retval += HandVal_SECOND_CARD_VALUE(d_topCardTable[t]);
            return retval;
        }

        if (retval) /* flush and straight */
            return retval;
        else {
            /* Two pair */
            int top, second;
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR);
            top = d_topCardTable[two_mask];
            retval += HandVal_TOP_CARD_VALUE(top);
            second = d_topCardTable[two_mask ^ (1 << top)];
            retval += HandVal_SECOND_CARD_VALUE(second);
            retval += HandVal_THIRD_CARD_VALUE(d_topCardTable[ranks ^ (1 << top)
                                                            ^ (1 << second)]);
            return retval;
        }
    }
}
