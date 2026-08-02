/*
 * eval_low_kernel.cl: OpenCL kernels for low hand evaluation
 *
 * Implements A-5 lowball (Razz, Omaha8, Stud8) and 2-7 lowball evaluation
 * Ported from CUDA implementation
 *
 * NOTE: Type definitions (HandVal, LowHandVal, OpenCLCardMask) are in eval_kernel.cl
 * which is loaded first when combining kernel sources.
 */

#ifndef OPENCL_POKER_TYPES_DEFINED
#define OPENCL_POKER_TYPES_DEFINED
typedef uint HandVal;
typedef uint LowHandVal;
typedef struct {
    uint cards_n;
    uint cards[4];
} OpenCLCardMask;
#endif

#ifndef OPENCL_HANDVAL_MACROS_DEFINED
#define OPENCL_HANDVAL_MACROS_DEFINED
#define StdRules_HandType_NOPAIR    0
#define StdRules_HandType_ONEPAIR   1
#define StdRules_HandType_TWOPAIR   2
#define StdRules_HandType_TRIPS     3
#define StdRules_HandType_STRAIGHT  4
#define StdRules_HandType_FLUSH     5
#define StdRules_HandType_FULLHOUSE 6
#define StdRules_HandType_QUADS     7
#define StdRules_HandType_STFLUSH   8
#define HandVal_HANDTYPE_SHIFT      24
#define HandVal_TOP_CARD_SHIFT      16
#define HandVal_SECOND_CARD_SHIFT   12
#define HandVal_THIRD_CARD_SHIFT    8
#define HandVal_FOURTH_CARD_SHIFT   4
#define HandVal_FIFTH_CARD_SHIFT    0
#define HandVal_CARD_WIDTH          4
#define HandVal_CARD_MASK           0x0f
#define HandVal_TOP_CARD_MASK       (0x0f << 16)
#define HandVal_SECOND_CARD_MASK    (0x0f << 12)
#define HandVal_FIFTH_CARD_MASK     0x0f
#define HandVal_HANDTYPE(hv)          ((hv) >> HandVal_HANDTYPE_SHIFT)
#define HandVal_HANDTYPE_VALUE(ht)    ((ht) << HandVal_HANDTYPE_SHIFT)
#define HandVal_TOP_CARD_VALUE(c)     ((c) << HandVal_TOP_CARD_SHIFT)
#define HandVal_SECOND_CARD_VALUE(c)  ((c) << HandVal_SECOND_CARD_SHIFT)
#define HandVal_THIRD_CARD_VALUE(c)   ((c) << HandVal_THIRD_CARD_SHIFT)
#define HandVal_FOURTH_CARD_VALUE(c)  ((c) << HandVal_FOURTH_CARD_SHIFT)
#define HandVal_FIFTH_CARD_VALUE(c)   ((c) << HandVal_FIFTH_CARD_SHIFT)
#endif

#ifndef OPENCL_STDDECK_RANKS_DEFINED
#define OPENCL_STDDECK_RANKS_DEFINED
#define StdDeck_Rank_2              0
#define StdDeck_Rank_3              1
#define StdDeck_Rank_4              2
#define StdDeck_Rank_5              3
#define StdDeck_Rank_6              4
#define StdDeck_Rank_7              5
#define StdDeck_Rank_8              6
#define StdDeck_Rank_9              7
#define StdDeck_Rank_TEN            8
#define StdDeck_Rank_JACK           9
#define StdDeck_Rank_QUEEN          10
#define StdDeck_Rank_KING           11
#define StdDeck_Rank_ACE            12
#endif

/* Low hand value constants and helpers (mirror CPU definitions) */
#ifndef OPENCL_LOW_HANDVAL_ALIASES_DEFINED
#define OPENCL_LOW_HANDVAL_ALIASES_DEFINED
#define LowHandVal_CARD_WIDTH           HandVal_CARD_WIDTH
#define LowHandVal_HANDTYPE(hv)         HandVal_HANDTYPE(hv)
#define LowHandVal_HANDTYPE_VALUE(ht)   HandVal_HANDTYPE_VALUE(ht)
#define LowHandVal_TOP_CARD_VALUE(c)    HandVal_TOP_CARD_VALUE(c)
#define LowHandVal_SECOND_CARD_VALUE(c) HandVal_SECOND_CARD_VALUE(c)
#define LowHandVal_THIRD_CARD_VALUE(c)  HandVal_THIRD_CARD_VALUE(c)
#define LowHandVal_FOURTH_CARD_VALUE(c) HandVal_FOURTH_CARD_VALUE(c)
#define LowHandVal_FIFTH_CARD_VALUE(c)  HandVal_FIFTH_CARD_VALUE(c)
#define LowHandVal_TOP_CARD_SHIFT       HandVal_TOP_CARD_SHIFT
#define LowHandVal_CARD_MASK            HandVal_CARD_MASK
#endif

