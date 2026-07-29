/*
 * ICM (Independent Chip Model) Calculator
 *
 * This module provides functionality to calculate ICM values for tournament
 * and sit-and-go scenarios. The ICM model calculates the monetary value
 * of a player's chip stack based on the probability of finishing in each
 * position and the corresponding payout structure.
 *
 * Copyright (C) 2024 Poker-Eval Project
 * Licensed under GPL v3 or later
 */

#ifndef ICM_CALCULATOR_H
#define ICM_CALCULATOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum number of players supported in ICM calculations */
#define ICM_MAX_PLAYERS 23

/* Maximum number of payout positions */
#define ICM_MAX_PAYOUTS 23

    /* Error codes for ICM calculations */
    typedef enum
    {
        ICM_SUCCESS = 0,
        ICM_ERROR_INVALID_PLAYERS = -1,
        ICM_ERROR_INVALID_STACKS = -2,
        ICM_ERROR_INVALID_PAYOUTS = -3,
        ICM_ERROR_MEMORY_ALLOCATION = -4,
        ICM_ERROR_CALCULATION_OVERFLOW = -5,
        ICM_ERROR_ZERO_TOTAL_CHIPS = -6
    } icm_error_t;

    /* Structure representing a player's tournament state */
    typedef struct
    {
        uint64_t chips; /* Current chip count */
        bool is_active; /* Whether player is still in tournament */
        char name[32];  /* Optional player name for display */
    } icm_player_t;

    /* Structure representing tournament payout structure */
    typedef struct
    {
        int num_paid_positions;          /* Number of positions that receive money */
        double payouts[ICM_MAX_PAYOUTS]; /* Payout percentages (sum should be 1.0) */
        double total_prize_pool;         /* Total prize pool amount */
    } icm_payout_structure_t;

    /* Structure containing ICM calculation results */
    typedef struct
    {
        int num_players;                                             /* Number of players in calculation */
        double equity[ICM_MAX_PLAYERS];                              /* ICM equity for each player */
        double finish_probability[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS]; /* Probability of finishing in each position */
        double total_chips;                                          /* Total chips in play */
        double calculation_time;                                     /* Time taken for calculation (seconds) */
        bool is_valid;                                               /* Whether the calculation completed successfully */
    } icm_result_t;

    /* Structure for ICM tournament configuration */
    typedef struct
    {
        icm_player_t players[ICM_MAX_PLAYERS];
        icm_payout_structure_t payout_structure;
        int num_active_players;
        bool use_approximation;     /* Use approximation for large player counts */
        int max_iterations;         /* Maximum iterations for approximation methods */
        double precision_threshold; /* Convergence threshold for approximations */
    } icm_tournament_t;

    /* Core ICM calculation functions */

    /**
     * Calculate ICM equity for all players in a tournament
     *
     * @param tournament Tournament configuration
     * @param result Output structure for results
     * @return ICM error code
     */
    icm_error_t icm_calculate_equity(const icm_tournament_t *tournament, icm_result_t *result);

    /**
     * Calculate ICM equity using exact combinatorial method
     * Best for smaller player counts (< 10 players)
     *
     * @param stacks Array of chip stacks
     * @param num_players Number of players
     * @param payouts Payout structure
     * @param result Output structure for results
     * @return ICM error code
     */
    icm_error_t icm_calculate_exact(const uint64_t *stacks, int num_players,
                                    const icm_payout_structure_t *payouts, icm_result_t *result);

    /**
     * Calculate ICM equity using Monte Carlo approximation
     * Better for larger player counts (10+ players)
     *
     * @param stacks Array of chip stacks
     * @param num_players Number of players
     * @param payouts Payout structure
     * @param iterations Number of Monte Carlo iterations
     * @param result Output structure for results
     * @return ICM error code
     */
    icm_error_t icm_calculate_monte_carlo(const uint64_t *stacks, int num_players,
                                          const icm_payout_structure_t *payouts,
                                          int iterations, icm_result_t *result);

    /**
     * Calculate ICM equity using Malmuth-Weitzman approximation
     * Fast approximation method for any player count
     *
     * @param stacks Array of chip stacks
     * @param num_players Number of players
     * @param payouts Payout structure
     * @param result Output structure for results
     * @return ICM error code
     */
    icm_error_t icm_calculate_approximation(const uint64_t *stacks, int num_players,
                                            const icm_payout_structure_t *payouts,
                                            icm_result_t *result);

    /* Utility functions */

    /**
     * Initialize a tournament structure with default values
     *
     * @param tournament Tournament structure to initialize
     */
    void icm_tournament_init(icm_tournament_t *tournament);

    /**
     * Add a player to the tournament
     *
     * @param tournament Tournament structure
     * @param chips Player's chip count
     * @param name Optional player name (can be NULL)
     * @return Player index or -1 if tournament is full
     */
    int icm_tournament_add_player(icm_tournament_t *tournament, uint64_t chips, const char *name);

    /**
     * Set up a standard tournament payout structure
     *
     * @param payout_structure Payout structure to configure
     * @param num_players Total number of players
     * @param num_paid Number of paid positions
     * @param total_prize_pool Total prize pool
     * @return ICM error code
     */
    icm_error_t icm_setup_standard_payouts(icm_payout_structure_t *payout_structure,
                                           int num_players, int num_paid, double total_prize_pool);

    /**
     * Set up sit-and-go payout structure (common formats)
     *
     * @param payout_structure Payout structure to configure
     * @param sng_type Type of SNG (6-max, 9-max, etc.)
     * @param total_prize_pool Total prize pool
     * @return ICM error code
     */
    icm_error_t icm_setup_sng_payouts(icm_payout_structure_t *payout_structure,
                                      int sng_type, double total_prize_pool);

    /**
     * Calculate the probability of a player finishing in a specific position
     *
     * @param stacks Array of chip stacks
     * @param num_players Number of players
     * @param player_index Index of the player
     * @param finish_position Position to calculate probability for (1 = first place)
     * @return Probability (0.0 to 1.0) or -1.0 on error
     */
    double icm_finish_probability(const uint64_t *stacks, int num_players,
                                  int player_index, int finish_position);

    /**
     * Calculate chip EV (expected value) for a specific decision
     * Useful for analyzing all-in situations
     *
     * @param current_stacks Current chip stacks
     * @param num_players Number of players
     * @param player_index Player making the decision
     * @param win_probability Probability of winning the hand
     * @param chips_at_risk Chips that would be lost if losing
     * @param chips_to_win Chips that would be won if winning
     * @param payouts Payout structure
     * @return Expected monetary value of the decision
     */
    double icm_calculate_chip_ev(const uint64_t *current_stacks, int num_players,
                                 int player_index, double win_probability,
                                 uint64_t chips_at_risk, uint64_t chips_to_win,
                                 const icm_payout_structure_t *payouts);

    /* Display and debugging functions */

    /**
     * Print ICM results in a formatted table
     *
     * @param result ICM calculation results
     * @param show_probabilities Whether to show finish probabilities
     */
    void icm_print_results(const icm_result_t *result, bool show_probabilities);

    /**
     * Print tournament state
     *
     * @param tournament Tournament structure
     */
    void icm_print_tournament(const icm_tournament_t *tournament);

    /**
     * Validate tournament configuration
     *
     * @param tournament Tournament structure to validate
     * @return ICM error code
     */
    icm_error_t icm_validate_tournament(const icm_tournament_t *tournament);

    /**
     * Get error message string for an ICM error code
     *
     * @param error ICM error code
     * @return Human-readable error message
     */
    const char *icm_error_string(icm_error_t error);

    /* Advanced features */

    /**
     * Calculate ICM with bubble factor adjustment
     * Adjusts ICM values near the bubble (when close to money positions)
     *
     * @param tournament Tournament configuration
     * @param bubble_factor Bubble adjustment factor (1.0 = no adjustment)
     * @param result Output structure for results
     * @return ICM error code
     */
    icm_error_t icm_calculate_with_bubble_factor(const icm_tournament_t *tournament,
                                                 double bubble_factor, icm_result_t *result);

    /**
     * Calculate future game tree ICM values
     * Useful for analyzing multiple future scenarios
     *
     * @param tournament Current tournament state
     * @param scenarios Array of possible future scenarios
     * @param num_scenarios Number of scenarios
     * @param scenario_probabilities Probability of each scenario
     * @param result Output structure for results
     * @return ICM error code
     */
    icm_error_t icm_calculate_future_scenarios(const icm_tournament_t *tournament,
                                               const icm_tournament_t *scenarios,
                                               int num_scenarios,
                                               const double *scenario_probabilities,
                                               icm_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ICM_CALCULATOR_H */
