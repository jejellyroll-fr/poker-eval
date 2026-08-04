/*
 * Modern C API for poker-eval library
 * 
 * This header provides a clean, namespace-consistent API with:
 * - Opaque structures with getters/setters
 * - Consistent error handling
 * - Modern function naming conventions
 * - Thread-safe operations
 * 
 * Copyright (C) 2024 poker-eval contributors
 * Licensed under GPL v3 or later
 */

#ifndef POKER_EVAL_MODERN_H
#define POKER_EVAL_MODERN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * ERROR HANDLING
 * ======================================================================== */

typedef enum {
    POKER_EVAL_SUCCESS = 0,
    POKER_EVAL_ERROR_NULL_POINTER,
    POKER_EVAL_ERROR_INVALID_ARGUMENT,
    POKER_EVAL_ERROR_INSUFFICIENT_CARDS,
    POKER_EVAL_ERROR_TOO_MANY_CARDS,
    POKER_EVAL_ERROR_DUPLICATE_CARD,
    POKER_EVAL_ERROR_INVALID_CARD,
    POKER_EVAL_ERROR_MEMORY_ALLOCATION,
    POKER_EVAL_ERROR_NOT_INITIALIZED,
    POKER_EVAL_ERROR_UNSUPPORTED_OPERATION
} poker_eval_error_t;

/**
 * Get human-readable error message for error code
 * @param error Error code
 * @return Constant string describing the error
 */
const char* poker_eval_error_string(poker_eval_error_t error);

/* ========================================================================
 * OPAQUE TYPES
 * ======================================================================== */

/* Forward declarations of opaque types */
typedef struct poker_eval_hand poker_eval_hand_t;
typedef struct poker_eval_deck poker_eval_deck_t;
typedef struct poker_eval_result poker_eval_result_t;
typedef struct poker_eval_context poker_eval_context_t;

/* ========================================================================
 * HAND TYPES AND VALUES
 * ======================================================================== */

typedef enum {
    POKER_HAND_HIGH_CARD = 0,
    POKER_HAND_PAIR,
    POKER_HAND_TWO_PAIR,
    POKER_HAND_THREE_OF_A_KIND,
    POKER_HAND_STRAIGHT,
    POKER_HAND_FLUSH,
    POKER_HAND_FULL_HOUSE,
    POKER_HAND_FOUR_OF_A_KIND,
    POKER_HAND_STRAIGHT_FLUSH,
    POKER_HAND_ROYAL_FLUSH
} poker_hand_type_t;

typedef enum {
    POKER_RANK_TWO = 0,
    POKER_RANK_THREE,
    POKER_RANK_FOUR,
    POKER_RANK_FIVE,
    POKER_RANK_SIX,
    POKER_RANK_SEVEN,
    POKER_RANK_EIGHT,
    POKER_RANK_NINE,
    POKER_RANK_TEN,
    POKER_RANK_JACK,
    POKER_RANK_QUEEN,
    POKER_RANK_KING,
    POKER_RANK_ACE
} poker_rank_t;

typedef enum {
    POKER_SUIT_CLUBS = 0,
    POKER_SUIT_DIAMONDS,
    POKER_SUIT_HEARTS,
    POKER_SUIT_SPADES
} poker_suit_t;

typedef enum {
    POKER_DECK_STANDARD = 0,    /* 52-card deck */
    POKER_DECK_SHORT,           /* 36-card deck (6+ only) */
    POKER_DECK_JOKER,           /* 53-card deck with joker */
    POKER_DECK_ASTUD            /* Ace-to-five lowball deck */
} poker_deck_type_t;

/* ========================================================================
 * CONTEXT MANAGEMENT
 * ======================================================================== */

/**
 * Create a new poker evaluation context
 * @param deck_type Type of deck to use
 * @param context Output parameter for created context
 * @return Error code
 */
poker_eval_error_t poker_eval_context_create(poker_deck_type_t deck_type, 
                                            poker_eval_context_t** context);

/**
 * Destroy a poker evaluation context
 * @param context Context to destroy (can be NULL)
 */