#ifndef OPENCL_LOW_QUALIFIER_ENUM
#define OPENCL_LOW_QUALIFIER_ENUM
#define LOW_QUALIFIER_NONE 0
#define LOW_QUALIFIER_8 1
#define LOW_QUALIFIER_7 2
#endif


#ifndef OPENCL_LOW_HANDVAL_CONSTS_DEFINED
#define OPENCL_LOW_HANDVAL_CONSTS_DEFINED
#define LowHandVal_NOTHING \
    (LowHandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) + \
     LowHandVal_TOP_CARD_VALUE(StdDeck_Rank_ACE) + 1)

#define LowHandVal_WORST_EIGHT \
    (LowHandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)     \
     + LowHandVal_TOP_CARD_VALUE(StdDeck_Rank_8 + 1)         \
     + LowHandVal_SECOND_CARD_VALUE(StdDeck_Rank_7 + 1)      \
     + LowHandVal_THIRD_CARD_VALUE(StdDeck_Rank_6 + 1)       \
     + LowHandVal_FOURTH_CARD_VALUE(StdDeck_Rank_5 + 1)      \
     + LowHandVal_FIFTH_CARD_VALUE(StdDeck_Rank_4 + 1))
#endif

/* Card rank values for low evaluation */
#define CARD_ACE 12
#define CARD_TWO 0
#define CARD_THREE 1
#define CARD_FOUR 2
#define CARD_FIVE 3
#define CARD_SIX 4
#define CARD_SEVEN 5
#define CARD_EIGHT 6

/* Rotate ranks to make Ace low (same as CPU implementation) */
#define Lowball_ROTATE_RANKS(ranks) \
    ((((ranks) & ~(1U << CARD_ACE)) << 1) | (((ranks) >> CARD_ACE) & 0x01U))

/* Bottom card table - find lowest set bit */
static inline uint opencl_bottom_card(uint ranks) {
    for (uint i = 0; i < 13; i++) {
        if (ranks & (1U << i)) return i;
    }
    return 0;
}

/* Get N bottom cards from ranks (lowest card goes to highest position) */
static inline uint opencl_bottom_n_cards(uint ranks, int n) {
    uint ordered[5];
    for (int i = 0; i < n; i++) {
        uint card = opencl_bottom_card(ranks);
        ordered[i] = card;
        ranks ^= (1U << card);
    }

    uint result = 0;
    for (int i = 0; i < n; i++) {
        uint card = ordered[n - 1 - i]; /* Highest rank first */
        result |= (card << ((n - 1 - i) * 4));
    }
    return result;
}

static inline LowHandVal opencl_eval_low8_5card(
    OpenCLCardMask cards,
    __constant uchar* nBitsTable
) {
    uint ranks = cards.cards[0] | cards.cards[1] | cards.cards[2] | cards.cards[3];
    ranks = Lowball_ROTATE_RANKS(ranks);

    if (nBitsTable[ranks & 0x1FFF] < 5) {
        return LowHandVal_NOTHING;
    }

    uint bottom5 = opencl_bottom_n_cards(ranks, 5);
    LowHandVal val = LowHandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) + bottom5;

    if (val <= LowHandVal_WORST_EIGHT) {
        return val;
    }
    return LowHandVal_NOTHING;
}

/* Lowball hand selection algorithms (ported from lowball_algorithm.c) */

