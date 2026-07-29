/*
 * tournament.h - Tournament structure management and economic calculations
 *
 * Provides:
 * - Blind/ante level scheduling and progression
 * - Tournament pressure calculations
 * - Bubble detection and ICM integration
 * - Time-based level transitions
 */

#ifndef POKER_EVAL_TOURNAMENT_H
#define POKER_EVAL_TOURNAMENT_H

#include <stdint.h>
#include <stdbool.h>
#include <poker_eval/utils/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum supported levels and players */
#define TOURNAMENT_MAX_LEVELS 64
#define TOURNAMENT_MAX_PLAYERS 10000
#define TOURNAMENT_MAX_PAYOUTS 1000

/* Tournament types */
typedef enum {
    TOURNAMENT_TYPE_FREEZEOUT = 0,    /* Standard freezeout */
    TOURNAMENT_TYPE_REBUY,            /* Rebuy period allowed */
    TOURNAMENT_TYPE_ADDON,            /* Add-on at break */
    TOURNAMENT_TYPE_SHOOTOUT,         /* Table-by-table elimination */
    TOURNAMENT_TYPE_SNG,              /* Sit-and-go */
    TOURNAMENT_TYPE_SATELLITE,        /* Satellite qualifier */
    TOURNAMENT_TYPE_BOUNTY,           /* Knockout bounty */
    TOURNAMENT_TYPE_PROGRESSIVE_KO    /* Progressive knockout */
} tournament_type_t;

/* Blind level definition */
typedef struct {
    int64_t small_blind;
    int64_t big_blind;
    int64_t ante;
    int64_t big_blind_ante;           /* BB posts ante for table (modern format) */
    int duration_seconds;             /* Level duration (0 = untimed) */
    bool is_break;                    /* True if this is a break period */
} tournament_level_t;

/* Tournament blind structure */
typedef struct {
    tournament_level_t levels[TOURNAMENT_MAX_LEVELS];
    int num_levels;
    int current_level;
    int64_t time_remaining_ms;        /* Time remaining in current level (ms) */
    int64_t level_start_time_ms;      /* Timestamp when level started */
    bool is_paused;
} tournament_structure_t;

/* Payout structure */
typedef struct {
    double percentages[TOURNAMENT_MAX_PAYOUTS];  /* Payout as % of prize pool */
    int64_t amounts[TOURNAMENT_MAX_PAYOUTS];     /* Fixed amounts (if not %) */
    int num_payouts;
    bool use_percentages;             /* True = use %, false = fixed amounts */
} tournament_payout_t;

/* Tournament state */
typedef struct {
    /* Configuration */
    tournament_type_t type;
    tournament_structure_t structure;
    tournament_payout_t payouts;
    int64_t buy_in;
    int64_t rebuy_cost;
    int64_t addon_cost;
    int64_t bounty_amount;
    int64_t starting_chips;
    
    /* Current state */
    int registered_players;
    int players_remaining;
    int64_t total_chips;
    int64_t prize_pool;
    int64_t total_rebuys;
    int64_t total_addons;
    
    /* Position tracking */
    int itm_position;                 /* First ITM position */
    int bubble_position;              /* Bubble position (ITM + 1) */
    bool on_bubble;                   /* Currently on bubble */
    bool in_the_money;                /* Players are ITM */
    
    /* Timing */
    int64_t tournament_start_time_ms;
    int64_t current_time_ms;
    bool is_running;
} tournament_state_t;

/* Player tournament info */
typedef struct {
    int player_id;
    int64_t chips;
    int64_t bounties_won;             /* For bounty tournaments */
    double icm_equity;                /* Current ICM equity */
    int current_position;             /* Current ranking by chips */
    bool is_eliminated;
} tournament_player_t;

