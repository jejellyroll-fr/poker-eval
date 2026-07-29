/*
 * engine_api.h - Game Engine Public API
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Public API for poker game engine functionality.
 * Use: #include <poker_eval/engine/engine_api.h>
 */

#ifndef POKER_EVAL_ENGINE_ENGINE_API_H
#define POKER_EVAL_ENGINE_ENGINE_API_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/pokereval_export.h>
#include <poker_eval/core/status.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Forward Declarations ===== */

/* Opaque engine handle */
typedef struct pe_engine pe_engine_t;

/* Opaque game state handle */
typedef struct pe_game_state pe_game_state_t;

/* Forward declarations for game rules and structures - avoid redefinition */
#ifndef GAME_ENGINE_H
typedef struct GameRules GameRules;
typedef struct BlindStructure BlindStructure;
typedef struct GameState GameState;

/* Player action structure - exposed for examples */
typedef struct {
    int type;              /* player_action_type_t */
    int64_t amount;
    int player_id;
    int round;             /* betting_round_t */
    bool is_valid;
} PlayerAction;

/* Hand result structure - exposed for legacy examples */
typedef struct {
    int player_id;
    uint64_t best_hand;        /* mask_t equivalent */
    int hand_rank;
    int64_t winnings;
    bool is_winner;
    bool is_low_winner;
    char hand_description[128];
} HandResult;
#endif

typedef struct EvaluationResult EvaluationResult;
struct mixed_game;
struct mixed_game_report;

/* ===== Engine Management ===== */

/* Create engine with default rules (Texas Hold'em) */
PE_API pe_status_t pe_engine_create_default(pe_engine_t** out_engine);

/* Create engine with specific game rules */
PE_API pe_status_t pe_engine_create_with_rules(const GameRules* rules, pe_engine_t** out_engine);

/* Destroy engine and free resources */
PE_API void pe_engine_destroy(pe_engine_t* engine);

/* ===== Game State Management ===== */

/* Create new game state with rules and blind structure */
PE_API pe_status_t pe_game_state_create(const GameRules* rules, const BlindStructure* blinds,
                                        int num_players, pe_game_state_t** out_state);

/* Destroy game state and free resources */
PE_API void pe_game_state_destroy(pe_game_state_t* state);

/* Set player stack size */
PE_API pe_status_t pe_engine_set_player_stack(pe_game_state_t* state, int player_id, int64_t stack_size);

/* ===== Hand Management ===== */

/* Setup new hand with button position */
PE_API pe_status_t pe_engine_setup_hand(pe_engine_t* engine, pe_game_state_t* state, int button_position);

/* Apply player action to game state (struct version) */
PE_API pe_status_t pe_engine_apply_action_struct(pe_engine_t* engine, pe_game_state_t* state,
                                                 const PlayerAction* action);

/* Apply player action using simple parameters (convenience) */
PE_API pe_status_t pe_engine_apply_action(pe_engine_t* engine, pe_game_state_t* state,
                                          int action_type, int64_t amount, int player_id);

/* Complete hand and calculate results (evaluation result version) */
PE_API pe_status_t pe_engine_complete_hand_eval(pe_engine_t* engine, pe_game_state_t* state,
                                               EvaluationResult* out_result);

/* Complete hand and get HandResult array (legacy compatibility) */
PE_API pe_status_t pe_engine_complete_hand(pe_engine_t* engine, pe_game_state_t* state,
                                           HandResult* results, int max_results);

/* ===== Mixed Game Management ===== */
PE_API pe_status_t pe_engine_attach_mixed_game(pe_engine_t* engine, struct mixed_game* mixed);
PE_API pe_status_t pe_engine_detach_mixed_game(pe_engine_t* engine, struct mixed_game** out_mixed);
PE_API pe_status_t pe_engine_get_mixed_report(const pe_engine_t* engine, struct mixed_game_report* out_report);
PE_API pe_status_t pe_engine_choose_mixed_variant(pe_engine_t* engine, int variant_index);
PE_API bool pe_engine_is_mixed_mode(const pe_engine_t* engine);

/* ===== Game State Queries ===== */

/* Get current pot size */
PE_API pe_status_t pe_engine_get_pot_size(const pe_game_state_t* state, int64_t* out_pot);

/* Get number of active players */
PE_API pe_status_t pe_engine_get_active_player_count(const pe_game_state_t* state, int* out_count);

/* Get current betting round */
PE_API pe_status_t pe_engine_get_current_round(const pe_game_state_t* state, int* out_round);

/* Get hand number */
PE_API pe_status_t pe_engine_get_hand_number(const pe_game_state_t* state, uint64_t* out_hand_number);

/* Get player whose action is required */
PE_API pe_status_t pe_engine_get_action_on_player(const pe_game_state_t* state, int* out_player_id);

/* Get number of community cards dealt */
PE_API pe_status_t pe_engine_get_num_community_cards(const pe_game_state_t* state, int* out_count);

/* Check if hand is complete */
PE_API pe_status_t pe_engine_is_hand_complete(const pe_game_state_t* state, bool* out_complete);

/* Check if showdown is reached */
PE_API pe_status_t pe_engine_is_showdown_reached(const pe_game_state_t* state, bool* out_showdown);

/* ===== Player State Queries ===== */

/* Get player stack size */
PE_API pe_status_t pe_engine_get_player_stack_size(const pe_game_state_t* state, int player_id, int64_t* out_stack);

/* Get player current bet amount */
PE_API pe_status_t pe_engine_get_player_bet_amount(const pe_game_state_t* state, int player_id, int64_t* out_bet);

/* Get player last action */
PE_API pe_status_t pe_engine_get_player_last_action(const pe_game_state_t* state, int player_id, int* out_action);

/* Check if player is active in hand */
PE_API pe_status_t pe_engine_is_player_active(const pe_game_state_t* state, int player_id, bool* out_active);

/* Check if player is all-in */
PE_API pe_status_t pe_engine_is_player_all_in(const pe_game_state_t* state, int player_id, bool* out_all_in);

/* Check if player has folded */
PE_API pe_status_t pe_engine_has_player_folded(const pe_game_state_t* state, int player_id, bool* out_folded);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ENGINE_ENGINE_API_H */
