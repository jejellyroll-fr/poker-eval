/*
 * eval_generic_kernel.cl: Generic OpenCL kernels for multi-game poker evaluation
 *
 * This file implements generalized GPU kernels that can evaluate multiple
 * poker variants (Hold'em, Omaha, Stud, Razz, hi/lo games, etc.)
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
#endif

/* Game configuration (must match host definition) */
typedef enum {
    GPU_GAME_HOLDEM = 0,
    GPU_GAME_OMAHA = 1,
    GPU_GAME_OMAHA5 = 2,
    GPU_GAME_OMAHA6 = 3,
    GPU_GAME_STUD = 4,
    GPU_GAME_RAZZ = 5,
    GPU_GAME_HOLDEM8 = 6,
    GPU_GAME_OMAHA8 = 7,
    GPU_GAME_STUD8 = 8,
    GPU_GAME_LOWBALL27 = 9
} gpu_game_type_t;

typedef struct {
    gpu_game_type_t game;
    int num_hole_cards;
    int num_board_cards;
    int eval_low;
    int split_pot;
    int low_qualifier;
} gpu_game_config_t;

/* Include function declarations from other kernel files */

/* From eval_kernel.cl (existing) */
static inline HandVal opencl_eval_5cards(
    OpenCLCardMask cards,
    int n_cards,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
);

/* From eval_low_kernel.cl */
static inline LowHandVal opencl_eval_low_a5(
    OpenCLCardMask cards,
    int n_cards,
    __constant uchar* nBitsTable
);

static inline LowHandVal opencl_eval_low8_5card(
    OpenCLCardMask cards,
    __constant uchar* nBitsTable
);

static inline HandVal opencl_eval_low_27(
    OpenCLCardMask cards,
    int n_cards,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
);

static inline int opencl_low_qualifies(LowHandVal lo, int qualifier);

/* From eval_omaha_kernel.cl */
static inline HandVal opencl_eval_omaha_best_hi(
    OpenCLCardMask hole,
    OpenCLCardMask board,
    int num_hole,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
);

static inline LowHandVal opencl_eval_omaha_best_lo(
    OpenCLCardMask hole,
    OpenCLCardMask board,
    int num_hole,
    __constant uchar* nBitsTable
);

/*
 * Combine two card masks
 */
inline OpenCLCardMask opencl_combine_masks(OpenCLCardMask a, OpenCLCardMask b) {
    OpenCLCardMask result;
    result.cards_n = a.cards_n + b.cards_n;
    result.cards[0] = a.cards[0] | b.cards[0];
    result.cards[1] = a.cards[1] | b.cards[1];
    result.cards[2] = a.cards[2] | b.cards[2];
    result.cards[3] = a.cards[3] | b.cards[3];
    return result;
}

/*
 * Count number of cards in a mask
 */
inline int opencl_count_cards(OpenCLCardMask mask) {
    int count = 0;
    count += popcount(mask.cards[0]); // spades
    count += popcount(mask.cards[1]); // hearts
    count += popcount(mask.cards[2]); // diamonds
    count += popcount(mask.cards[3]); // clubs
    return count;
}

/*
 * Generic evaluation kernel for all game types
 * This kernel evaluates hands based on game configuration
 */
__kernel void eval_generic_boards_kernel(
    __global OpenCLCardMask* boards,
    __global OpenCLCardMask* hole_cards,
    int n_boards,
    int n_players,
    __constant gpu_game_config_t* config,
    __global HandVal* results_hi,
    __global HandVal* results_lo,
    __global int* lo_qualifies,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
) {
    int board_idx = get_global_id(0);

    if (board_idx < n_boards) {
        for (int player = 0; player < n_players; player++) {
            int result_idx = board_idx * n_players + player;
            int hole_idx = board_idx * n_players + player;

            OpenCLCardMask combined;
            combined.cards_n = config->num_hole_cards + config->num_board_cards;

            /* Combine hole and board cards based on game type */
            if (config->num_board_cards > 0) {
                /* Board games: Hold'em, Omaha */
                OpenCLCardMask board = boards[board_idx];
                OpenCLCardMask hole = hole_cards[hole_idx];

                /* Check if we need to do Omaha-style evaluation */
                if (config->game == GPU_GAME_OMAHA ||
                    config->game == GPU_GAME_OMAHA5 ||
                    config->game == GPU_GAME_OMAHA6 ||
                    config->game == GPU_GAME_OMAHA8) {

                    /* Omaha: exactly 2 hole + 3 board */
                    results_hi[result_idx] = opencl_eval_omaha_best_hi(
                        hole, board, config->num_hole_cards,
                        nBitsTable, straightTable,
                        topFiveCardsTable, topCardTable);

                    if (config->eval_low) {
                        LowHandVal lo = opencl_eval_omaha_best_lo(
                            hole, board, config->num_hole_cards,
                            nBitsTable);
                        results_lo[result_idx] = lo;
                        lo_qualifies[result_idx] = (lo != LowHandVal_NOTHING);
                    }
                } else {
                    /* Hold'em: use all cards */
                    combined = opencl_combine_masks(board, hole);
                    results_hi[result_idx] = opencl_eval_5cards(
                        combined, 7,
                        nBitsTable, straightTable,
                        topFiveCardsTable, topCardTable);

                    if (config->eval_low) {
                        LowHandVal lo = opencl_eval_low_a5(combined, 7, nBitsTable);
                        results_lo[result_idx] = lo;
                        lo_qualifies[result_idx] = opencl_low_qualifies(lo, config->low_qualifier);
                    }
                }
            } else {
                /* No board games: Stud, Razz, 2-7 Lowball */
                combined = hole_cards[hole_idx];
                combined.cards_n = config->num_hole_cards;  /* Fix: restore correct card count */

            if (config->game == GPU_GAME_LOWBALL27) {
                    HandVal lo = opencl_eval_low_27(
                        combined, config->num_hole_cards,
                        nBitsTable, straightTable,
                        topFiveCardsTable, topCardTable);
                    results_hi[result_idx] = 0;
                    results_lo[result_idx] = lo;
                    lo_qualifies[result_idx] = 1;
            } else if (config->eval_low && !config->split_pot) {
                    /* Razz: A-5 low only */
                    LowHandVal lo = opencl_eval_low_a5(combined, config->num_hole_cards, nBitsTable);
                    results_lo[result_idx] = lo;
                    results_hi[result_idx] = 0;  /* Not used */
                    lo_qualifies[result_idx] = 1; /* Always qualifies in Razz */
                } else {
                    /* Regular Stud */
                    results_hi[result_idx] = opencl_eval_5cards(
                        combined, config->num_hole_cards,
                        nBitsTable, straightTable,
                        topFiveCardsTable, topCardTable);

                    if (config->eval_low) {
                        /* Stud Hi/Lo */
                        LowHandVal lo = opencl_eval_low_a5(combined, config->num_hole_cards, nBitsTable);
                        results_lo[result_idx] = lo;
                        lo_qualifies[result_idx] = opencl_low_qualifies(lo, config->low_qualifier);
                    }
                }
            }
        }
    }
}
