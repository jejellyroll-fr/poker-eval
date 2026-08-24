/*
 * poker_eval_api.h - Stable C API for poker-eval library
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * This header provides a stable ABI-compatible C API for external consumers.
 * All internal structures are hidden behind opaque handles to ensure
 * forward compatibility.
 *
 * Usage:
 *   pe_handle_t handle = pe_init(NULL);
 *   pe_equity_result_t result;
 *   pe_calculate_equity_holdem(handle, "AhAs", "KhKs", "Td9d8c", &result);
 *   printf("Equity: %.2f%%\n", result.equity * 100.0);
 *   pe_free(handle);
 */

#ifndef POKER_EVAL_API_H
#define POKER_EVAL_API_H

#include <stdint.h>
#include <stddef.h>

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_traversal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== ABI Version ===== */
#define PE_API_VERSION_MAJOR 1
#define PE_API_VERSION_MINOR 0
#define PE_API_VERSION_PATCH 0

/* ===== Opaque Handles ===== */

/** Main poker-eval handle */
typedef struct pe_context_t* pe_handle_t;

/** CFR solver handle */
typedef struct pe_cfr_solver_t* pe_cfr_handle_t;

/** ICM calculator handle */
typedef struct pe_icm_calc_t* pe_icm_handle_t;

/* ===== Error Codes ===== */
typedef enum {
    PE_OK = 0,
    PE_ERROR_INVALID_HANDLE = -1,
    PE_ERROR_INVALID_ARGUMENT = -2,
    PE_ERROR_PARSE_FAILED = -3,
    PE_ERROR_OUT_OF_MEMORY = -4,
    PE_ERROR_NOT_SUPPORTED = -5,
    PE_ERROR_IO_FAILED = -6,
    PE_ERROR_UNKNOWN = -99
} pe_error_t;

/* ===== Game Types ===== */
typedef enum {
    PE_GAME_HOLDEM = 0,
    PE_GAME_HOLDEM8 = 1,      /* Holdem Hi/Lo */
    PE_GAME_OMAHA = 2,
    PE_GAME_OMAHA8 = 3,       /* Omaha Hi/Lo */
    PE_GAME_OMAHA5 = 4,
    PE_GAME_OMAHA6 = 5,
    PE_GAME_STUD7 = 6,
    PE_GAME_STUD7_8 = 7,      /* 7-Stud Hi/Lo */
    PE_GAME_RAZZ = 8,
    PE_GAME_SHORTDECK = 9,
    PE_GAME_LOWBALL27 = 10,
    PE_GAME_LOWBALL_A5 = 11
} pe_game_type_t;

/* ===== Configuration ===== */
typedef struct {
    int use_monte_carlo;       /* 0 = exhaustive, 1 = Monte Carlo */
    int monte_carlo_samples;   /* Number of MC samples (default: 10000) */
    int num_threads;           /* Number of threads (0 = auto) */
    int verbose;               /* Enable verbose output */
} pe_config_t;

/* ===== Result Structures ===== */

/** Equity calculation result */
typedef struct {
    double equity;             /* Equity [0.0, 1.0] */
    double win_pct;            /* Win percentage */
    double tie_pct;            /* Tie percentage */
    double lose_pct;           /* Lose percentage */
    uint64_t samples;          /* Number of samples evaluated */
} pe_equity_result_t;

/** Hand evaluation result */
typedef struct {
    uint32_t hand_value;       /* Numeric hand value (higher = better) */
    int hand_type;             /* Hand type (pair, flush, etc.) */
    char hand_name[32];        /* Human-readable hand name */
    uint8_t cards[5];          /* Best 5-card combination */
} pe_hand_result_t;

/** ICM calculation result (per player) */
typedef struct {
    double icm_equity;         /* ICM equity in dollars */
    double chip_ev;            /* Chip EV */
} pe_icm_player_result_t;

/** Multi-player equity result */
typedef struct {
    int num_players;
    pe_equity_result_t* player_results;  /* Array of per-player results */
    uint64_t total_samples;
} pe_multiway_result_t;

/* ===== Library Initialization ===== */

/**
 * Get API version
 *
 * @param major  Output major version
 * @param minor  Output minor version
 * @param patch  Output patch version
 */
void pe_get_version(int* major, int* minor, int* patch);

/**
 * Initialize poker-eval library
 *
 * @param config  Optional configuration (NULL for defaults)
 * @return Handle or NULL on failure
 */
pe_handle_t pe_init(const pe_config_t* config);