void poker_eval_context_destroy(poker_eval_context_t* context);

/**
 * Enable/disable evaluation caching for performance
 * @param context Evaluation context
 * @param enable True to enable caching, false to disable
 * @return Error code
 */
poker_eval_error_t poker_eval_context_set_caching(poker_eval_context_t* context, 
                                                 bool enable);

#ifdef POKER_EVAL_EXPERIMENTAL
/**
 * Override the game type used by the experimental equity functions. Without
 * this, the game is derived automatically from the hole-card count (2 -> hold'em,
 * 3 -> pineapple, 4 -> omaha, ...). Setting a game lets callers reach variants
 * that share a pocket size with a different default — e.g. Pineapple Hi/Lo
 * (3 cards) as `game_pineapple8`. Pass (enum_game_t)-1 to restore automatic
 * selection.
 * @param context Evaluation context
 * @param game_type Desired enum_game_t, or (enum_game_t)-1 for automatic
 * @return Error code
 */
poker_eval_error_t poker_eval_context_set_game(poker_eval_context_t* context,
                                               int game_type);
#endif /* POKER_EVAL_EXPERIMENTAL */

/* ========================================================================
 * HAND MANAGEMENT
 * ======================================================================== */

/**
 * Create a new empty hand
 * @param hand Output parameter for created hand
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_create(poker_eval_hand_t** hand);

/**
 * Destroy a hand
 * @param hand Hand to destroy (can be NULL)
 */
void poker_eval_hand_destroy(poker_eval_hand_t* hand);

/**
 * Clear all cards from a hand
 * @param hand Hand to clear
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_clear(poker_eval_hand_t* hand);

/**
 * Add a card to a hand
 * @param hand Hand to modify
 * @param rank Card rank
 * @param suit Card suit
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_add_card(poker_eval_hand_t* hand, 
                                           poker_rank_t rank, 
                                           poker_suit_t suit);

/**
 * Add a card from string representation (e.g., "As", "Kh", "2c")
 * @param hand Hand to modify
 * @param card_str String representation of card
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_add_card_string(poker_eval_hand_t* hand, 
                                                  const char* card_str);

/**
 * Remove a card from a hand
 * @param hand Hand to modify
 * @param rank Card rank
 * @param suit Card suit
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_remove_card(poker_eval_hand_t* hand, 
                                              poker_rank_t rank, 
                                              poker_suit_t suit);

/**
 * Get number of cards in hand
 * @param hand Hand to query
 * @param count Output parameter for card count
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_get_count(const poker_eval_hand_t* hand, 
                                            size_t* count);

/**
 * Check if hand contains a specific card
 * @param hand Hand to check
 * @param rank Card rank
 * @param suit Card suit
 * @param contains Output parameter for result
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_contains_card(const poker_eval_hand_t* hand, 
                                                poker_rank_t rank, 
                                                poker_suit_t suit, 
                                                bool* contains);

/**
 * Get card at specific index
 * @param hand Hand to query
 * @param index Card index (0-based)
 * @param rank Output parameter for card rank
 * @param suit Output parameter for card suit
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_get_card(const poker_eval_hand_t* hand, 
                                           size_t index, 
                                           poker_rank_t* rank, 
                                           poker_suit_t* suit);

/**
 * Copy one hand to another
 * @param dest Destination hand
 * @param src Source hand
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_copy(poker_eval_hand_t* dest, 
                                       const poker_eval_hand_t* src);

/* ========================================================================
 * HAND EVALUATION
 * ======================================================================== */

/**
 * Create a new evaluation result
 * @param result Output parameter for created result
 * @return Error code
 */
poker_eval_error_t poker_eval_result_create(poker_eval_result_t** result);

/**
 * Destroy an evaluation result
 * @param result Result to destroy (can be NULL)
 */
void poker_eval_result_destroy(poker_eval_result_t* result);

/**
 * Evaluate a poker hand
 * @param context Evaluation context
 * @param hand Hand to evaluate
 * @param result Output parameter for evaluation result
 * @return Error code
 */
