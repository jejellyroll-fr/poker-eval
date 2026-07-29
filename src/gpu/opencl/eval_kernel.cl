/*
 * eval_kernel.cl: OpenCL kernels for GPU-accelerated poker hand evaluation
 */

/* Common type definitions - use guards to prevent redefinition */
#ifndef OPENCL_POKER_TYPES_DEFINED
#define OPENCL_POKER_TYPES_DEFINED

/* HandVal type definition */
typedef uint HandVal;
typedef uint LowHandVal;

/* Card mask structure for multi-game kernels */
typedef struct {
    uint cards_n;
    uint cards[4]; /* spades, clubs, diamonds, hearts - matches gpu_card_mask_t layout */
} OpenCLCardMask;

#endif /* OPENCL_POKER_TYPES_DEFINED */

/* Legacy card mask structure for backwards compatibility */
typedef struct {
    uint cards_n;
    uint spades;
    uint clubs;
    uint diamonds;
    uint hearts;
} CardMask;

#ifndef OPENCL_HANDVAL_MACROS_DEFINED
#define OPENCL_HANDVAL_MACROS_DEFINED

/* Hand types enum */
#define StdRules_HandType_NOPAIR    0
#define StdRules_HandType_ONEPAIR   1
#define StdRules_HandType_TWOPAIR   2
#define StdRules_HandType_TRIPS     3
#define StdRules_HandType_STRAIGHT  4
#define StdRules_HandType_FLUSH     5
#define StdRules_HandType_FULLHOUSE 6
#define StdRules_HandType_QUADS     7
#define StdRules_HandType_STFLUSH   8

/* Rank definitions for special cases */
#define StdDeck_Rank_5              3  /* Rank 5 (for wheel detection in 2-7 lowball) */

/* HandVal manipulation macros */
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

#define HandVal_HANDTYPE_VALUE(ht)    ((ht) << HandVal_HANDTYPE_SHIFT)
#define HandVal_TOP_CARD_VALUE(c)     ((c) << HandVal_TOP_CARD_SHIFT)
#define HandVal_SECOND_CARD_VALUE(c)  ((c) << HandVal_SECOND_CARD_SHIFT)
#define HandVal_THIRD_CARD_VALUE(c)   ((c) << HandVal_THIRD_CARD_SHIFT)
#define HandVal_FOURTH_CARD_VALUE(c)  ((c) << HandVal_FOURTH_CARD_SHIFT)
#define HandVal_FIFTH_CARD_VALUE(c)   ((c) << HandVal_FIFTH_CARD_SHIFT)

#endif /* OPENCL_HANDVAL_MACROS_DEFINED */