/**
 * Free poker-eval library resources
 *
 * @param handle  Handle to free
 */
void pe_free(pe_handle_t handle);

/**
 * Get default configuration
 *
 * @param config  Output configuration
 */
void pe_get_default_config(pe_config_t* config);

/**
 * Get last error message
 *
 * @param handle  Library handle
 * @return Error message string (static, do not free)
 */
const char* pe_get_error(pe_handle_t handle);

/* ===== Card Parsing ===== */

/**
 * Parse card string to card number
 *
 * @param card_str  Card string (e.g., "As", "Kh", "Td")
 * @return Card number [0-51] or -1 on error
 */
int pe_parse_card(const char* card_str);

/**
 * Convert card number to string
 *
 * @param card    Card number [0-51]
 * @param buffer  Output buffer (at least 3 bytes)
 * @param size    Buffer size
 * @return PE_OK on success
 */
pe_error_t pe_card_to_string(int card, char* buffer, size_t size);

/**
 * Parse board string to cards array
 *
 * @param board_str  Board string (e.g., "AhKhQh" or "Ah Kh Qh")
 * @param cards      Output card array
 * @param max_cards  Maximum cards to parse
 * @return Number of cards parsed, or negative on error
 */
int pe_parse_board(const char* board_str, uint8_t* cards, int max_cards);

/* ===== Hand Evaluation ===== */

/**
 * Evaluate a 5-7 card hand (high)
 *
 * @param handle    Library handle
 * @param cards     Card numbers array
 * @param num_cards Number of cards (5-7)
 * @param result    Output result
 * @return PE_OK on success
 */
pe_error_t pe_evaluate_hand(pe_handle_t handle,
                            const uint8_t* cards,
                            int num_cards,
                            pe_hand_result_t* result);

/**
 * Evaluate Hold'em hand (2 hole + 5 board)
 *
 * @param handle     Library handle
 * @param hole_cards Hole cards string (e.g., "AhAs")
 * @param board      Board cards string (e.g., "KhQhJh")
 * @param result     Output result
 * @return PE_OK on success
 */
pe_error_t pe_evaluate_holdem(pe_handle_t handle,
                              const char* hole_cards,
                              const char* board,
                              pe_hand_result_t* result);

/* ===== Equity Calculation ===== */

/**
 * Calculate Hold'em equity (2 players)
 *
 * @param handle   Library handle
 * @param hand1    Hand 1 string (e.g., "AhAs" or "AA")
 * @param hand2    Hand 2 string
 * @param board    Board cards string (may be empty)
 * @param result   Output result (for hand1)
 * @return PE_OK on success
 */
pe_error_t pe_calculate_equity_holdem(pe_handle_t handle,
                                      const char* hand1,
                                      const char* hand2,
                                      const char* board,
                                      pe_equity_result_t* result);

/**
 * Calculate Omaha equity (2 players)
 *
 * @param handle   Library handle
 * @param hand1    Hand 1 string (e.g., "AhAsKhKs")
 * @param hand2    Hand 2 string
 * @param board    Board cards string
 * @param result   Output result (for hand1)
 * @return PE_OK on success
 */
pe_error_t pe_calculate_equity_omaha(pe_handle_t handle,
                                     const char* hand1,
                                     const char* hand2,
                                     const char* board,
                                     pe_equity_result_t* result);

/**
 * Calculate multi-way equity
 *
 * @param handle      Library handle
 * @param game        Game type
 * @param hands       Array of hand strings
 * @param num_players Number of players
 * @param board       Board cards string
 * @param result      Output result (must free with pe_free_multiway_result)
 * @return PE_OK on success
 */
pe_error_t pe_calculate_equity_multiway(pe_handle_t handle,
                                        pe_game_type_t game,
                                        const char** hands,
                                        int num_players,
                                        const char* board,
                                        pe_multiway_result_t* result);

/**
 * Free multi-way result
 *
 * @param result  Result to free
 */
void pe_free_multiway_result(pe_multiway_result_t* result);

/* ===== Range Equity ===== */

/**
 * Calculate range vs range equity
 *
 * @param handle    Library handle
 * @param game      Game type
 * @param range1    Range string (e.g., "AA,KK,QQ" or "AAxx")
 * @param range2    Range string
 * @param board     Board cards string
 * @param result    Output result (for range1)
 * @return PE_OK on success
 */
