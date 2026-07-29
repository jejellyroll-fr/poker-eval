#ifndef OMAHA_HAND_DISTRIBUTION_H
#define OMAHA_HAND_DISTRIBUTION_H

#include <poker_eval/deck/deck_std.h> // For StdDeck_CardMask and card definitions
#include <poker_eval/core/poker_defs.h> // For MAX_HANDS or similar constants, if available

// Only define TRACE and ASSERT if NDEBUG is not defined
#ifndef NDEBUG
#include <stdio.h> // For printf in TRACE
#include <assert.h> // For assert in ASSERT
#define TRACE_OHD(...) do { printf(__VA_ARGS__); } while (0)
#define ASSERT_OHD assert
#else
#define TRACE_OHD(...)
#define ASSERT_OHD(x) ((void)0)
#endif

// Enum for Omaha hand suit properties
typedef enum {
    OMAHA_SUIT_PROPERTY_NONE,    // No specific property (any suit combo for the given ranks)
    OMAHA_SUIT_PROPERTY_DS,      // Double-suited (e.g., AhAs KhKs or AhKh AsKs)
    OMAHA_SUIT_PROPERTY_SS,      // Single-suited (e.g., AhAs KcQd)
    OMAHA_SUIT_PROPERTY_TS,      // Triple-suited (e.g., AhAsAcKd)
    OMAHA_SUIT_PROPERTY_QS,      // Quad-suited / Monotone (e.g., AsKsQsJs)
    OMAHA_SUIT_PROPERTY_RAINBOW  // Rainbow (e.g., AhKsQcJd)
} OmahaSuitProperty;

// Define constants for wildcard rank/suit if not defined elsewhere
#ifndef WILDCARD_RANK
#define WILDCARD_RANK -1
#endif

#ifndef WILDCARD_SUIT
#define WILDCARD_SUIT -1
#endif

#ifndef SUIT_DETERMINED_BY_PROPERTY
#define SUIT_DETERMINED_BY_PROPERTY -2
#endif

// Structure to represent a single card pattern in a query
typedef struct {
    int rank; // Rank (0-12 for 2-A), or WILDCARD_RANK for 'x'
    int suit; // Suit (0-3 for c,d,h,s), WILDCARD_SUIT for 'x', or SUIT_DETERMINED_BY_PROPERTY
} OmahaCardPattern;

// Structure to represent a parsed Omaha hand query
typedef struct {
    OmahaCardPattern patterns[4];     // Four card patterns
    OmahaSuitProperty suit_property;  // Overall suit property for the hand
    int num_specific_cards;           // Number of fully specified cards (e.g., AsKd = 2)
    int num_rank_wildcards;           // Number of 'x' ranks (e.g., AAxx = 2)
    int required_ranks[4];            // Specific ranks required (e.g., for AKQJds, this would be A,K,Q,J)
    int num_required_ranks;           // Count of distinct required ranks
} OmahaHandQuery;

// Using a HandList structure similar to HoldemAgnosticHand for instantiated hands.
// Dynamic allocation allows unlimited hand combinations
typedef struct {
    StdDeck_CardMask* hands;  // Dynamically allocated array
    int count;                 // Current number of hands
    int capacity;              // Current allocated capacity
} OmahaHandList;

// Default initial capacity for dynamic allocation
#define OMAHA_HANDLIST_INITIAL_CAPACITY 1000
// Growth factor when resizing
#define OMAHA_HANDLIST_GROWTH_FACTOR 2

// Function prototypes
int OmahaHand_Parse(const char* handText, OmahaHandQuery* query);
int OmahaHand_Instantiate(const OmahaHandQuery* query, StdDeck_CardMask deadCards, OmahaHandList* handList);

// Memory management functions for OmahaHandList
int OmahaHandList_Init(OmahaHandList* handList, int initial_capacity);
void OmahaHandList_Free(OmahaHandList* handList);
int OmahaHandList_Resize(OmahaHandList* handList, int new_capacity);
int OmahaHandList_AddHand(OmahaHandList* handList, StdDeck_CardMask hand);
void OmahaHandList_Clear(OmahaHandList* handList);

#endif // OMAHA_HAND_DISTRIBUTION_H