/* Evaluate a poker hand - 5-card evaluation (can handle 5-7 cards) */
HandVal eval_5cards(
    CardMask cards,
    int n_cards,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
) {
    HandVal retval;
    uint ranks, four_mask, three_mask, two_mask;
    uint n_dups, n_ranks;
    uint ss, sh, sd, sc;

    ss = cards.spades;
    sc = cards.clubs;
    sd = cards.diamonds;
    sh = cards.hearts;

    retval = 0;
    ranks = sc | sd | sh | ss;
    n_ranks = nBitsTable[ranks];
    n_dups = n_cards - n_ranks;

    /* Check for straight, flush, or straight flush */
    if (n_ranks >= 5) {
        if (nBitsTable[ss] >= 5) {
            if (straightTable[ss])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(straightTable[ss]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + topFiveCardsTable[ss];
        }
        else if (nBitsTable[sc] >= 5) {
            if (straightTable[sc])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(straightTable[sc]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + topFiveCardsTable[sc];
        }
        else if (nBitsTable[sd] >= 5) {
            if (straightTable[sd])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(straightTable[sd]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + topFiveCardsTable[sd];
        }
        else if (nBitsTable[sh] >= 5) {
            if (straightTable[sh])
                return HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)
                    + HandVal_TOP_CARD_VALUE(straightTable[sh]);
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)
                    + topFiveCardsTable[sh];
        }
        else {
            int st = straightTable[ranks];
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
            + topFiveCardsTable[ranks];
        
    case 1: {
        /* One pair */
        uint t, kickers;
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR)
            + HandVal_TOP_CARD_VALUE(topCardTable[two_mask]);
        t = ranks ^ two_mask;
        kickers = (topFiveCardsTable[t] >> HandVal_CARD_WIDTH)
            & ~HandVal_FIFTH_CARD_MASK;
        retval += kickers;
        return retval;
    }
        
    case 2: 
        /* Two pair or trips */
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (two_mask) { 
            uint t;
            t = ranks ^ two_mask;
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR)
                + (topFiveCardsTable[two_mask]
                   & (HandVal_TOP_CARD_MASK | HandVal_SECOND_CARD_MASK))
                + HandVal_THIRD_CARD_VALUE(topCardTable[t]);
            return retval;
        }
        else {
            int t, second;
            three_mask = ((sc&sd)|(sh&ss)) & ((sc&sh)|(sd&ss));
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
        /* Quads, full house, or two pair */
        four_mask = sh & sd & sc & ss;
        if (four_mask) {
            int tc;
            tc = topCardTable[four_mask];
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS)
                + HandVal_TOP_CARD_VALUE(tc)
                + HandVal_SECOND_CARD_VALUE(topCardTable[ranks ^ (1 << tc)]);
            return retval;
        }

        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (nBitsTable[two_mask] != n_dups) {
            /* Full house */
            int tc, t;
            three_mask = ((sc&sd)|(sh&ss)) & ((sc&sh)|(sd&ss));
            retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE);
            tc = topCardTable[three_mask];
            retval += HandVal_TOP_CARD_VALUE(tc);
            t = (two_mask | three_mask) ^ (1 << tc);
            retval += HandVal_SECOND_CARD_VALUE(topCardTable[t]);
            return retval;
        }

        if (retval) /* flush and straight */
            return retval;
        else {
            /* Two pair */
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
}

/* Wrapper function for OpenCLCardMask - converts to CardMask and calls eval_5cards */
static inline HandVal opencl_eval_5cards(
    OpenCLCardMask ocl_cards,
    int n_cards,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
) {
    /* Convert OpenCLCardMask to CardMask
     * Memory layout: cards[0]=spades, cards[1]=clubs, cards[2]=diamonds, cards[3]=hearts
     */
    CardMask cards;
    cards.cards_n = ocl_cards.cards_n;
    cards.spades = ocl_cards.cards[0];
    cards.clubs = ocl_cards.cards[1];
    cards.diamonds = ocl_cards.cards[2];
    cards.hearts = ocl_cards.cards[3];

    return eval_5cards(cards, n_cards, nBitsTable, straightTable,
                      topFiveCardsTable, topCardTable);
}

/* Kernel for batch evaluation of multiple boards */
__kernel void eval_batch_boards_kernel(
    __global CardMask* boards,
    __global CardMask* hole_cards,
    int n_boards,
    int n_players,
    __global HandVal* results,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
) {
    int board_idx = get_global_id(0);
    
    if (board_idx < n_boards) {
        CardMask board = boards[board_idx];
        
        /* Evaluate each player's hand for this board */
        for (int player = 0; player < n_players; player++) {
            CardMask combined;
            combined.cards_n = 7; /* 5 board + 2 hole cards */
            
            /* Combine board and hole cards */
            int hole_idx = board_idx * n_players + player;
            CardMask holes = hole_cards[hole_idx];
            
            combined.spades = board.spades | holes.spades;
            combined.clubs = board.clubs | holes.clubs;
            combined.diamonds = board.diamonds | holes.diamonds;
            combined.hearts = board.hearts | holes.hearts;
            
            /* Evaluate the 7-card hand */
            HandVal val = eval_5cards(combined, 7, nBitsTable, straightTable, 
                                     topFiveCardsTable, topCardTable);
            results[board_idx * n_players + player] = val;
        }
    }
}

/* Simple XORShift RNG for Monte Carlo */
uint xorshift32(uint state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* Kernel for Monte Carlo equity calculation */
__kernel void monte_carlo_equity_kernel(
    __global CardMask* ranges,
    int n_players,
    int n_simulations,
    __global int* wins,
    __global int* ties,
    __global uint* rng_states,
    __constant uchar* nBitsTable,
    __constant uchar* straightTable,
    __constant uint* topFiveCardsTable,
    __constant uchar* topCardTable
) {
    int sim_idx = get_global_id(0);
    
    if (sim_idx < n_simulations) {
        /* Initialize thread-local RNG */
        uint rng_state = rng_states[sim_idx];
        
        /* TODO: Implement full Monte Carlo simulation */
        /* This is a placeholder for the actual implementation */
        
        /* Update RNG state for next iteration */
        rng_states[sim_idx] = xorshift32(rng_state);
    }
}
