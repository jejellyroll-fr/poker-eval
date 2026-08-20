/**
 * @file range.h
 * @brief Poker range parsing and manipulation API
 *
 * This header provides functions for parsing, creating, and manipulating
 * poker hand ranges. Ranges represent sets of possible hands a player might hold.
 *
 * @section range_syntax Range Syntax
 *
 * Hold'em range notation:
 * - Single hands: `AhKs`, `QdQc`
 * - Pairs: `AA`, `KK`, `22`
 * - Suited hands: `AKs`, `T9s`
 * - Offsuit hands: `AKo`, `QJo`
 * - Plus notation: `TT+` (TT through AA), `ATs+` (ATs through AKs)
 * - Ranges: `22-99`, `A2s-A9s`
 * - Weighted: `AA:0.5` (50% of the time)
 * - Operators: `+` (add), `-` (remove), `!` (exclude)
 *
 * Omaha range notation:
 * - Specific hands: `AhKsQdJc`
 * - Categories: `AAxx`, `AKds` (double-suited)
 * - PLO categories: `AADS`, `BROADWAYDS`, `RUNDOWN`
 *
 * @section range_example Example
 * @code{.c}
 * #include <poker_eval/range.h>
 *
 * pe_range_t *range = NULL;
 * StdDeck_CardMask dead;
 * StdDeck_CardMask_RESET(dead);
 *
 * // Parse a Hold'em range
 * pe_status_t st = pe_range_parse(game_holdem, "AA,KK,QQ,AKs", dead, NULL, &range);
 * if (st == PE_STATUS_OK) {
 *     printf("Range has %zu combos\n", range->count);
 *     pe_range_free(range);
 * }
 * @endcode
 *
 * @copyright Copyright (C) 2025 poker-eval contributors
 */

#ifndef __PE_RANGE_H__
#define __PE_RANGE_H__

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/pokereval_export.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status codes for range and equity operations
 *
 * Note: pe_status_t may already be defined by <poker_eval/core/status.h>
 * when this header is included after poker_eval.h; the guard below lets the
 * first definition win.
 */
#ifndef PE_STATUS_T_DEFINED
#define PE_STATUS_T_DEFINED
typedef enum {
    PE_STATUS_OK = 0,             /**< Operation completed successfully */
    PE_STATUS_ERROR = 1,          /**< Generic error */
    PE_STATUS_INVALID_ARG = 2,    /**< Invalid argument provided */
    PE_STATUS_PARSE_ERROR = 3,    /**< Range string parsing failed */
    PE_STATUS_OUT_OF_MEMORY = 4,  /**< Memory allocation failed */
    PE_STATUS_NOT_IMPLEMENTED = 5 /**< Feature not yet implemented */
} pe_status_t;
#endif

/**
 * @brief Single weighted hand combination
 *
 * Represents one specific hand (e.g., AhKs) with an optional weight.
 */
typedef struct {
    StdDeck_CardMask hand; /**< Card mask representing the hand */
    double weight;         /**< Weight/frequency (0.0 to 1.0, default 1.0) */
} pe_combo_t;

/**
 * @brief Unified range structure
 *
 * Contains all possible hands in a range with their weights.
 * Dynamically allocated and must be freed with pe_range_free().
 */
typedef struct {
    pe_combo_t *combos;    /**< Array of hand combinations */
    size_t count;          /**< Number of combinations in the range */
    size_t capacity;       /**< Allocated capacity (internal use) */
    enum_game_t game_type; /**< Game variant this range is for */
    double total_weight;   /**< Sum of all weights (for normalization) */
} pe_range_t;

/**
 * @brief Range parsing options
 *
 * Configure how range strings are parsed.
 */
typedef struct {
    int strict_syntax;     /**< Fail on minor syntax errors (default: 0) */
    int allow_weights;     /**< Allow weight syntax like "AA:0.5" (default: 1) */
    double default_weight; /**< Default weight for unweighted hands (default: 1.0) */
} pe_parse_opts_t;

/**
 * @brief Strategy likelihood callback returning P(action | combo).
 *
 * The combo index is stable for the duration of the update and the card mask
 * identifies the exact two-card combination being evaluated.
 */
typedef double (*pe_action_likelihood_fn)(
    size_t combo_index,
    StdDeck_CardMask combo_cards,
    int action_id,
    void *user_data
);

/**
 * @brief Range compilation options
 *
 * Configure range optimization during compilation.
 */
typedef struct {
    int canonicalize;      /**< Sort and deduplicate hands */
} pe_compile_opts_t;

/**
 * @brief Compiled range type
 *
 * Currently an alias for pe_range_t. May become opaque in future versions.
 */
typedef pe_range_t pe_compiled_range_t;

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * @brief Initialize parsing options with default values
 *
 * Sets opts to: strict_syntax=0, allow_weights=1, default_weight=1.0
 *
 * @param[out] opts Options structure to initialize
 */
void pe_range_opts_init(pe_parse_opts_t *opts);