/* Error codes */
typedef enum {
    TOURNAMENT_OK = 0,
    TOURNAMENT_ERROR_INVALID_PARAMS,
    TOURNAMENT_ERROR_INVALID_LEVEL,
    TOURNAMENT_ERROR_INVALID_STATE,
    TOURNAMENT_ERROR_NOT_RUNNING,
    TOURNAMENT_ERROR_ALREADY_RUNNING,
    TOURNAMENT_ERROR_NO_PLAYERS,
    TOURNAMENT_ERROR_CALCULATION_FAILED
} tournament_error_t;

/* ============================================================================
 * STRUCTURE CREATION AND MANAGEMENT
 * ============================================================================ */

/**
 * Initialize a tournament structure with default values
 */
POKEREVAL_EXPORT void tournament_structure_init(tournament_structure_t *structure);

/**
 * Add a blind level to the structure
 * @param structure The structure to modify
 * @param sb Small blind amount
 * @param bb Big blind amount  
 * @param ante Ante amount (0 for no ante)
 * @param bb_ante Big blind ante amount (0 for traditional ante)
 * @param duration_seconds Level duration (0 for untimed)
 * @return Level index or -1 on error
 */
POKEREVAL_EXPORT int tournament_structure_add_level(
    tournament_structure_t *structure,
    int64_t sb, int64_t bb, int64_t ante, int64_t bb_ante,
    int duration_seconds);

/**
 * Add a break to the structure
 */
POKEREVAL_EXPORT int tournament_structure_add_break(
    tournament_structure_t *structure,
    int duration_seconds);

/**
 * Create standard tournament structures
 */
POKEREVAL_EXPORT void tournament_structure_create_turbo(
    tournament_structure_t *structure,
    int64_t starting_sb);

POKEREVAL_EXPORT void tournament_structure_create_regular(
    tournament_structure_t *structure,
    int64_t starting_sb);

POKEREVAL_EXPORT void tournament_structure_create_deep_stack(
    tournament_structure_t *structure,
    int64_t starting_sb);

/* ============================================================================
 * TOURNAMENT STATE MANAGEMENT
 * ============================================================================ */

/**
 * Initialize tournament state
 */
POKEREVAL_EXPORT void tournament_state_init(tournament_state_t *state);

/**
 * Configure tournament parameters
 */
POKEREVAL_EXPORT tournament_error_t tournament_configure(
    tournament_state_t *state,
    tournament_type_t type,
    int64_t buy_in,
    int64_t starting_chips,
    int num_players);

/**
 * Set payout structure (percentage-based)
 */
POKEREVAL_EXPORT tournament_error_t tournament_set_payouts_percentage(
    tournament_state_t *state,
    const double *percentages,
    int num_payouts);

/**
 * Set standard payout structure by player count
 */
POKEREVAL_EXPORT tournament_error_t tournament_set_standard_payouts(
    tournament_state_t *state,
    int num_players);

/**
 * Start the tournament
 */
POKEREVAL_EXPORT tournament_error_t tournament_start(tournament_state_t *state);

/**
 * Pause/resume the tournament
 */
POKEREVAL_EXPORT tournament_error_t tournament_pause(tournament_state_t *state);
POKEREVAL_EXPORT tournament_error_t tournament_resume(tournament_state_t *state);

/**
 * Update tournament time (call periodically)
 * @param state Tournament state
 * @param current_time_ms Current timestamp in milliseconds
 * @return true if level changed
 */
POKEREVAL_EXPORT bool tournament_update_time(
    tournament_state_t *state,
    int64_t current_time_ms);

/**
 * Manually advance to next level
 */
POKEREVAL_EXPORT tournament_error_t tournament_advance_level(tournament_state_t *state);

/**
 * Eliminate a player
 */
POKEREVAL_EXPORT tournament_error_t tournament_eliminate_player(
    tournament_state_t *state,
    int player_id,
    int64_t chips_redistributed);

/* ============================================================================
 * BLIND/ANTE QUERIES
 * ============================================================================ */

/**
 * Get current blind level info
 */
POKEREVAL_EXPORT const tournament_level_t* tournament_get_current_level(
    const tournament_state_t *state);

/**
 * Get blinds for a specific level
 */
