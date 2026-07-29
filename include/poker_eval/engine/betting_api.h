/*
 * betting_api.h - Betting Engine Public API
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Public API for poker betting engine functionality.
 * Use: #include <poker_eval/betting/betting_api.h>
 */

#ifndef POKER_EVAL_BETTING_BETTING_API_H
#define POKER_EVAL_BETTING_BETTING_API_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/engine/engine_api.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Forward Declarations ===== */

/* Opaque betting engine handle */
typedef struct pe_betting_engine pe_betting_engine_t;

/* Forward declarations for betting structures */
#ifndef GAME_ENGINE_H
typedef struct BettingLimits BettingLimits;

/* ===== Betting Structure Types ===== */

typedef enum {
    BETTING_STRUCTURE_NO_LIMIT = 0,
    BETTING_STRUCTURE_POT_LIMIT = 1,
    BETTING_STRUCTURE_FIXED_LIMIT = 2,
    BETTING_STRUCTURE_SPREAD_LIMIT = 3
} betting_structure_t;

/* Compatibility aliases for legacy code */
#define BETTING_NO_LIMIT BETTING_STRUCTURE_NO_LIMIT
#define BETTING_POT_LIMIT BETTING_STRUCTURE_POT_LIMIT
#define BETTING_LIMIT BETTING_STRUCTURE_FIXED_LIMIT
#define BETTING_SPREAD_LIMIT BETTING_STRUCTURE_SPREAD_LIMIT

/* Player action types - compatible with FPDB-3 action codes */
typedef enum {
    ACTION_CALL = 6,    /* FPDB-3: "calls" = 6 */
    ACTION_RAISE = 7,   /* FPDB-3: "raises" = 7 */
    ACTION_BET = 8,     /* FPDB-3: "bets" = 8 */
    ACTION_FOLD = 10,   /* FPDB-3: "folds" = 10 */
    ACTION_CHECK = 11,  /* FPDB-3: "checks" = 11 */
    ACTION_ALL_IN = 18, /* Custom: not in FPDB-3, using safe value */
    ACTION_COUNT = 19
} player_action_type_t;

/* Betting action with detailed information */
typedef struct {
    player_action_type_t action;
    int64_t amount;
    int64_t min_amount;
    int64_t max_amount;
    bool is_valid;
    const char* description;
} BettingAction;

#endif

/* ===== Betting Engine Management ===== */

/* Create betting engine with structure and limits */
PE_API pe_status_t pe_betting_engine_create(betting_structure_t structure,
                                           const BettingLimits* limits,
                                           pe_betting_engine_t** out_engine);

/* Destroy betting engine and free resources */
PE_API void pe_betting_engine_destroy(pe_betting_engine_t* engine);

/* ===== Betting Action Validation ===== */

/* Get valid actions for a player in current game state */
PE_API pe_status_t pe_betting_engine_get_valid_actions(pe_betting_engine_t* engine,
                                                      const pe_game_state_t* state,
                                                      int player_id,
                                                      const BettingAction** out_actions,
                                                      int* out_count);

/* Validate if a specific action is allowed */
PE_API pe_status_t pe_betting_engine_validate_action(pe_betting_engine_t* engine,
                                                    const pe_game_state_t* state,
                                                    int player_id,
                                                    const BettingAction* action,
                                                    bool* out_valid);

/* Get minimum bet amount for a player */
PE_API pe_status_t pe_betting_engine_get_min_bet(pe_betting_engine_t* engine,
                                                const pe_game_state_t* state,
                                                int player_id,
                                                int64_t* out_min_bet);

/* Get maximum bet amount for a player */
PE_API pe_status_t pe_betting_engine_get_max_bet(pe_betting_engine_t* engine,
                                                const pe_game_state_t* state,
                                                int player_id,
                                                int64_t* out_max_bet);

/* Get call amount for a player */
PE_API pe_status_t pe_betting_engine_get_call_amount(pe_betting_engine_t* engine,
                                                    const pe_game_state_t* state,
                                                    int player_id,
                                                    int64_t* out_call_amount);

/* ===== Betting Structure Queries ===== */

/* Get current betting structure */
PE_API pe_status_t pe_betting_engine_get_structure(pe_betting_engine_t* engine,
                                                   betting_structure_t* out_structure);

/* Get betting limits */
PE_API pe_status_t pe_betting_engine_get_limits(pe_betting_engine_t* engine,
                                               const BettingLimits** out_limits);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_BETTING_BETTING_API_H */
