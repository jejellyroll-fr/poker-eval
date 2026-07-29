/**
 * @file equity.h
 * @brief Poker equity calculation API
 *
 * This header provides functions for calculating poker hand equity,
 * including range vs range calculations, multiway pots, and preflop equity.
 *
 * @section equity_usage Basic Usage
 * @code{.c}
 * #include <poker_eval/equity.h>
 * #include <poker_eval/range.h>
 *
 * // Parse ranges
 * pe_range_t *r1, *r2;
 * pe_range_parse(game_holdem, "AA,KK,AKs", dead, NULL, &r1);
 * pe_range_parse(game_holdem, "QQ+,AKo", dead, NULL, &r2);
 *
 * // Calculate preflop equity
 * const pe_range_t *ranges[] = {r1, r2};
 * pe_equity_result_multi_t result;
 * pe_equity_preflop(game_holdem, ranges, 2, NULL, &result);
 *
 * printf("Player 1: %.2f%%, Player 2: %.2f%%\n",
 *        result.results[0].equity * 100,
 *        result.results[1].equity * 100);
 * @endcode
 *
 * @copyright Copyright (C) 2025 poker-eval contributors
 */

#ifndef __PE_EQUITY_PUBLIC_H__
#define __PE_EQUITY_PUBLIC_H__

#include <poker_eval/range.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/eval_context.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Single player equity result
 *
 * Contains the equity breakdown for one player in an equity calculation.
 */
typedef struct {
    double equity;       /**< Total equity (0.0 to 1.0), including ties */
    double win_prob;     /**< Probability of winning outright (exclusive) */
    double tie_prob;     /**< Probability of tying */
    double ev;           /**< Expected Value (chips/pot share) */
} pe_equity_result_t;

/**
 * @brief Single player Hi/Lo equity result
 *
 * Contains equity breakdown for Hi/Lo split pot games (Omaha Hi/Lo, Stud Hi/Lo).
 */
typedef struct {
    double equity_hi;    /**< Equity from High pot (0.0 to 0.5) */
    double equity_lo;    /**< Equity from Low pot (0.0 to 0.5) */
    double win_hi_prob;  /**< Probability of winning High */
    double tie_hi_prob;  /**< Probability of tying High */
    double win_lo_prob;  /**< Probability of winning Low */
    double tie_lo_prob;  /**< Probability of tying Low */
    double scoop_prob;   /**< Probability of scooping (winning both Hi and Lo) */
} pe_hilo_equity_result_t;

/**
 * @brief Multiway equity result container
 *
 * Contains results for all players in a multiway equity calculation.
 * Supports up to 10 players.
 */
typedef struct {
    int num_players;                          /**< Number of players in the calculation */
    pe_equity_result_t results[10];           /**< Results for each player (index 0..num_players-1) */
    pe_hilo_equity_result_t hilo_results[10]; /**< Hi/Lo results if applicable */
    long samples;                             /**< Number of samples evaluated */
    int exact;                                /**< 1 if exact/exhaustive calculation, 0 if Monte Carlo */
} pe_equity_result_multi_t;

/**
 * @brief Equity calculation options
 *
 * Configure how equity calculations are performed.
 */
typedef struct {
    int is_monte_carlo;      /**< Force Monte Carlo (1) or use automatic heuristic (0) */
    long iterations;         /**< Number of Monte Carlo iterations (default: 200000) */
    int timeout_ms;          /**< Timeout in milliseconds (0 = none). @note Not fully implemented */
} pe_equity_opts_t;