poker_eval_error_t poker_eval_hand_evaluate(poker_eval_context_t* context, 
                                           const poker_eval_hand_t* hand, 
                                           poker_eval_result_t* result);

/**
 * Compare two evaluation results
 * @param result1 First result
 * @param result2 Second result
 * @param comparison Output: -1 if result1 < result2, 0 if equal, 1 if result1 > result2
 * @return Error code
 */
poker_eval_error_t poker_eval_result_compare(const poker_eval_result_t* result1, 
                                            const poker_eval_result_t* result2, 
                                            int* comparison);

/**
 * Get hand type from evaluation result
 * @param result Evaluation result
 * @param hand_type Output parameter for hand type
 * @return Error code
 */
poker_eval_error_t poker_eval_result_get_hand_type(const poker_eval_result_t* result, 
                                                  poker_hand_type_t* hand_type);

/**
 * Get raw evaluation value (for advanced users)
 * @param result Evaluation result
 * @param value Output parameter for raw value
 * @return Error code
 */
poker_eval_error_t poker_eval_result_get_value(const poker_eval_result_t* result, 
                                              uint32_t* value);

/**
 * Get human-readable description of hand
 * @param result Evaluation result
 * @param buffer Output buffer for description
 * @param buffer_size Size of output buffer
 * @return Error code
 */
poker_eval_error_t poker_eval_result_get_description(const poker_eval_result_t* result, 
                                                    char* buffer, 
                                                    size_t buffer_size);

/* ========================================================================
 * EQUITY CALCULATION (experimental)
 * ======================================================================== */

#ifdef POKER_EVAL_EXPERIMENTAL
typedef struct {
    double win_probability;
    double tie_probability;
    double lose_probability;
    uint64_t total_outcomes;
} poker_equity_result_t;

/**
 * Calculate equity between multiple hands
 */
poker_eval_error_t poker_eval_calculate_equity(poker_eval_context_t* context,
                                              const poker_eval_hand_t* const* hands,
                                              size_t num_hands,
                                              const poker_eval_hand_t* board_cards,
                                              const poker_eval_hand_t* dead_cards,
                                              poker_equity_result_t* results);

/**
 * Calculate equity using Monte Carlo simulation
 */
poker_eval_error_t poker_eval_calculate_equity_monte_carlo(poker_eval_context_t* context,
                                                          const poker_eval_hand_t* const* hands,
                                                          size_t num_hands,
                                                          const poker_eval_hand_t* board_cards,
                                                          const poker_eval_hand_t* dead_cards,
                                                          uint64_t num_iterations,
                                                          poker_equity_result_t* results);
#endif /* POKER_EVAL_EXPERIMENTAL */

/* ========================================================================
 * UTILITY FUNCTIONS
 * ======================================================================== */

/**
 * Get string representation of hand type
 * @param hand_type Hand type
 * @return Constant string describing hand type
 */
const char* poker_eval_hand_type_string(poker_hand_type_t hand_type);

/**
 * Get string representation of rank
 * @param rank Card rank
 * @return Constant string for rank (e.g., "A", "K", "Q")
 */
const char* poker_eval_rank_string(poker_rank_t rank);

/**
 * Get string representation of suit
 * @param suit Card suit
 * @return Constant string for suit (e.g., "s", "h", "d", "c")
 */
const char* poker_eval_suit_string(poker_suit_t suit);

/**
 * Parse card from string representation
 * @param card_str String representation (e.g., "As", "Kh")
 * @param rank Output parameter for rank
 * @param suit Output parameter for suit
 * @return Error code
 */
poker_eval_error_t poker_eval_parse_card(const char* card_str, 
                                        poker_rank_t* rank, 
                                        poker_suit_t* suit);

/**
 * Get library version information
 * @param major Output parameter for major version
 * @param minor Output parameter for minor version
 * @param patch Output parameter for patch version
 * @return Error code
 */
poker_eval_error_t poker_eval_get_version(int* major, int* minor, int* patch);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_MODERN_H */