/* Select 5 unique ranks (no pairs) - returns 1 if successful */
static inline int opencl_select_no_pair_ranks(const uint ss, const uint sc, const uint sd, const uint sh, int out_ranks[5]) {
    /* Count occurrences of each rank across all suits */
    int rank_counts[13];
    for (int r = 0; r < 13; r++) {
        rank_counts[r] = 0;
        uint mask = (1U << r);
        if (ss & mask) rank_counts[r]++;
        if (sc & mask) rank_counts[r]++;
        if (sd & mask) rank_counts[r]++;
        if (sh & mask) rank_counts[r]++;
    }

    /* A duplicated rank does not rule out a no-pair low: only one card per rank
       is kept and the surplus ones are skipped.  A seven-card 4-5-5-6-6-7-8
       plays 8-7-6-5-4, not a pair. */
    int found = 0;
    for (int r = 0; r < 13; r++) {
        if (rank_counts[r] > 0) {
            out_ranks[found++] = r;
            if (found == 5) return 1;
        }
    }
    return 0;
}

/* Select one pair + 3 kickers - returns 1 if successful */
static inline int opencl_select_one_pair_ranks(const uint ss, const uint sc, const uint sd, const uint sh, int out_ranks[4]) {
    int rank_counts[13];
    for (int r = 0; r < 13; r++) {
        rank_counts[r] = 0;
        uint mask = (1U << r);
        if (ss & mask) rank_counts[r]++;
        if (sc & mask) rank_counts[r]++;
        if (sd & mask) rank_counts[r]++;
        if (sh & mask) rank_counts[r]++;
    }

    for (int pair = 0; pair < 13; pair++) {
        if (rank_counts[pair] >= 2) {
            int kickers[3];
            int found = 0;
            for (int r = 0; r < 13; r++) {
                if (r != pair && rank_counts[r] > 0) {
                    kickers[found++] = r;
                    if (found == 3) break;
                }
            }
            if (found == 3) {
                out_ranks[0] = pair;
                out_ranks[1] = kickers[0];
                out_ranks[2] = kickers[1];
                out_ranks[3] = kickers[2];
                return 1;
            }
        }
    }
    return 0;
}

/* Flatten mask into lists */
static inline int opencl_flatten_cards(OpenCLCardMask mask, int ranks[52], int suits[52]) {
    int count = 0;
    for (int s = 0; s < 4; s++) {
        uint suit_mask = mask.cards[s];
        for (int r = 0; r < 13; r++) {
            if (suit_mask & (1U << r)) {
                ranks[count] = r;
                suits[count] = s;
                count++;
            }
        }
    }
    return count;
}

static inline LowHandVal opencl_encode_low(int sorted[5]) {
    LowHandVal val = 0;
    int shift = LowHandVal_TOP_CARD_SHIFT;
    for (int i = 0; i < 5; i++) {
        val |= ((LowHandVal)sorted[i] << shift);
        shift -= 4;
    }
    return val;
}

/* Select two pair + 1 kicker - returns 1 if successful */
static inline int opencl_select_two_pair_ranks(const uint ss, const uint sc, const uint sd, const uint sh, int out_ranks[3]) {
    int rank_counts[13];
    for (int r = 0; r < 13; r++) {
        rank_counts[r] = 0;
        uint mask = (1U << r);
        if (ss & mask) rank_counts[r]++;
        if (sc & mask) rank_counts[r]++;
        if (sd & mask) rank_counts[r]++;
        if (sh & mask) rank_counts[r]++;
    }

    int pairs[2];
    int found_pairs = 0;
    for (int r = 0; r < 13; r++) {
        if (rank_counts[r] >= 2) {
            pairs[found_pairs++] = r;
            if (found_pairs == 2) break;
        }
    }
    if (found_pairs < 2) return 0;

    for (int r = 0; r < 13; r++) {
        if (r != pairs[0] && r != pairs[1] && rank_counts[r] > 0) {
            out_ranks[0] = pairs[0];
            out_ranks[1] = pairs[1];
            out_ranks[2] = r;
            return 1;
        }
    }
    return 0;
}

/* Select trips + 2 kickers - returns 1 if successful */
static inline int opencl_select_trips_ranks(const uint ss, const uint sc, const uint sd, const uint sh, int out_ranks[3]) {
    int rank_counts[13];
    for (int r = 0; r < 13; r++) {
        rank_counts[r] = 0;
        uint mask = (1U << r);
        if (ss & mask) rank_counts[r]++;
        if (sc & mask) rank_counts[r]++;
        if (sd & mask) rank_counts[r]++;
        if (sh & mask) rank_counts[r]++;
    }

    for (int trips = 0; trips < 13; trips++) {
        if (rank_counts[trips] >= 3) {
            int kickers[2];
            int found = 0;
            for (int r = 0; r < 13; r++) {
                if (r != trips && rank_counts[r] > 0) {
                    kickers[found++] = r;
                    if (found == 2) break;
                }
            }
            if (found == 2) {
                out_ranks[0] = trips;
                out_ranks[1] = kickers[0];
                out_ranks[2] = kickers[1];
                return 1;
            }
        }
    }
    return 0;
}

