/*
 * debug_kernel.cl: Debug version of the evaluation kernel
 */

/* HandVal type definition */
typedef uint HandVal;

/* Card mask structure */
typedef struct {
    uint cards_n;
    uint4 cards; /* x=spades, y=clubs, z=diamonds, w=hearts */
} CardMask;

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

/* Debug: encode debug info in the result */
#define DEBUG_ENCODE(case_num, n_dups, data) \
    (((case_num) << 28) | ((n_dups) << 24) | ((data) & 0xffffff))

/* Debug: encode ranks and n_ranks info */
#define DEBUG_ENCODE_RANKS(ranks, n_ranks) \
    (((ranks) << 12) | (n_ranks))

/* Evaluate a poker hand with debug info */
HandVal debug_eval_5cards(
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

    ss = cards.cards.x; /* spades */
    sc = cards.cards.y; /* clubs */
    sd = cards.cards.z; /* diamonds */
    sh = cards.cards.w; /* hearts */

    retval = 0;
    ranks = sc | sd | sh | ss;
    n_ranks = nBitsTable[ranks];
    n_dups = n_cards - n_ranks;

    /* Case 13: Return debug info about all individual suits */
    /* Return ss, sc, sd, sh to verify the GPU is getting the right values */
    
    /* Option 1: Return ss and sc with full precision */
    /* return DEBUG_ENCODE(13, 0, ((ss & 0xfff) << 12) | (sc & 0xfff)); */
    
    /* Option 2: Return sd and sh */
    return DEBUG_ENCODE(14, 0, ((sd & 0xfff) << 12) | (sh & 0xfff));
    
    /* Option 3: Return all suits in one go (limited precision) */
    /* Each suit gets 6 bits, total 24 bits */
    /* return DEBUG_ENCODE(13, 0, ((ss & 0x3f) << 18) | ((sc & 0x3f) << 12) | ((sd & 0x3f) << 6) | (sh & 0x3f)); */
    
    /* Check for straight, flush, or straight flush */
    if (n_ranks >= 5) {
        if (nBitsTable[ss] >= 5) {
            if (straightTable[ss]) 
                return DEBUG_ENCODE(99, n_dups, ss); /* Spades straight flush */
            else
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) 
                    + topFiveCardsTable[ss];
        } 
        else if (nBitsTable[sc] >= 5) {
            if (straightTable[sc]) 
                return DEBUG_ENCODE(98, n_dups, sc); /* Clubs straight flush */
            else 
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) 
                    + topFiveCardsTable[sc];
        } 
        else if (nBitsTable[sd] >= 5) {
            if (straightTable[sd]) 
                return DEBUG_ENCODE(97, n_dups, sd); /* Diamonds straight flush */
            else 
                retval = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH) 
                    + topFiveCardsTable[sd];
        } 
        else if (nBitsTable[sh] >= 5) {
            if (straightTable[sh]) 
                return DEBUG_ENCODE(96, n_dups, sh); /* Hearts straight flush */
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
            return DEBUG_ENCODE(95, n_dups, retval & 0xffffff); /* Early return */
    }

    /* Handle different numbers of duplicates */
    switch (n_dups) {
    case 0:
        return DEBUG_ENCODE(0, n_dups, ranks); /* Case 0 */
        
    case 1: {
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        return DEBUG_ENCODE(1, n_dups, two_mask); /* Case 1 */
    }
        
    case 2: {
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (two_mask) { 
            return DEBUG_ENCODE(2, n_dups, two_mask); /* Case 2a - two pair */
        }
        else {
            three_mask = ((sc&sd)|(sh&ss)) & ((sc&sh)|(sd&ss));
            return DEBUG_ENCODE(22, n_dups, three_mask); /* Case 2b - trips */
        }
    }
        
    default:
        two_mask = ranks ^ (sc ^ sd ^ sh ^ ss);
        if (nBitsTable[two_mask] != n_dups) {
            return DEBUG_ENCODE(3, n_dups, two_mask); /* Default - full house */
        } else {
            return DEBUG_ENCODE(33, n_dups, two_mask); /* Default - other */
        }
    }
}

/* Debug kernel for batch evaluation */
__kernel void debug_eval_batch_boards_kernel(
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
            
            combined.cards.x = board.cards.x | holes.cards.x; /* spades */
            combined.cards.y = board.cards.y | holes.cards.y; /* clubs */
            combined.cards.z = board.cards.z | holes.cards.z; /* diamonds */
            combined.cards.w = board.cards.w | holes.cards.w; /* hearts */
            
            /* Evaluate the 7-card hand with debug info */
            HandVal val = debug_eval_5cards(combined, 7, nBitsTable, straightTable, 
                                           topFiveCardsTable, topCardTable);
            results[board_idx * n_players + player] = val;
        }
    }
}