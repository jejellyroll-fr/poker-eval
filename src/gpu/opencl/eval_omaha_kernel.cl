/*
 * eval_omaha_kernel.cl: OpenCL kernels for Omaha hand evaluation
 *
 * Implements Omaha evaluation: exactly 2 from hole + 3 from board
 * Supports Omaha (4 hole), Omaha-5 (5 hole), and Omaha-6 (6 hole)
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

#ifndef OPENCL_LOW_HANDVAL_CONSTS_DEFINED
#define OPENCL_LOW_HANDVAL_CONSTS_DEFINED
#define LowHandVal_NOTHING \
    (HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH) + \
     HandVal_TOP_CARD_VALUE(StdDeck_Rank_ACE) + 1)
#define LowHandVal_WORST_EIGHT \
    (HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)     \
     + HandVal_TOP_CARD_VALUE(StdDeck_Rank_8 + 1)         \
     + HandVal_SECOND_CARD_VALUE(StdDeck_Rank_7 + 1)      \
     + HandVal_THIRD_CARD_VALUE(StdDeck_Rank_6 + 1)       \
     + HandVal_FOURTH_CARD_VALUE(StdDeck_Rank_5 + 1)      \
     + HandVal_FIFTH_CARD_VALUE(StdDeck_Rank_4 + 1))
#endif

/* Forward declarations - these are implemented in eval_kernel.cl */
static inline HandVal opencl_eval_5cards(
    OpenCLCardMask cards,
    int n_cards,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
);

static inline LowHandVal opencl_eval_low_a5(
    OpenCLCardMask cards,
    int n_cards,
    __constant uchar* nBitsTable
);

static inline LowHandVal opencl_eval_low8_5card(
    OpenCLCardMask cards,
    __constant uchar* nBitsTable
);

static inline int opencl_low_qualifies(LowHandVal lo, int qualifier);

/*
 * Extract nth card from a mask
 * Returns suit (0-3) and rank (0-12) via pointers
 */
static inline int opencl_get_nth_card(OpenCLCardMask mask, int n, int* suit, int* rank) {
    int count = 0;

    for (int s = 0; s < 4; s++) {
        uint suit_mask = mask.cards[s];
        for (int r = 0; r < 13; r++) {
            if (suit_mask & (1U << r)) {
                if (count == n) {
                    *suit = s;
                    *rank = r;
                    return 1;
                }
                count++;
            }
        }
    }

    return 0; /* Card not found */
}

/*
 * Create a card mask with exactly 5 cards:
 * 2 from hole (at indices h1, h2) and 3 from board (at indices b1, b2, b3)
 */
static inline OpenCLCardMask opencl_make_omaha_hand(
    OpenCLCardMask hole,
    OpenCLCardMask board,
    int h1, int h2,
    int b1, int b2, int b3
) {
    OpenCLCardMask result;
    result.cards_n = 5;
    result.cards[0] = result.cards[1] = result.cards[2] = result.cards[3] = 0;

    int suit, rank;

    /* Add 2 hole cards */
    if (opencl_get_nth_card(hole, h1, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }
    if (opencl_get_nth_card(hole, h2, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }

    /* Add 3 board cards */
    if (opencl_get_nth_card(board, b1, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }
    if (opencl_get_nth_card(board, b2, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }
    if (opencl_get_nth_card(board, b3, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }

    return result;
}

/*
 * Evaluate best high hand for Omaha
 * Tries all C(num_hole, 2) × C(5, 3) = 60 combinations for Omaha-4
 */
static inline HandVal opencl_eval_omaha_best_hi(
    OpenCLCardMask hole,
    OpenCLCardMask board,
    int num_hole,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
) {
    HandVal best = 0;

    /* Try all combinations of 2 hole cards */
    for (int h1 = 0; h1 < num_hole; h1++) {
        for (int h2 = h1 + 1; h2 < num_hole; h2++) {

            /* Try all combinations of 3 board cards */
            for (int b1 = 0; b1 < 5; b1++) {
                for (int b2 = b1 + 1; b2 < 5; b2++) {
                    for (int b3 = b2 + 1; b3 < 5; b3++) {

                        /* Make 5-card hand */
                        OpenCLCardMask hand = opencl_make_omaha_hand(
                            hole, board, h1, h2, b1, b2, b3);

                        /* Evaluate it */
                        HandVal val = opencl_eval_5cards(
                            hand, 5,
                            nBitsTable, straightTable,
                            topFiveCardsTable, topCardTable);

                        /* Track best */
                        if (val > best) {
                            best = val;
                        }
                    }
                }
            }
        }
    }

    return best;
}

/*
 * Evaluate best low hand for Omaha Hi/Lo
 * Tries all C(num_hole, 2) × C(5, 3) combinations
 * Returns best (lowest) qualifying low hand
 */
static inline LowHandVal opencl_eval_omaha_best_lo(
    OpenCLCardMask hole,
    OpenCLCardMask board,
    int num_hole,
    __constant uchar* nBitsTable
) {
    LowHandVal best = LowHandVal_NOTHING;
    int found = 0;

    /* Try all combinations of 2 hole cards */
    for (int h1 = 0; h1 < num_hole; h1++) {
        for (int h2 = h1 + 1; h2 < num_hole; h2++) {

            /* Try all combinations of 3 board cards */
            for (int b1 = 0; b1 < 5; b1++) {
                for (int b2 = b1 + 1; b2 < 5; b2++) {
                    for (int b3 = b2 + 1; b3 < 5; b3++) {

                        /* Make 5-card hand */
                        OpenCLCardMask hand = opencl_make_omaha_hand(
                            hole, board, h1, h2, b1, b2, b3);

                        /* Evaluate low */
                        LowHandVal val = opencl_eval_low8_5card(hand, nBitsTable);
                        if (val == LowHandVal_NOTHING)
                            continue;
                        if (!found || val < best) {
                            best = val;
                            found = 1;
                        }
                    }
                }
            }
        }
    }

    return found ? best : LowHandVal_NOTHING;
}
