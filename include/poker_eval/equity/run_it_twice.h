/*
 * run_it_twice.h - Multiple runout support for variance reduction
 *
 * Implements "Run It Twice" (RIT) and "Run It N Times" for Hold'em and Omaha:
 * - Generate N independent board runouts from same flop/turn state
 * - Calculate aggregate equity/EV across all runouts
 * - Variance reduction analysis
 *
 * Common use cases:
 * - RIT-2: Standard in high-stakes cash games
 * - RIT-3/4: Tournament bubble situations
 */

#ifndef POKER_EVAL_EQUITY_RUN_IT_TWICE_H
#define POKER_EVAL_EQUITY_RUN_IT_TWICE_H

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/handval_low.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum supported players and runouts */
#define RIT_MAX_PLAYERS 10
#define RIT_MAX_RUNOUTS 8

/* Game types - Hold'em and Omaha variants only */
typedef enum {
    RIT_GAME_HOLDEM = 0,
    RIT_GAME_OMAHA,
    RIT_GAME_OMAHA8,      /* Omaha Hi/Lo 8-or-better */
} rit_game_type_t;

/* Runout stage - which cards to deal on common board */
typedef enum {
    RIT_STAGE_FLOP = 0,         /* Deal flop (3 cards) */
    RIT_STAGE_TURN,             /* Deal turn (1 card) - flop already on board */
    RIT_STAGE_RIVER,            /* Deal river (1 card) - flop+turn on board */
    RIT_STAGE_TURN_RIVER,       /* Deal turn+river (2 cards) - flop on board */
} rit_stage_t;

/* Configuration for RIT calculation */
typedef struct {
    rit_game_type_t game_type;
    rit_stage_t stage;
    int num_runouts;              /* 2-8 runouts */

    /* Random seed control */
    uint64_t seed;                /* 0 = use system time */

    /* Performance options */
    bool enable_simd;             /* Use SIMD for evaluation if available */
    int num_threads;              /* 0 = auto-detect, 1 = single-threaded */

    /* Variance analysis */
    bool calc_variance;           /* Calculate variance reduction metrics */
    bool track_individual;        /* Track each runout separately */
} rit_config_t;

/* Single runout result */
typedef struct {
    StdDeck_CardMask board;       /* Complete 5-card board for this runout */
    HandVal player_hands[RIT_MAX_PLAYERS];  /* Evaluated hands */
    LowHandVal player_low_hands[RIT_MAX_PLAYERS]; /* Low hand values (Omaha8) */
    double equities[RIT_MAX_PLAYERS];       /* 0.0-1.0 for this runout */
    double low_equities[RIT_MAX_PLAYERS];   /* Portion of low pot (Omaha8) */
    int winner_mask;              /* Bitmask of winning players */
    int num_winners;              /* Number of winners (split pot if >1) */
    int low_winner_mask;          /* Low winners (Omaha8) */
    int low_num_winners;          /* Count of low winners */
    bool low_available;           /* True if a qualifying low was found */
} rit_runout_result_t;

/* Aggregate results across all runouts */
typedef struct {
    /* Input parameters */
    int num_players;
    int num_runouts;
    rit_game_type_t game_type;

    /* Individual runout data (if track_individual enabled) */
    rit_runout_result_t runouts[RIT_MAX_RUNOUTS];

    /* Aggregate equity (averaged across runouts) */
    double avg_equity[RIT_MAX_PLAYERS];      /* Mean equity per player */
    double avg_low_equity[RIT_MAX_PLAYERS];  /* Mean low equity (Omaha8) */

    /* Win/tie statistics (across all runouts) */
    int total_wins[RIT_MAX_PLAYERS];         /* Number of runouts won outright */
    int total_ties[RIT_MAX_PLAYERS];         /* Number of runouts tied */
    int total_losses[RIT_MAX_PLAYERS];       /* Number of runouts lost */
    int total_low_wins[RIT_MAX_PLAYERS];     /* Low-half wins (Omaha8) */
    int total_low_ties[RIT_MAX_PLAYERS];     /* Low-half ties */
    int total_low_losses[RIT_MAX_PLAYERS];   /* Low-half losses */

    /* Variance analysis (if calc_variance enabled) */
    double equity_variance[RIT_MAX_PLAYERS]; /* Variance across runouts */
    double equity_stddev[RIT_MAX_PLAYERS];   /* Standard deviation */
    double low_equity_variance[RIT_MAX_PLAYERS]; /* Low variance (Omaha8) */
    double low_equity_stddev[RIT_MAX_PLAYERS];   /* Low stddev */
    double variance_reduction;               /* Reduction factor vs single runout */

    /* Performance metrics */
    double computation_time_ms;
} rit_result_t;

