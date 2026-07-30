/*
 * Copyright (C) 2024
 *           Poker-eval contributors
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ADVANCED_RANGE_PARSER_H__
#define __ADVANCED_RANGE_PARSER_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/distributions/plo_nomenclature.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum number of hands in a range */
#define ARP_MAX_RANGE_SIZE 2048

    /* Range operation types */
    typedef enum
    {
        ARP_OP_NONE = 0,
        ARP_OP_ADD,      /* + operator */
        ARP_OP_SUBTRACT, /* - operator */
        ARP_OP_EXCLUDE   /* ! operator */
    } arp_operation_t;

    /* Range token types */
    typedef enum
    {
        ARP_TOKEN_UNKNOWN = 0,
        ARP_TOKEN_PAIR_RANGE,    /* AA-TT */
        ARP_TOKEN_HAND_RANGE,    /* AK-AJ, AKs-AJs, etc. */
        ARP_TOKEN_SUITED,        /* AKs */
        ARP_TOKEN_OFFSUIT,       /* AKo */
        ARP_TOKEN_BOTH,          /* AK (both suited and offsuit) */
        ARP_TOKEN_PERCENTAGE,    /* 20% */
        ARP_TOKEN_SPECIFIC_HAND, /* AsKh */
        ARP_TOKEN_OPERATION,     /* +, -, ! */
        ARP_TOKEN_COMMA,         /* , */
        ARP_TOKEN_STUD_PATTERN,  /* (AA)K, (ss)Ks */
        ARP_TOKEN_PLO_PATTERN,   /* AAxxds, JT98r, etc. */
        ARP_TOKEN_PLO_CATEGORY,  /* PLO hand categories */
        ARP_TOKEN_LPAREN,        /* ( */
        ARP_TOKEN_RPAREN,        /* ) */
        ARP_TOKEN_END
    } arp_token_type_t;

    /* Parsed range token */
    typedef struct
    {
        arp_token_type_t type;
        arp_operation_t operation;
        double weight;
        bool has_weight;
        union
        {
            struct
            {
                int high_rank; /* For AA-TT: high=ACE, low=TEN */
                int low_rank;
            } pair_range;
            struct
            {
                int rank1_high; /* For AK-AJ: A */
                int rank2_high; /* For AK-AJ: K */
                int rank2_low;  /* For AK-AJ: J */
                bool suited;    /* true for AKs-AJs */
                bool offsuit;   /* true for AKo-AJo */
                bool both;      /* true for AK-AJ */
            } hand_range;
            struct
            {
                int rank1; /* For AK: rank1=ACE, rank2=KING */
                int rank2;
                bool suited; /* true for AKs, false for AKo */
                bool both;   /* true for AK (both s and o) */
            } hand;
            struct
            {
                int suit1; /* For AsKh: specific suits */
                int suit2;
                int rank1;
                int rank2;
            } specific;
            struct
            {
                char pattern[32];         /* PLO pattern string */
                PLOHandCategory category; /* PLO category if applicable */
            } plo;
            struct
            {
                char pattern[32];         /* Stud pattern string */
            } stud;
            float percentage; /* For 20%: 0.20 */
        } data;
    } arp_token_t;

    /* Expression node for parsing complex expressions */
    typedef struct arp_expr_node_s
    {
        enum
        {
            ARP_EXPR_RANGE,    /* Leaf node: range pattern */
            ARP_EXPR_OPERATION /* Internal node: operation */
        } type;

        union
        {
            struct
            {
                arp_token_t token; /* Range token (percentage, pattern, etc.) */
            } range;

            struct
            {
                arp_operation_t op;            /* Operation type */
                struct arp_expr_node_s *left;  /* Left operand */
                struct arp_expr_node_s *right; /* Right operand (NULL for unary !) */
            } operation;
        } data;
    } arp_expr_node_t;

    /* Range parsing context */
    typedef struct
    {
        const char *input;
        size_t position;
        size_t length;
        arp_token_t *tokens;
        size_t token_count;
        size_t token_capacity;
        size_t current_token;       /* Current token index for expression parsing */
        arp_expr_node_t *expr_tree; /* Expression tree root */
        char error_message[256];
    } arp_context_t;

    /* Parsed range result */
    typedef struct
    {
        StdDeck_CardMask *hands;
        double *weights;
        size_t count;
        size_t capacity;
        double total_weight;
        bool has_weights;
        float percentage_used; /* If percentage was used, store it here */
        bool is_percentage;    /* True if range was defined by percentage */
        enum_game_t game_type; /* Game type this range was created for */
        bool is_omaha;         /* True if this is an Omaha range */
        void *internal_data;   /* Opaque pointer for internal use (e.g., hash table) */
    } arp_range_t;

    /* Core API Functions */

    /**
     * Initialize a range parsing context
     * @param ctx Context to initialize
     * @param range_string Range string to parse (e.g., "AA-TT, AKs, 20%")
     * @return 1 on success, 0 on failure
     */
    int ARP_InitContext(arp_context_t *ctx, const char *range_string);

    /**
     * Free resources used by a parsing context
     * @param ctx Context to free
     */
    void ARP_FreeContext(arp_context_t *ctx);

    /**
     * Parse a range string into a range structure
     * @param range_string Range string (e.g., "AA-TT, AKs, AKo")
     * @param dead_cards Cards to exclude from the range
     * @param game_type Game type (for percentage calculations)
     * @param result Output range structure
     * @return 1 on success, 0 on failure
     */
    int ARP_ParseRange(const char *range_string, StdDeck_CardMask dead_cards, enum_game_t game_type, arp_range_t *result);

    /**
     * Convert an arp_range_t to a PlayerRange for use with equity calculations
     * @param arp_range Source range
     * @param player_range Destination PlayerRange
     * @return 1 on success, 0 on failure
     */
    int ARP_ToPlayerRange(const arp_range_t *arp_range, PlayerRange *player_range);

    /**
     * Free resources used by a range
     * @param range Range to free
     */
    void ARP_FreeRange(arp_range_t *range);

    /* Utility Functions */

    /**
     * Get a string representation of a range
     * @param range Range to describe
     * @param buffer Output buffer
     * @param buffer_size Size of output buffer
     * @return Number of characters written
     */
    int ARP_RangeToString(const arp_range_t *range, char *buffer, size_t buffer_size);

    /**
     * Get the percentage of all possible hands that this range represents
     * @param range Range to analyze
     * @param game_type Game type (for determining total possible hands)
     * @return Percentage (0.0 to 1.0)
     */
    float ARP_GetRangePercentage(const arp_range_t *range, enum_game_t game_type);

    /**
     * Validate a range string without fully parsing it
     * @param range_string Range string to validate
     * @param error_buffer Buffer for error message (can be NULL)
     * @param error_buffer_size Size of error buffer
     * @return 1 if valid, 0 if invalid
     */
    int ARP_ValidateRangeString(const char *range_string, char *error_buffer, size_t error_buffer_size);

    /* Advanced Operations */

    /**
     * Combine two ranges using an operation
     * @param range1 First range
     * @param range2 Second range
     * @param operation Operation to perform (ADD, SUBTRACT, etc.)
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_CombineRanges(const arp_range_t *range1, const arp_range_t *range2,
                          arp_operation_t operation, arp_range_t *result);

    /**
     * Get the top N% of hands for a specific game
     * @param percentage Percentage (0.0 to 1.0)
     * @param game_type Game type
     * @param dead_cards Cards to exclude
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_GetTopPercentage(float percentage, enum_game_t game_type,
                             StdDeck_CardMask dead_cards, arp_range_t *result);

    /* Stud-specific Functions */

    /**
     * Get the top N% of Stud starting hands (3rd street)
     * @param percentage Percentage (0.0 to 1.0)
     * @param game_type Stud game type (game_7stud, game_7stud8, game_razz)
     * @param dead_cards Cards to exclude
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_GetStudTopPercentage(float percentage, enum_game_t game_type,
                                 StdDeck_CardMask dead_cards, arp_range_t *result);

    /* Omaha-specific Functions */

    /**
     * Parse an Omaha range string (supports PLO patterns)
     * @param range_string Range string (e.g., "AAxxds, JT98r, 20%")
     * @param dead_cards Cards to exclude from the range
     * @param game_type Omaha game type (game_omaha, game_omaha8, etc.)
     * @param result Output range structure
     * @return 1 on success, 0 on failure
     */
    int ARP_ParseOmahaRange(const char *range_string, StdDeck_CardMask dead_cards,
                            enum_game_t game_type, arp_range_t *result);

    /**
     * Convert an arp_range_t to an OmahaHandList for use with PLO functions
     * @param arp_range Source range
     * @param omaha_list Destination OmahaHandList (must be initialized)
     * @return 1 on success, 0 on failure
     */
    int ARP_ToOmahaHandList(const arp_range_t *arp_range, OmahaHandList *omaha_list);

    /**
     * Get the top N% of Omaha hands
     * @param percentage Percentage (0.0 to 1.0)
     * @param game_type Omaha game type
     * @param dead_cards Cards to exclude
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_GetOmahaTopPercentage(float percentage, enum_game_t game_type,
                                  StdDeck_CardMask dead_cards, arp_range_t *result);

    /**
     * Parse a PLO pattern and add to range
     * @param pattern PLO pattern (e.g., "AAxxds", "JT98r")
     * @param dead_cards Cards to exclude
     * @param range Range to add hands to
     * @return 1 on success, 0 on failure
     */
    int ARP_AddPLOPattern(const char *pattern, StdDeck_CardMask dead_cards, arp_range_t *range);

    /**
     * Validate an Omaha range string
     * @param range_string Range string to validate
     * @param error_buffer Buffer for error message (can be NULL)
     * @param error_buffer_size Size of error buffer
     * @return 1 if valid, 0 if invalid
     */
    int ARP_ValidateOmahaRangeString(const char *range_string, char *error_buffer, size_t error_buffer_size);

    /* Advanced Expression Parsing Functions (Phase 2) */

    /**
     * Parse a complex range expression with operators and parentheses
     * @param expression Range expression (e.g., "AAxxds + KKxxds - (QQxx + JJxx)")
     * @param dead_cards Cards to exclude from the range
     * @param game_type Game type (for percentage calculations)
     * @param result Output range structure
     * @return 1 on success, 0 on failure
     */
    int ARP_ParseComplexExpression(const char *expression, StdDeck_CardMask dead_cards,
                                   enum_game_t game_type, arp_range_t *result);

    /**
     * Evaluate an expression tree and generate the resulting range
     * @param expr_tree Root of the expression tree
     * @param dead_cards Cards to exclude
     * @param game_type Game type
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_EvaluateExpressionTree(arp_expr_node_t *expr_tree, StdDeck_CardMask dead_cards,
                                   enum_game_t game_type, arp_range_t *result);

    /**
     * Free an expression tree and all its nodes
     * @param expr_tree Root of the expression tree to free
     */
    void ARP_FreeExpressionTree(arp_expr_node_t *expr_tree);

    /**
     * Validate a complex range expression
     * @param expression Expression string to validate
     * @param error_buffer Buffer for error message (can be NULL)
     * @param error_buffer_size Size of error buffer
     * @return 1 if valid, 0 if invalid
     */
    int ARP_ValidateComplexExpression(const char *expression, char *error_buffer, size_t error_buffer_size);

    /* Cache Management Functions */

    /**
     * Initialize the range percentage cache
     * Should be called once at startup
     */
    void ARP_InitCache(void);

    /**
     * Clear all entries in the cache
     */
    void ARP_ClearCache(void);

    typedef struct
    {
        int total_entries;
        int valid_entries;
        size_t total_memory_bytes;
    } arp_cache_stats_t;

    /**
     * Get statistics about the cache
     * @param stats Output structure
     */
    void ARP_GetCacheStats(arp_cache_stats_t *stats);

    /* Utility Functions (Phase 4) */

    /**
     * Count combinations without generating full hand list if possible
     * @param range_string Range string to count
     * @param dead_cards Cards to exclude
     * @param game_type Game type
     * @return Number of combinations, 0 on error
     */
    size_t ARP_CountCombinations(const char *range_string, StdDeck_CardMask dead_cards, enum_game_t game_type);

    /**
     * Clone a range (deep copy)
     * @param source Source range
     * @param dest Destination range (will be allocated)
     * @return 1 on success, 0 on failure
     */
    int ARP_CloneRange(const arp_range_t *source, arp_range_t *dest);

    /**
     * Compare two ranges for equality
     * @param range1 First range
     * @param range2 Second range
     * @return true if equal, false otherwise
     */
    bool ARP_RangesEqual(const arp_range_t *range1, const arp_range_t *range2);

    /**
     * Get intersection of two ranges (hands in both)
     * @param range1 First range
     * @param range2 Second range
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_IntersectRanges(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result);

    /**
     * Union of two ranges (all hands in either)
     * @param range1 First range
     * @param range2 Second range
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_UnionRanges(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result);

    /**
     * Subtract range2 from range1 (hands in range1 but not range2)
     * @param range1 First range
     * @param range2 Second range
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_SubtractRanges(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result);

    /**
     * Check if a specific hand is in the range
     * @param range Range to check
     * @param hand Hand to look for
     * @return true if hand is in range, false otherwise
     */
    bool ARP_ContainsHand(const arp_range_t *range, StdDeck_CardMask hand);

    /**
     * Export range to file in readable format
     * @param range Range to export
     * @param output Output file
     * @param game_type Game type (for formatting)
     * @return Number of hands written, 0 on error
     */
    int ARP_ExportRange(const arp_range_t *range, FILE *output, enum_game_t game_type);

    /**
     * Import range from exported file
     * @param input Input file
     * @param result Output range
     * @return 1 on success, 0 on failure
     */
    int ARP_ImportRange(FILE *input, arp_range_t *result);

    /* Error Handling (Phase 4) */

    typedef enum {
        ARP_ERROR_NONE = 0,
        ARP_ERROR_INVALID_SYNTAX,
        ARP_ERROR_INVALID_RANK,
        ARP_ERROR_INVALID_SUIT,
        ARP_ERROR_INCOMPLETE_RANGE,
        ARP_ERROR_MISMATCHED_OPERATORS,
        ARP_ERROR_UNCLOSED_PARENTHESIS,
        ARP_ERROR_INVALID_PERCENTAGE,
        ARP_ERROR_EMPTY_RANGE,
        ARP_ERROR_MEMORY_ALLOCATION,
        ARP_ERROR_UNSUPPORTED_GAME,
        ARP_ERROR_INVALID_DEAD_CARDS
    } arp_error_code_t;

    typedef struct {
        int error_position;         /* Position in input string */
        int error_line;            /* Line number (for multi-line) */
        int error_column;          /* Column number */
        char error_token[32];      /* Problematic token */
        char error_message[256];   /* Error description */
        char suggestion[256];      /* Suggested fix */
        int error_code;            /* Numeric error code */
    } arp_error_details_t;

    /**
     * Format error details into a string
     * @param details Error details structure
     * @param buffer Output buffer
     * @param buffer_size Size of output buffer
     * @return Number of characters written
     */
    int ARP_FormatError(const arp_error_details_t *details, char *buffer, size_t buffer_size);

    /**
     * Parse a range string with detailed error reporting
     * @param range_string Range string to parse
     * @param dead_cards Cards to exclude
     * @param game_type Game type
     * @param result Output range
     * @param error_details Output error details (can be NULL)
     * @return 1 on success, 0 on failure
     */
    int ARP_ParseRangeWithError(const char *range_string, StdDeck_CardMask dead_cards,
                                enum_game_t game_type, arp_range_t *result,
                                arp_error_details_t *error_details);

    /* Supported Range Syntax Examples:
     *
     * Basic Hands:
     *   "AA"          - Pocket aces
     *   "AKs"         - Ace-king suited
     *   "AKo"         - Ace-king offsuit
     *   "AK"          - Ace-king (both suited and offsuit)
     *   "AsKh"        - Specific ace of spades, king of hearts
     *
     * Ranges:
     *   "AA-TT"       - All pocket pairs from aces to tens
     *   "AK-AJ"       - AK, AQ, AJ (both suited and offsuit)
     *   "AKs-AJs"     - AKs, AQs, AJs (suited only)
     *
     * Percentages:
     *   "20%"         - Top 20% of hands
     *   "5.5%"        - Top 5.5% of hands
     *
     * Operations:
     *   "AA-TT + AKs" - Pocket pairs AA-TT plus AK suited
     *   "20% - AA"    - Top 20% minus pocket aces
     *   "AK, !AKo"    - AK but exclude AK offsuit (so just AKs)
     *
     * Complex Examples:
     *   "AA-TT, AKs, AKo, AQs"
     *   "20% - 22, 33"
     *   "AA-TT + AK-AJ + KQs"
     *
     * Omaha/PLO Patterns:
     *   "AAxxds"      - Pocket aces double-suited with any two cards
     *   "JT98r"       - Jack-Ten-Nine-Eight rainbow (four different suits)
     *   "KKxxss"      - Pocket kings single-suited
     *   "AsKdQhJc"    - Specific four-card hand
     *   "AAxx"        - Pocket aces with any two cards (any suits)
     *   "JT98"        - Jack-Ten-Nine-Eight (any suits)
     *   "20%"         - Top 20% of Omaha hands (when used with Omaha game types)
     *
     * PLO Operations (Phase 2):
     *   "AAxxds + KKxxds"     - Union of two patterns
     *   "20% - AAxx"          - Top 20% minus all pocket aces
     *   "JT98r + JT98ds"      - Rundown in rainbow and double-suited
     *   "!(AAxx + KKxx)"      - Exclude pocket aces and kings
     *   "(AAxx + KKxx) - QQxxr" - Complex expression with parentheses
     *   "20% - (AAxx + KKxx + QQxx)" - Percentage minus specific hands
     *
     * Operator Precedence (highest to lowest):
     *   1. Parentheses ()
     *   2. Unary NOT (!)
     *   3. Addition (+) and Subtraction (-) [left-to-right]
     *   4. Comma (,) [lowest precedence]
     */

#ifdef __cplusplus
}
#endif

#endif /* __ADVANCED_RANGE_PARSER_H__ */
