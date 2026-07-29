#ifndef PLO_NOMENCLATURE_H
#define PLO_NOMENCLATURE_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PLO Hand Categories */
typedef enum {
    PLO_CAT_UNPAIRED_DS = 1,      /* Double-suited unpaired */
    PLO_CAT_UNPAIRED_SS = 2,      /* Single-suited unpaired */
    PLO_CAT_UNPAIRED_RB = 3,      /* Rainbow unpaired */
    PLO_CAT_PAIR_DS = 4,          /* One pair double-suited */
    PLO_CAT_PAIR_SS = 5,          /* One pair single-suited */
    PLO_CAT_PAIR_RB = 6,          /* One pair rainbow */
    PLO_CAT_TWO_PAIR_DS = 7,      /* Two pair double-suited */
    PLO_CAT_TWO_PAIR_SS = 8,      /* Two pair single-suited */
    PLO_CAT_TWO_PAIR_RB = 9,      /* Two pair rainbow */
    PLO_CAT_TRIPS_DS = 10,        /* Trips double-suited */
    PLO_CAT_TRIPS_SS = 11,        /* Trips single-suited */
    PLO_CAT_TRIPS_RB = 12,        /* Trips rainbow */
    PLO_CAT_AA_DS = 13,           /* Aces double-suited */
    PLO_CAT_AA_SS = 14,           /* Aces single-suited */
    PLO_CAT_AA_RB = 15,           /* Aces rainbow */
    PLO_CAT_BROADWAY_DS = 16,     /* 3+ Broadway double-suited */
    PLO_CAT_BROADWAY_SS = 17,     /* 3+ Broadway single-suited */
    PLO_CAT_BROADWAY_RB = 18,     /* 3+ Broadway rainbow */
    PLO_CAT_RAGGED_DS = 19,       /* Ragged/Low double-suited */
    PLO_CAT_RAGGED_SS = 20,       /* Ragged/Low single-suited */
    PLO_CAT_RAGGED_RB = 21,       /* Ragged/Low rainbow */
    PLO_CAT_INVALID = 0
} PLOHandCategory;

/* Suitedness types */
typedef enum {
    PLO_SUIT_RAINBOW = 0,         /* Four different suits */
    PLO_SUIT_SINGLE = 1,          /* One suit has 2+ cards */
    PLO_SUIT_DOUBLE = 2,          /* Two suits have 2 cards each */
    PLO_SUIT_TRIPLE = 3,          /* One suit has 3 cards */
    PLO_SUIT_QUAD = 4             /* All four cards same suit (monotone) */
} PLOSuitedness;

/* Connectivity types */
typedef enum {
    PLO_CONN_NONE = 0,            /* No connectivity */
    PLO_CONN_RUNDOWN = 1,         /* Four consecutive cards (e.g., JT98) */
    PLO_CONN_1GAP = 2,            /* One gap in sequence (e.g., JT86) */
    PLO_CONN_2GAP = 3,            /* Two gaps in sequence */
    PLO_CONN_PARTIAL = 4          /* Some connectivity but not rundown */
} PLOConnectivity;

/* PLO Hand Structure */
typedef struct {
    StdDeck_CardMask cards;       /* The four cards */
    PLOHandCategory category;     /* Hand category (1-21) */
    PLOSuitedness suitedness;     /* Suitedness type */
    PLOConnectivity connectivity; /* Connectivity type */
    int has_ace;                  /* Contains at least one ace */
    int broadway_count;           /* Number of broadway cards (T-A) */
    int pair_count;               /* Number of pairs (0-2) */
    int trips;                    /* Has trips (1) or not (0) */
    char notation[32];            /* String notation (e.g., "AAKKds") */
} PLOHand;

/* Function prototypes */

/**
 * Parse a PLO hand string into a PLOHand structure
 * @param hand_str Hand string (e.g., "AsKdQhJc" or "AAKKds")
 * @param hand Pointer to PLOHand structure to fill
 * @return 1 on success, 0 on failure
 */
int PLO_ParseHand(const char* hand_str, PLOHand* hand);

/**
 * Categorize a PLO hand based on its cards
 * @param cards The four cards as a CardMask
 * @param hand Pointer to PLOHand structure to fill
 * @return PLOHandCategory
 */
PLOHandCategory PLO_CategorizeHand(StdDeck_CardMask cards, PLOHand* hand);

/**
 * Get suitedness of a PLO hand
 * @param cards The four cards as a CardMask
 * @return PLOSuitedness
 */
PLOSuitedness PLO_GetSuitedness(StdDeck_CardMask cards);

/**
 * Get connectivity of a PLO hand
 * @param cards The four cards as a CardMask
 * @return PLOConnectivity
 */
PLOConnectivity PLO_GetConnectivity(StdDeck_CardMask cards);

/**
 * Convert PLO hand to standard notation string
 * @param hand Pointer to PLOHand structure
 * @param buffer Buffer to write notation to
 * @param bufsize Size of buffer
 * @return Pointer to buffer on success, NULL on failure
 */
char* PLO_HandToString(const PLOHand* hand, char* buffer, size_t bufsize);

/**
 * Get category name as string
 * @param category PLOHandCategory
 * @return Category name string
 */
const char* PLO_CategoryName(PLOHandCategory category);

/**
 * Get suitedness suffix
 * @param suitedness PLOSuitedness
 * @return Suffix string ("ds", "ss", "r", etc.)
 */
const char* PLO_SuitednessSuffix(PLOSuitedness suitedness);

/**
 * Check if hand matches a pattern with wildcards
 * @param hand PLOHand to check
 * @param pattern Pattern string (e.g., "AAxx", "KK**ds")
 * @return 1 if matches, 0 otherwise
 */
int PLO_MatchesPattern(const PLOHand* hand, const char* pattern);

/**
 * Get approximate percentage of deck for a category
 * @param category PLOHandCategory
 * @return Percentage as float
 */
float PLO_CategoryPercentage(PLOHandCategory category);

#ifdef __cplusplus
}
#endif

#endif /* PLO_NOMENCLATURE_H */