/* Default configuration */
rit_config_t rit_config_default(void);

/* Preset configurations */
rit_config_t rit_config_holdem_rit2(void);      /* Standard RIT-2 Hold'em */
rit_config_t rit_config_omaha_rit2(void);       /* Standard RIT-2 Omaha */

/* Configuration validation */
bool rit_config_validate(const rit_config_t *config);

/* Core API - Calculate equity with multiple runouts */

/**
 * Calculate Run It Twice (or N times) equity
 *
 * @param holes Array of player hole cards (size: num_players)
 *              Hold'em: 2 cards per player
 *              Omaha: 4-6 cards per player
 * @param num_players Number of active players (2-10)
 * @param board Cards already on board (0-4 cards depending on stage)
 * @param dead_cards Additional dead cards to exclude from deck
 * @param config RIT configuration
 * @param result Output results structure
 * @return 0 on success, error code on failure
 */
int rit_calculate_equity(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const rit_config_t *config,
    rit_result_t *result
);

/**
 * Calculate RIT equity for Hold'em (convenience wrapper)
 *
 * @param holes Array of 2-card hole hands
 * @param num_players Number of players (2-10)
 * @param board Current board (0-4 cards: empty/flop/flop+turn)
 * @param num_runouts Number of runouts (2-8, typically 2)
 * @param result Output results
 * @return 0 on success, error code on failure
 */
int rit_holdem_equity(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    int num_runouts,
    rit_result_t *result
);

/**
 * Calculate RIT equity for Omaha (convenience wrapper)
 *
 * @param holes Array of 4-6 card hole hands
 * @param num_players Number of players (2-10)
 * @param board Current board (0-4 cards: empty/flop/flop+turn)
 * @param num_runouts Number of runouts (2-8, typically 2)
 * @param result Output results
 * @return 0 on success, error code on failure
 */
int rit_omaha_equity(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    int num_runouts,
    rit_result_t *result
);

/* Runout generation (lower-level API) */

/**
 * Generate independent board runouts from current state
 *
 * @param base_board Cards already on board (e.g., flop)
 * @param dead_cards All hole cards + any other dead cards
 * @param cards_to_deal Number of cards per runout (1 for turn/river, 2 for turn+river, 3 for flop)
 * @param num_runouts Number of runouts to generate (2-8)
 * @param seed Random seed (0 = system time)
 * @param boards Output array of complete 5-card boards (size: num_runouts)
 * @return 0 on success, error code on failure
 */
int rit_generate_runouts(
    StdDeck_CardMask base_board,
    StdDeck_CardMask dead_cards,
    int cards_to_deal,
    int num_runouts,
    uint64_t seed,
    StdDeck_CardMask boards[]
);

/**
 * Evaluate single runout for all players
 *
 * @param holes Player hole cards (size: num_players)
 * @param num_players Number of players
 * @param board Complete 5-card board
 * @param game_type Game type (HOLDEM, OMAHA, OMAHA8)
 * @param result Output runout result
 * @return 0 on success, error code on failure
 */
int rit_evaluate_runout(
    const StdDeck_CardMask holes[],
    int num_players,
    StdDeck_CardMask board,
    rit_game_type_t game_type,
    rit_runout_result_t *result
);

/* Variance analysis utilities */

/**
 * Calculate theoretical variance reduction for N runouts
 * Theory: variance reduces by factor of N (e.g., RIT-2 halves variance)
 *
 * @param num_runouts Number of runouts (2-8)
 * @return Variance reduction factor (e.g., 2.0 for RIT-2)
 */
double rit_theoretical_variance_reduction(int num_runouts);

/* Output and debugging */

/**
 * Print RIT results in human-readable format
 *
 * @param result RIT result
 * @param verbose Include individual runout details
 */
void rit_print_result(const rit_result_t *result, bool verbose);

/**
 * Export RIT results to CSV
 *
 * @param result RIT result
 * @param filename Output CSV filename
 * @return 0 on success, error code on failure
 */
int rit_export_csv(const rit_result_t *result, const char *filename);

/* Error codes */
#define RIT_SUCCESS               0
#define RIT_ERROR_INVALID_CONFIG  -1
#define RIT_ERROR_TOO_MANY_PLAYERS -2
#define RIT_ERROR_INVALID_STAGE   -3
#define RIT_ERROR_DECK_EXHAUSTED  -4
#define RIT_ERROR_EVAL_FAILED     -5
#define RIT_ERROR_IO_ERROR        -6

/* Error message retrieval */
const char* rit_error_string(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_EQUITY_RUN_IT_TWICE_H */