/* Select full house - returns 1 if successful */
static inline int opencl_select_full_house_ranks(const uint ss, const uint sc, const uint sd, const uint sh, int out_ranks[2]) {
    int rank_counts[13];
    for (int r = 0; r < 13; r++) {
        rank_counts[r] = 0;
        uint mask = (1U << r);
        if (ss & mask) rank_counts[r]++;
        if (sc & mask) rank_counts[r]++;
        if (sd & mask) rank_counts[r]++;
        if (sh & mask) rank_counts[r]++;
    }

    for (int trips = 0; trips < 13; trips++) {
        if (rank_counts[trips] >= 3) {
            for (int pair = 0; pair < 13; pair++) {
                if (pair != trips && rank_counts[pair] >= 2) {
                    out_ranks[0] = trips;
                    out_ranks[1] = pair;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Select quads + 3 kickers - returns 1 if successful */
static inline int opencl_select_quads_ranks(const uint ss, const uint sc, const uint sd, const uint sh, int out_ranks[4]) {
    int rank_counts[13];
    for (int r = 0; r < 13; r++) {
        rank_counts[r] = 0;
        uint mask = (1U << r);
        if (ss & mask) rank_counts[r]++;
        if (sc & mask) rank_counts[r]++;
        if (sd & mask) rank_counts[r]++;
        if (sh & mask) rank_counts[r]++;
    }

    for (int quads = 0; quads < 13; quads++) {
        if (rank_counts[quads] >= 4) {
            int kickers[3];
            int found = 0;
            for (int r = 0; r < 13; r++) {
                if (r != quads && rank_counts[r] > 0) {
                    kickers[found++] = r;
                    if (found == 3) break;
                }
            }
            if (found == 3) {
                out_ranks[0] = quads;
                out_ranks[1] = kickers[0];
                out_ranks[2] = kickers[1];
                out_ranks[3] = kickers[2];
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Evaluate A-5 lowball hand (Ace is low, straights/flushes don't count)
 * Lower HandVal = better low hand (inverted, so higher value = better low)
 * Returns LowHandVal_NOTHING if no qualifying low
 *
 * Based on CPU implementation in eval_low.h with full _findBestLowballHand logic
 */
static inline LowHandVal opencl_eval_low_a5(
    OpenCLCardMask cards,
    int n_cards,
    __constant uchar* nBitsTable
) {
    uint sc, sd, sh, ss;

    ss = cards.cards[0]; /* spades */
    sc = cards.cards[1]; /* clubs */
    sd = cards.cards[2]; /* diamonds */
    sh = cards.cards[3]; /* hearts */

    /* Rotate ranks to make Ace=0 (low), 2=1, ..., King=12 */
    ss = Lowball_ROTATE_RANKS(ss);
    sc = Lowball_ROTATE_RANKS(sc);
    sd = Lowball_ROTATE_RANKS(sd);
    sh = Lowball_ROTATE_RANKS(sh);

    /* For 7-card hands, use hierarchical selection logic.
       The selectors return ranks in ascending order, while LowHandVal keeps the
       highest card in TOP_CARD (see LowHandVal_WORST_EIGHT), so the cards are
       read back in reverse -- same layout as the CPU evaluator. */
    if (n_cards > 5) {
        int out_ranks[5];

        /* Try no pair (best case for lowball) */
        if (opencl_select_no_pair_ranks(ss, sc, sd, sh, out_ranks)) {
            return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)
                + LowHandVal_TOP_CARD_VALUE(out_ranks[4])
                + LowHandVal_SECOND_CARD_VALUE(out_ranks[3])
                + LowHandVal_THIRD_CARD_VALUE(out_ranks[2])
                + LowHandVal_FOURTH_CARD_VALUE(out_ranks[1])
                + LowHandVal_FIFTH_CARD_VALUE(out_ranks[0]);
        }

        /* Try one pair: the pair takes TOP_CARD, then kickers highest to lowest */
        int out_pair[4];
        if (opencl_select_one_pair_ranks(ss, sc, sd, sh, out_pair)) {
            return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR)
                + LowHandVal_TOP_CARD_VALUE(out_pair[0])
                + LowHandVal_SECOND_CARD_VALUE(out_pair[3])
                + LowHandVal_THIRD_CARD_VALUE(out_pair[2])
                + LowHandVal_FOURTH_CARD_VALUE(out_pair[1]);
        }

        /* Try two pair: high pair, low pair, then kicker */
        int out_two_pair[3];
        if (opencl_select_two_pair_ranks(ss, sc, sd, sh, out_two_pair)) {
            return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR)
                + LowHandVal_TOP_CARD_VALUE(out_two_pair[1])
                + LowHandVal_SECOND_CARD_VALUE(out_two_pair[0])
                + LowHandVal_THIRD_CARD_VALUE(out_two_pair[2]);
        }

        /* Try trips */
        int out_trips[3];
        if (opencl_select_trips_ranks(ss, sc, sd, sh, out_trips)) {
            return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS)
                + LowHandVal_TOP_CARD_VALUE(out_trips[0])
                + LowHandVal_SECOND_CARD_VALUE(out_trips[2])
                + LowHandVal_THIRD_CARD_VALUE(out_trips[1]);
        }

        /* Try full house */
        int out_full[2];
        if (opencl_select_full_house_ranks(ss, sc, sd, sh, out_full)) {
            return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE)
                + LowHandVal_TOP_CARD_VALUE(out_full[0])
                + LowHandVal_SECOND_CARD_VALUE(out_full[1]);
        }

        /* Try quads */
        int out_quads[4];
        if (opencl_select_quads_ranks(ss, sc, sd, sh, out_quads)) {
            return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS)
                + LowHandVal_TOP_CARD_VALUE(out_quads[0])
                + LowHandVal_SECOND_CARD_VALUE(out_quads[1])
                + LowHandVal_THIRD_CARD_VALUE(out_quads[2])
                + LowHandVal_FOURTH_CARD_VALUE(out_quads[3]);
        }
    } else if (n_cards == 5) {
        /* Exactly 5 cards – only no-pair hands produce a valid low */
        uint ranks = sc | ss | sd | sh;
        if (nBitsTable[ranks & 0x1FFF] < 5) {
            return LowHandVal_NOTHING;
        }
        uint bottom5 = opencl_bottom_n_cards(ranks, 5);
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) + bottom5;
    }

    return LowHandVal_NOTHING;
}

/*
 * Evaluate 2-7 lowball hand (Ace is high, straights/flushes count against you)
 * Lower HandVal = better low hand
 *
 * In 2-7 lowball: straights and flushes COUNT AGAINST you (except wheel A-2-3-4-5)
 * Best hand: 7-5-4-3-2 unsuited
 */

static inline int opencl_rotate_rank_for_low(int rank) {
    return (rank == StdDeck_Rank_ACE) ? 0 : (rank + 1);
}

/*
 * Check if a low hand qualifies under the configured rule.
 * Returns 1 if qualifies, 0 otherwise.
 */
static inline int opencl_low_qualifies(LowHandVal lo, int qualifier) {
    if (lo == LowHandVal_NOTHING) {
        return 0;
    }

    if (qualifier <= 0) {
        return 1;
    }

    /* A paired hand can never satisfy an "8 or better" qualifier: it needs five
       distinct ranks.  Without this test a pair of deuces passes, its TOP_CARD
       holding the rank of the pair. */
    if (LowHandVal_HANDTYPE(lo) != StdRules_HandType_NOPAIR) {
        return 0;
    }

    /* Convert qualifier (e.g. 8) to StdDeck rank index (2 -> 0, ..., Ace -> 12) */
    int std_rank = qualifier - 2;
    if (std_rank < 0) std_rank = 0;
    if (std_rank > StdDeck_Rank_ACE) std_rank = StdDeck_Rank_ACE;

    int limit = opencl_rotate_rank_for_low(std_rank);
    uint worst_rank = (lo >> LowHandVal_TOP_CARD_SHIFT) & LowHandVal_CARD_MASK;
    return (worst_rank <= (uint)limit) ? 1 : 0;
}

/* 2-7 Lowball evaluation (ported from StdDeck_Lowball27_EVAL_N) */
static inline HandVal opencl_eval_low_27(
    OpenCLCardMask cards,
    int n_cards,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
) {
    HandVal retval;
    uint ranks, four_mask, three_mask, two_mask;
    uint n_dups, n_ranks;
    uint suits[4];

    suits[0] = cards.cards[0];
    suits[1] = cards.cards[1];
    suits[2] = cards.cards[2];
    suits[3] = cards.cards[3];

    retval = 0;
    ranks = suits[0] | suits[1] | suits[2] | suits[3];
    n_ranks = nBitsTable[ranks];
    n_dups = n_cards - n_ranks;

    if (n_ranks >= 5) {
        uint suit;
        for (suit = 0; suit < 4; suit++) {
            if (nBitsTable[suits[suit]] >= 5) {
                if (straightTable[suits[suit]]) {
                    retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                           + HandVal_TOP_CARD_VALUE(straightTable[suits[suit]]);
                    return retval;
                } else {
                    retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                           + topFiveCardsTable[suits[suit]];
                    return retval;
                }
            }
        }

        if (retval == 0) {
            int st = straightTable[ranks];
            if (st && st != StdDeck_Rank_5) {
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_STRAIGHT)
                       + HandVal_TOP_CARD_VALUE(st);
            }
        }

        if (retval && n_dups < 3)
            return retval;
    }

    switch (n_dups) {
    case 0:
        return HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)
             + topFiveCardsTable[ranks];

    case 1: {
        uint t, kickers;

        two_mask = ranks ^ (suits[0] ^ suits[1] ^ suits[2] ^ suits[3]);

        retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR)
               + HandVal_TOP_CARD_VALUE(topCardTable[two_mask]);
        t = ranks ^ two_mask;
        kickers = (topFiveCardsTable[t] >> HandVal_CARD_WIDTH)
                & ~HandVal_FIFTH_CARD_MASK;
        retval += kickers;

        return retval;
    }

    case 2:
        two_mask = ranks ^ (suits[0] ^ suits[1] ^ suits[2] ^ suits[3]);

        if (two_mask) {
            uint t;

            t = ranks ^ two_mask;
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR)
                   + (topFiveCardsTable[two_mask]
                      & (HandVal_TOP_CARD_MASK | HandVal_SECOND_CARD_MASK))
                   + HandVal_THIRD_CARD_VALUE(topCardTable[t]);

            return retval;
        } else {
            int t, second;

            three_mask = ((suits[0] & suits[1]) | (suits[2] & suits[3]))
                       & ((suits[0] & suits[2]) | (suits[1] & suits[3]));

            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS)
                   + HandVal_TOP_CARD_VALUE(topCardTable[three_mask]);

            t = ranks ^ three_mask;
            second = topCardTable[t];
            retval += HandVal_SECOND_CARD_VALUE(second);
            t ^= (1 << second);
            retval += HandVal_THIRD_CARD_VALUE(topCardTable[t]);

            return retval;
        }

    default:
        four_mask = suits[0] & suits[1] & suits[2] & suits[3];

        if (four_mask) {
            int tc;

            tc = topCardTable[four_mask];
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS)
                   + HandVal_TOP_CARD_VALUE(tc)
                   + HandVal_SECOND_CARD_VALUE(topCardTable[ranks ^ (1 << tc)]);
            return retval;
        }

        two_mask = ranks ^ (suits[0] ^ suits[1] ^ suits[2] ^ suits[3]);

        if (nBitsTable[two_mask] != n_dups) {
            int tc, t;

            three_mask = ((suits[0] & suits[1]) | (suits[2] & suits[3]))
                       & ((suits[0] & suits[2]) | (suits[1] & suits[3]));

            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE);
            tc = topCardTable[three_mask];
            retval += HandVal_TOP_CARD_VALUE(tc);
            t = (two_mask | three_mask) ^ (1 << tc);
            retval += HandVal_SECOND_CARD_VALUE(topCardTable[t]);

            return retval;
        }

        if (retval) {
            return retval;
        } else {
            int top, second;

            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR);
            top = topCardTable[two_mask];
            retval += HandVal_TOP_CARD_VALUE(top);
            second = topCardTable[two_mask ^ (1 << top)];
            retval += HandVal_SECOND_CARD_VALUE(second);
            retval += HandVal_THIRD_CARD_VALUE(topCardTable[ranks ^ (1 << top)
                                                           ^ (1 << second)]);
            return retval;
        }
    }

    return 0;
}