pe_error_t pe_calculate_range_equity(pe_handle_t handle,
                                     pe_game_type_t game,
                                     const char* range1,
                                     const char* range2,
                                     const char* board,
                                     pe_equity_result_t* result);

/* ===== CFR Solver ===== */

/**
 * Create CFR solver
 *
 * @param handle     Library handle
 * @param game       Game type
 * @param game_tree  Optional game tree configuration (NULL for default)
 * @return CFR handle or NULL on failure
 */
pe_cfr_handle_t pe_cfr_create(pe_handle_t handle,
                              pe_game_type_t game,
                              const char* game_tree);

/**
 * Free CFR solver
 *
 * @param cfr  CFR handle
 */
void pe_cfr_free(pe_cfr_handle_t cfr);

/**
 * Run CFR iterations
 *
 * @param cfr         CFR handle
 * @param iterations  Number of iterations
 * @return PE_OK on success
 */
pe_error_t pe_cfr_solve(pe_cfr_handle_t cfr, int iterations);

/**
 * Get strategy for an information set
 *
 * @param cfr         CFR handle
 * @param infoset_key Information set key (hash)
 * @param strategy    Output strategy array
 * @param max_actions Maximum actions
 * @return Number of actions, or negative on error
 */
int pe_cfr_get_strategy(pe_cfr_handle_t cfr,
                        uint64_t infoset_key,
                        double* strategy,
                        int max_actions);

/**
 * Save CFR solution to file
 *
 * @param cfr       CFR handle
 * @param filepath  Output file path
 * @return PE_OK on success
 */
pe_error_t pe_cfr_save(pe_cfr_handle_t cfr, const char* filepath);

/**
 * Load CFR solution from file
 *
 * @param cfr       CFR handle
 * @param filepath  Input file path
 * @return PE_OK on success
 */
pe_error_t pe_cfr_load(pe_cfr_handle_t cfr, const char* filepath);

/**
 * Get current exploitability
 *
 * @param cfr           CFR handle
 * @param exploitability Output exploitability
 * @return PE_OK on success
 */
pe_error_t pe_cfr_get_exploitability(pe_cfr_handle_t cfr, double* exploitability);

/* ===== Solver v3 façade ===== */

/**
 * Opaque handle for the architecture-v3 solver.
 *
 * The vector game and its callbacks are borrowed by the handle and must stay
 * alive until pe_solver_api_free(). Configuration is copied at creation.
 */
typedef struct pe_solver_api_t* pe_solver_api_handle_t;

/** Create a v3 solver backed by the supplied generic vector game. */
pe_solver_api_handle_t pe_solver_api_create(
    const pe_solver_config_t* config,
    const pe_vector_game_t* game);

/** Destroy a v3 solver façade. Safe on NULL. */
void pe_solver_api_free(pe_solver_api_handle_t solver);

/** Validate the configured plan without running it. */
pe_solver_status_t pe_solver_api_validate(
    pe_solver_api_handle_t solver,
    pe_diagnostics_t* diagnostics);

/** Run the configured solve until its configured stop condition. */
pe_solver_status_t pe_solver_api_run(pe_solver_api_handle_t solver);

/** Read lifecycle progress. */
pe_solver_status_t pe_solver_api_progress(
    pe_solver_api_handle_t solver,
    pe_progress_t* progress);

/** Read exploitability metrics after the target has been measured. */
pe_solver_status_t pe_solver_api_metrics(
    pe_solver_api_handle_t solver,
    pe_metrics_t* metrics);

/** Read an average strategy view after a completed solve. */
pe_solver_status_t pe_solver_api_strategy(
    pe_solver_api_handle_t solver,
    const pe_strategy_query_t* query,
    pe_strategy_view_t* view);

/* ===== ICM Calculator ===== */

/**
 * Create ICM calculator
 *
 * @param handle  Library handle
 * @return ICM handle or NULL on failure
 */
pe_icm_handle_t pe_icm_create(pe_handle_t handle);

/**
 * Free ICM calculator
 *
 * @param icm  ICM handle
 */
void pe_icm_free(pe_icm_handle_t icm);

/**
 * Calculate ICM equity
 *
 * @param icm         ICM handle
 * @param stacks      Array of chip stacks
 * @param payouts     Array of payouts
 * @param num_players Number of players
 * @param results     Output array of results (one per player)
 * @return PE_OK on success
 */
pe_error_t pe_icm_calculate(pe_icm_handle_t icm,
                            const double* stacks,
                            const double* payouts,
                            int num_players,
                            pe_icm_player_result_t* results);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_API_H */