/**
 * @brief Parse a range string into a pe_range_t object
 *
 * Parses standard poker range notation and creates a range containing
 * all matching hand combinations.
 *
 * @param[in] variant Game variant (game_holdem, game_omaha, game_omaha8, etc.)
 * @param[in] range_str Range string (e.g., "AA,KK,AKs", "QQ+,AJs+")
 * @param[in] dead_cards Cards to exclude from the range (board cards, known cards)
 * @param[in] opts Parsing options (NULL for defaults)
 * @param[out] out_range Pointer to receive the allocated range (caller must free)
 *
 * @return PE_STATUS_OK on success
 * @return PE_STATUS_PARSE_ERROR if range_str is invalid
 * @return PE_STATUS_OUT_OF_MEMORY if allocation fails
 * @return PE_STATUS_INVALID_ARG if out_range is NULL
 *
 * @code{.c}
 * pe_range_t *range = NULL;
 * pe_status_t st = pe_range_parse(game_holdem, "AA,KK,QQ", dead, NULL, &range);
 * if (st == PE_STATUS_OK) {
 *     // Use range...
 *     pe_range_free(range);
 * }
 * @endcode
 *
 * @see pe_range_free() to release the allocated range
 */
pe_status_t pe_range_parse(
    enum_game_t variant,
    const char *range_str,
    StdDeck_CardMask dead_cards,
    const pe_parse_opts_t *opts,
    pe_range_t **out_range
);

/**
 * @brief Free a range object and its resources
 *
 * Releases all memory associated with a range. Safe to call with NULL.
 *
 * @param[in] range Range to free (may be NULL)
 */
void pe_range_free(pe_range_t *range);

/**
 * @brief Create a new empty range
 *
 * Creates an empty range that can be populated manually or combined
 * with other ranges.
 *
 * @param[in] variant Game variant for the range
 * @param[out] out_range Pointer to receive the allocated range
 *
 * @return PE_STATUS_OK on success
 * @return PE_STATUS_OUT_OF_MEMORY if allocation fails
 */
pe_status_t pe_range_create(enum_game_t variant, pe_range_t **out_range);

/**
 * @brief Apply Bayes' rule to a range after observing an action.
 *
 * Each weight is multiplied by P(action | combo), then normalized by the
 * action evidence. The range's original total weight is preserved, so this
 * works both for probability-normalized ranges and for the usual combo-count
 * representation. Combos with zero likelihood receive weight zero.
 *
 * @return PE_STATUS_OK, PE_STATUS_INVALID_ARG, or PE_STATUS_ERROR when the
 *         range is empty/invalid or the observed action has zero evidence.
 */
extern POKEREVAL_EXPORT pe_status_t pe_range_bayesian_update(
    pe_range_t *range,
    int observed_action,
    pe_action_likelihood_fn likelihood_fn,
    void *user_data
);

/**
 * @brief Compile a range (sort, deduplicate, optimize)
 *
 * Converts a raw parsed range into canonical form by sorting hands
 * and removing duplicates. May also build optimization structures.
 *
 * @param[in] in_range Input range to compile
 * @param[in] opts Compilation options (NULL for defaults)
 * @param[out] out_range Pointer to receive the compiled range
 *
 * @return PE_STATUS_OK on success
 */
pe_status_t pe_range_compile(
    const pe_range_t *in_range,
    const pe_compile_opts_t *opts,
    pe_compiled_range_t **out_range
);

/**
 * @brief Range combination operations
 */
typedef enum {
    PE_OP_UNION,      /**< Combine ranges (A + B) */
    PE_OP_INTERSECT,  /**< Common hands only (A ∩ B) */
    PE_OP_DIFFERENCE  /**< Remove hands (A - B) */
} pe_range_op_t;

/**
 * @brief Combine two ranges using a set operation
 *
 * Creates a new range from the combination of two input ranges.
 *
 * @param[in] range1 First range
 * @param[in] range2 Second range
 * @param[in] op Operation to perform (union, intersect, difference)
 * @param[out] out_range Pointer to receive the result range
 *
 * @return PE_STATUS_OK on success
 *
 * @code{.c}
 * pe_range_t *combined = NULL;
 * pe_range_combine(range1, range2, PE_OP_UNION, &combined);
 * @endcode
 */
pe_status_t pe_range_combine(
    const pe_range_t *range1,
    const pe_range_t *range2,
    pe_range_op_t op,
    pe_range_t **out_range
);

/**
 * @brief Filter dead cards from a range
 *
 * Creates a new range with all hands containing dead cards removed.
 *
 * @param[in] in_range Input range
 * @param[in] dead_cards Cards to filter out
 * @param[out] out_range Pointer to receive the filtered range
 *
 * @return PE_STATUS_OK on success
 */
pe_status_t pe_range_filter_dead(
    const pe_range_t *in_range,
    StdDeck_CardMask dead_cards,
    pe_range_t **out_range
);

/**
 * @brief Get top N percent of hands by strength
 *
 * Creates a range containing the top percentage of starting hands.
 * Uses standard hand rankings (AA, KK, QQ, AKs, etc.).
 *
 * @param[in] variant Game variant
 * @param[in] percent Percentage of hands to include (0.0 to 100.0)
 * @param[in] dead_cards Cards to exclude
 * @param[out] out_range Pointer to receive the range
 *
 * @return PE_STATUS_OK on success
 *
 * @code{.c}
 * pe_range_t *top10 = NULL;
 * pe_range_top_percent(game_holdem, 10.0, dead, &top10);  // Top 10%
 * @endcode
 */
pe_status_t pe_range_top_percent(
    enum_game_t variant,
    double percent,
    StdDeck_CardMask dead_cards,
    pe_range_t **out_range
);

/**
 * @brief Get human-readable error message for status code
 *
 * @param[in] status Status code to describe
 * @return Static string describing the error (never NULL)
 */
const char* pe_error_string(pe_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* __PE_RANGE_H__ */
