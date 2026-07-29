#ifndef BETTING_ENGINE_H
#define BETTING_ENGINE_H

#include <poker_eval/engine/game_engine.h>
#include <poker_eval/core/status.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Betting action with detailed information */
    typedef struct
    {
        player_action_type_t action;
        int64_t amount;
        int64_t min_amount;
        int64_t max_amount;
        bool is_valid;
        const char *description;
    } BettingAction;

    /* Betting context for calculations */
    typedef struct
    {
        const GameState *game_state;
        int player_id;
        int64_t pot_size;
        int64_t amount_to_call;
        int64_t current_bet;
        int64_t last_raise_amount;
        int num_raises_this_round;
        bool is_heads_up;
    } BettingContext;

    /* Betting engine */
    typedef struct
    {
        betting_structure_t structure;
        BettingLimits limits;
        bool cap_raises;
        int max_raises_per_round;

        /* Pot limit specific */
        bool calculate_pot_limit_dynamically;

        /* Spread limit specific */
        int64_t spread_min;
        int64_t spread_max;

        /* Statistics */
        uint64_t actions_calculated;
        uint64_t pot_calculations;
    } BettingEngine;

    /* Betting engine lifecycle */
    BettingEngine *betting_engine_create(betting_structure_t structure, const BettingLimits *limits);
    void betting_engine_destroy(BettingEngine *engine);

    /* Action calculation */
    BettingAction *betting_engine_get_valid_actions(BettingEngine *engine, const GameState *state, int player_id);
    int betting_engine_get_num_valid_actions(BettingEngine *engine, const GameState *state, int player_id);
    bool betting_engine_is_action_valid(BettingEngine *engine, const GameState *state,
                                        player_action_type_t action, int64_t amount, int player_id);

    /* Amount validation and calculation */
    bool betting_engine_validate_bet_size(BettingEngine *engine, const GameState *state,
                                          int64_t amount, int player_id);
    int64_t betting_engine_calculate_min_bet(BettingEngine *engine, const GameState *state, int player_id);
    int64_t betting_engine_calculate_max_bet(BettingEngine *engine, const GameState *state, int player_id);
    int64_t betting_engine_calculate_min_raise(BettingEngine *engine, const GameState *state, int player_id);
    int64_t betting_engine_calculate_max_raise(BettingEngine *engine, const GameState *state, int player_id);

    /* Pot limit specific calculations */
    int64_t betting_engine_calculate_pot_limit(BettingEngine *engine, const GameState *state, int player_id);
    int64_t betting_engine_calculate_effective_pot_size(BettingEngine *engine, const GameState *state, int player_id);

    /* Betting structure helpers */
    bool betting_engine_is_limit_structure(betting_structure_t structure);
    bool betting_engine_is_no_limit_structure(betting_structure_t structure);
    bool betting_engine_is_pot_limit_structure(betting_structure_t structure);
    bool betting_engine_allows_check_raise(BettingEngine *engine, const GameState *state);

    /* Side pot calculation */
    typedef struct
    {
        int64_t amount;
        int eligible_players[MAX_PLAYERS];
        int num_eligible_players;
        int64_t contribution_per_player[MAX_PLAYERS];
    } SidePot;

    typedef struct
    {
        SidePot side_pots[MAX_PLAYERS];
        int num_side_pots;
        int64_t total_pot_value;
    } SidePotCalculation;

    SidePotCalculation *betting_engine_calculate_side_pots(BettingEngine *engine, const GameState *state);
    void betting_engine_free_side_pot_calculation(SidePotCalculation *calculation);

    /* Utility functions */
    BettingContext betting_engine_create_context(const GameState *state, int player_id);
    int64_t betting_engine_get_highest_bet(const GameState *state);
    int64_t betting_engine_get_last_raise_amount(const GameState *state);
    bool betting_engine_can_player_act(const GameState *state, int player_id);

    /* Advanced betting features */
    bool betting_engine_is_betting_capped(BettingEngine *engine, const GameState *state);
    int64_t betting_engine_calculate_all_in_amount(BettingEngine *engine, const GameState *state, int player_id);
    bool betting_engine_would_action_cap_betting(BettingEngine *engine, const GameState *state,
                                                 player_action_type_t action, int player_id);

    /* Tournament specific features */
    typedef struct
    {
        int64_t ante_schedule[32];     /* Ante amounts by level */
        int64_t blind_schedule[32][2]; /* Small/Big blind by level */
        int level_duration_minutes[32];
        int num_levels;
        int current_level;
        int64_t time_left_in_level;
    } TournamentStructure;

    typedef struct
    {
        TournamentStructure *structure;
        int64_t starting_stack;
        int64_t total_chips_in_play;
        int players_remaining;
        bool in_the_money;
        double average_stack;
        int64_t prize_pool;
    } TournamentContext;

    /* Tournament betting calculations */
    double betting_engine_calculate_tournament_pressure(const TournamentContext *tournament, int player_id);
    bool betting_engine_is_tournament_bubble(const TournamentContext *tournament);
    double betting_engine_calculate_icm_pressure(const TournamentContext *tournament,
                                                 const GameState *state, int player_id);

    /* Error handling */
    typedef enum
    {
        BETTING_ENGINE_OK = 0,
        BETTING_ENGINE_ERROR_INVALID_PARAMS,
        BETTING_ENGINE_ERROR_INVALID_STRUCTURE,
        BETTING_ENGINE_ERROR_INSUFFICIENT_FUNDS,
        BETTING_ENGINE_ERROR_BETTING_CAPPED,
        BETTING_ENGINE_ERROR_INVALID_AMOUNT,
        BETTING_ENGINE_ERROR_MEMORY,
        BETTING_ENGINE_ERROR_COUNT
    } betting_engine_error_t;

    betting_engine_error_t betting_engine_get_last_error(const BettingEngine *engine);
    const char *betting_engine_error_to_string(betting_engine_error_t error);

    /* Debug and utilities */
    void betting_engine_print_actions(const BettingAction *actions, int num_actions);
    void betting_engine_print_side_pots(const SidePotCalculation *calculation);
    void betting_engine_print_betting_context(const BettingContext *context);

#ifdef __cplusplus
}
#endif

#endif /* BETTING_ENGINE_H */