POKEREVAL_EXPORT const tournament_level_t* tournament_get_level(
    const tournament_state_t *state,
    int level_index);

/**
 * Get current small blind
 */
POKEREVAL_EXPORT int64_t tournament_get_small_blind(const tournament_state_t *state);

/**
 * Get current big blind
 */
POKEREVAL_EXPORT int64_t tournament_get_big_blind(const tournament_state_t *state);

/**
 * Get current ante (traditional or BB ante)
 */
POKEREVAL_EXPORT int64_t tournament_get_ante(const tournament_state_t *state);

/**
 * Check if using big blind ante format
 */
POKEREVAL_EXPORT bool tournament_uses_bb_ante(const tournament_state_t *state);

/**
 * Get time remaining in current level (seconds)
 */
POKEREVAL_EXPORT int tournament_get_time_remaining(const tournament_state_t *state);

/**
 * Get next level info (for display)
 */
POKEREVAL_EXPORT const tournament_level_t* tournament_get_next_level(
    const tournament_state_t *state);

/* ============================================================================
 * TOURNAMENT PRESSURE & ICM CALCULATIONS
 * ============================================================================ */

/**
 * Calculate tournament pressure factor for a player
 * Based on: stack size vs average, blind pressure, bubble proximity
 * @return Pressure factor (0.0 = no pressure, 1.0+ = high pressure)
 */
POKEREVAL_EXPORT double tournament_calculate_pressure(
    const tournament_state_t *state,
    int64_t player_chips);

/**
 * Calculate M-ratio (Harrington's M)
 * M = stack / (SB + BB + antes)
 */
POKEREVAL_EXPORT double tournament_calculate_m_ratio(
    const tournament_state_t *state,
    int64_t player_chips,
    int players_at_table);

/**
 * Calculate Q-ratio (stack relative to average)
 * Q = stack / average_stack
 */
POKEREVAL_EXPORT double tournament_calculate_q_ratio(
    const tournament_state_t *state,
    int64_t player_chips);

/**
 * Calculate effective M (M adjusted for table size)
 * Effective M = M * (players_at_table / 10)
 */
POKEREVAL_EXPORT double tournament_calculate_effective_m(
    const tournament_state_t *state,
    int64_t player_chips,
    int players_at_table);

/**
 * Check if tournament is on the bubble
 */
POKEREVAL_EXPORT bool tournament_is_on_bubble(const tournament_state_t *state);

/**
 * Calculate bubble factor for ICM adjustments
 * Higher factor = more conservative play warranted
 */
POKEREVAL_EXPORT double tournament_calculate_bubble_factor(
    const tournament_state_t *state,
    int64_t player_chips);

/**
 * Get payout for a finishing position
 */
POKEREVAL_EXPORT int64_t tournament_get_payout(
    const tournament_state_t *state,
    int finish_position);

/**
 * Calculate prize pool equity for a chip stack
 * Simplified ICM approximation
 */
POKEREVAL_EXPORT double tournament_calculate_equity(
    const tournament_state_t *state,
    int64_t player_chips);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Get average stack
 */
POKEREVAL_EXPORT int64_t tournament_get_average_stack(const tournament_state_t *state);

/**
 * Get chip leader stack estimate
 */
POKEREVAL_EXPORT int64_t tournament_get_chip_leader_estimate(const tournament_state_t *state);

/**
 * Calculate total pot cost per orbit
 * (SB + BB + all antes)
 */
POKEREVAL_EXPORT int64_t tournament_get_orbit_cost(
    const tournament_state_t *state,
    int players_at_table);

/**
 * Get error description
 */
POKEREVAL_EXPORT const char* tournament_error_string(tournament_error_t error);

/**
 * Print tournament state (debug)
 */
POKEREVAL_EXPORT void tournament_print_state(const tournament_state_t *state);

/**
 * Print blind structure (debug)
 */
POKEREVAL_EXPORT void tournament_print_structure(const tournament_structure_t *structure);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_TOURNAMENT_H */