/**
 * @brief Calculate equity for Range vs Range (Heads-up)
 *
 * Computes equity between two player ranges, optionally with a known board.
 * Uses exact enumeration for river boards, Monte Carlo for earlier streets.
 *
 * @param[in] ctx Evaluation context (can be NULL for default Hold'em context)
 * @param[in] game_variant Game variant (game_holdem, game_omaha, game_holdem8, etc.)
 * @param[in] r1 Player 1's range (parsed via pe_range_parse())
 * @param[in] r2 Player 2's range (parsed via pe_range_parse())
 * @param[in] board Current board cards (0-5 cards, use empty mask for preflop)
 * @param[in] dead_cards Dead/removed cards to exclude from calculation
 * @param[in] opts Equity calculation options (NULL for defaults: 200k iterations)
 * @param[out] result Output result structure with equity for both players
 *
 * @return PE_STATUS_OK on success
 * @return PE_STATUS_INVALID_ARG if ranges are NULL or invalid
 * @return PE_STATUS_NOT_IMPLEMENTED for unsupported game variants
 *
 * @see pe_equity_multiway() for 3+ player scenarios
 * @see pe_equity_preflop() for preflop-only calculations
 */
pe_status_t pe_equity_range_vs_range(
    EvalContext *ctx,
    enum_game_t game_variant,
    const pe_range_t *r1,
    const pe_range_t *r2,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
);

/**
 * @brief Calculate equity for multiway scenarios (2-10 players)
 *
 * Computes equity for multiple player ranges simultaneously.
 * Supports all streets (preflop through river) with automatic method selection.
 *
 * @param[in] ctx Evaluation context (can be NULL for default)
 * @param[in] game_variant Game variant (game_holdem, game_omaha, game_omaha8, etc.)
 * @param[in] ranges Array of pointers to player ranges (pe_range_t*)
 * @param[in] num_players Number of players (2-10)
 * @param[in] board Current board cards (0-5 cards)
 * @param[in] dead_cards Dead/removed cards
 * @param[in] opts Equity calculation options (NULL for defaults)
 * @param[out] result Output results for all players
 *
 * @return PE_STATUS_OK on success
 * @return PE_STATUS_INVALID_ARG if num_players < 2 or > 10
 *
 * @note For 5-card boards with valid context, uses exact enumeration.
 *       Otherwise falls back to Monte Carlo sampling.
 *
 * @code{.c}
 * const pe_range_t *ranges[] = {r1, r2, r3};
 * pe_equity_result_multi_t result;
 * pe_equity_multiway(ctx, game_holdem, ranges, 3, board, dead, NULL, &result);
 * @endcode
 */
pe_status_t pe_equity_multiway(
    EvalContext *ctx,
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
);

/**
 * @brief Calculate preflop equity for community-card games
 *
 * Specialized function for preflop equity calculation. Always uses Monte Carlo
 * sampling as exact enumeration is computationally infeasible for preflop.
 *
 * @param[in] game_variant Game variant (game_holdem, game_omaha, game_holdem8, etc.)
 * @param[in] ranges Array of pointers to player ranges
 * @param[in] num_players Number of players (2-10)
 * @param[in] opts Equity options (iterations field controls sample count, default: 200000)
 * @param[out] result Output results for all players
 *
 * @return PE_STATUS_OK on success
 * @return PE_STATUS_INVALID_ARG if ranges are NULL or num_players invalid
 *
 * @note This function always sets result->exact = 0 (Monte Carlo).
 *
 * @code{.c}
 * pe_equity_opts_t opts = {.is_monte_carlo = 1, .iterations = 50000};
 * pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
 * printf("Equity: %.1f%% vs %.1f%%\n",
 *        result.results[0].equity * 100,
 *        result.results[1].equity * 100);
 * @endcode
 */
pe_status_t pe_equity_preflop(
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
);

/**
 * @brief Legacy wrapper for equity calculation
 *
 * Use pe_equity_multiway() instead for new code.

 * This function forwards to pe_equity_multiway() with a NULL context.
 * Provided for backward compatibility.
 *
 * @param[in] game_variant Game variant
 * @param[in] ranges Array of player ranges
 * @param[in] num_players Number of players
 * @param[in] board Current board cards
 * @param[in] dead_cards Dead cards
 * @param[in] opts Calculation options
 * @param[out] result Output results
 *
 * @return PE_STATUS_OK on success
 */
pe_status_t pe_equity_calculate(
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
);

#ifdef __cplusplus
}
#endif

#endif /* __PE_EQUITY_PUBLIC_H__ */
