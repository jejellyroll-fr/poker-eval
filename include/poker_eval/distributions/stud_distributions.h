#ifndef STUD_HAND_DISTRIBUTION_H
#define STUD_HAND_DISTRIBUTION_H

#include <poker_eval/deck/deck_std.h>   // For StdDeck_CardMask and card definitions
#include <poker_eval/core/poker_defs.h> // For any relevant constants like MAX_CARDS_IN_HAND
#include <stdbool.h>    // For bool type

// Define constants if not available elsewhere
#ifndef MAX_STUD_CARDS
#define MAX_STUD_CARDS 7 // For 7-Card Stud
#endif

#ifndef WILDCARD_CARD_VAL // Could use a specific value like -1 for rank/suit to denote wildcard
#define WILDCARD_CARD_VAL -1 // Indicates this card slot is a wildcard 'x'
#endif

// Only define TRACE and ASSERT if NDEBUG is not defined (similar to OmahaHandDistribution.h)
#ifndef NDEBUG
#include <stdio.h> // For printf in TRACE
#include <assert.h> // For assert in ASSERT
#define TRACE_SHD(...) do { printf(__VA_ARGS__); } while (0)
#define ASSERT_SHD assert
#else
#define TRACE_SHD(...)
#define ASSERT_SHD(x) ((void)0)
#endif


// Structure to represent a single card in a Stud hand query.
typedef struct {
    int rank;         // Rank (0-12 for 2-A), or WILDCARD_CARD_VAL if 'x'
    int suit;         // Suit (0-3 for c,d,h,s), or WILDCARD_CARD_VAL if 'x'
    bool is_up_card;  // True if this card is an up-card, false if a down-card
} StudCardPattern;


// Structure to represent a parsed Stud hand query
typedef struct {
    StudCardPattern patterns[MAX_STUD_CARDS]; // Array to store each card's pattern
    int num_pattern_cards;  // Number of cards described by the input pattern (explicit cards + 'x's in string)
    int game_total_cards;   // Total cards the game is played to (e.g., 7 for 7-stud, 5 for 5-stud). Instantiator fills to this.
    int num_known_cards;    // Count of specific cards (not 'x') provided in the query
    int num_wildcards_in_pattern; // Count of 'x's explicitly in the query string
    // num_wildcards_to_fill = game_total_cards - num_known_cards - num_wildcards_in_pattern (if pattern is partial)
    // Or, more simply: num_wildcards_to_fill = game_total_cards - num_known_cards (if 'x' in pattern are placeholders for known card *types*)
    // Let's simplify: instantiator always fills up to game_total_cards.
    // num_wildcards_to_instantiate = game_total_cards - num_known_cards; (This is what matters for instantiation)

    int num_down_cards_specified; // Number of cards specified as down cards in the query pattern
    int num_up_cards_specified;   // Number of cards specified as up cards in the query pattern
} StudHandQuery;


// For instantiated hands, we can reuse the StdDeck_CardMask to represent a full hand.
#define MAX_STUD_COMBOS 2000 
typedef struct {
    StdDeck_CardMask hands[MAX_STUD_COMBOS]; 
    int count;
} StudHandList;


// Function prototypes
int StudHand_Parse(const char* handText, int game_cards, StudHandQuery* query);
int StudHand_Instantiate(const StudHandQuery* query, StdDeck_CardMask deadCards, StudHandList* handList);

#endif // STUD_HAND_DISTRIBUTION_H
