/*
 * Public Game Engine API (canonical header)
 * Ownership: GameState est créé/détruit par l'appelant;
 *            GameEngine ne détruit pas GameState.
 */

#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/universal_deck.h>
#include <poker_eval/core/status.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct mixed_game;
    struct mixed_game_report;

/* Maximum players supported by game engine */
#define MAX_PLAYERS 10
#define MAX_BETTING_ROUNDS 4
#define MAX_COMMUNITY_CARDS 7
#define MAX_HOLE_CARDS 7

    /* Game types supported */
    typedef enum
    {
        GAME_TEXAS_HOLDEM = 0,
        GAME_OMAHA,
        GAME_OMAHA_HILO,
        GAME_PLO_5CARD,
        GAME_PLO_6CARD,
        GAME_SEVEN_STUD,
        GAME_SEVEN_STUD_HILO,
        GAME_RAZZ,
        GAME_FIVE_DRAW,
        GAME_DEUCE_TO_SEVEN,
        GAME_BADUGI,
        GAME_OFC,
        GAME_MIXED_HORSE,
        GAME_TYPE_COUNT
    } game_type_t;

    /* Betting structure types */
    typedef enum
    {
        BETTING_LIMIT = 0,
        BETTING_NO_LIMIT,
        BETTING_POT_LIMIT,
        BETTING_SPREAD_LIMIT,
        BETTING_STRUCTURE_COUNT
    } betting_structure_t;

    /* Player actions */
    /* Player action types - compatible with FPDB-3 action codes */
    typedef enum
    {
        ACTION_CALL = 6,    /* FPDB-3: "calls" = 6 */
        ACTION_RAISE = 7,   /* FPDB-3: "raises" = 7 */
        ACTION_BET = 8,     /* FPDB-3: "bets" = 8 */
        ACTION_FOLD = 10,   /* FPDB-3: "folds" = 10 */
        ACTION_CHECK = 11,  /* FPDB-3: "checks" = 11 */
        ACTION_ALL_IN = 18, /* Custom: not in FPDB-3, using safe value */
        ACTION_COUNT = 19
    } player_action_type_t;

    /* Betting rounds */
    typedef enum
    {
        ROUND_PREFLOP = 0,
        ROUND_FLOP,
        ROUND_TURN,
        ROUND_RIVER,
        ROUND_SHOWDOWN,
        ROUND_COUNT
    } betting_round_t;

    /* Player action structure */
    typedef struct
    {
        player_action_type_t type;
        int64_t amount;
        int player_id;
        betting_round_t round;
        bool is_valid;
    } PlayerAction;

    /* Blind structure */
    typedef struct
    {
        int64_t small_blind;
        int64_t big_blind;
        int64_t ante;
        int button_position;
    } BlindStructure;

    /* Betting limits */
    typedef struct
    {
        int64_t min_bet;
        int64_t max_bet;
        int64_t min_raise;
        int64_t max_raise;
        bool cap_raises;
        int max_raises_per_round;
    } BettingLimits;

    /* Player state */
    typedef struct
    {
        int player_id;
        StdDeck_CardMask hole_cards;  /* Legacy CardMask - use cardmask_to_mask_t() for modern API */
        int64_t stack_size;
        int64_t bet_amount;
        int64_t total_investment;
        player_action_type_t last_action;
        bool is_active;
        bool is_all_in;
        bool has_folded;
        int position;
    } PlayerState;

    /* Game rules configuration */
    typedef struct
    {
        game_type_t game_type;
        betting_structure_t betting_structure;
        deck_type_t deck_type;

        /* Game-specific parameters */
        int num_hole_cards;
        int num_community_cards;
        int max_players;
        bool has_low_component;
        bool use_ace_low;
        bool use_straights;
        bool use_flushes;

        /* Betting rules */
        BettingLimits limits;
        bool allow_check_raise;
        bool kill_pot_rules;

        /* Special rules */
        bool must_use_both_hole_cards; /* Omaha */
        bool must_use_exactly_two;     /* PLO */
        bool razz_bring_in;            /* Stud games */
        bool ofc_progressive_royalty;  /* OFC */
    } GameRules;

    /* Game state */
    typedef struct
    {
        GameRules rules;
        BlindStructure blinds;

        /* Current hand state */
        PlayerState players[MAX_PLAYERS];
        int num_players;
        StdDeck_CardMask community_cards;  /* Legacy CardMask */
        int num_community_cards_dealt;

        /* Betting state */
        betting_round_t current_round;
        int64_t pot_size;
        int64_t side_pots[MAX_PLAYERS];
        int num_side_pots;
        int action_on_player;
        int num_actions_this_round;
        int raises_this_round;

        /* Hand management */
        uint64_t hand_number;
        StdDeck_CardMask deck_remaining;  /* Legacy CardMask */

        /* Game flow */
        bool hand_complete;
        bool showdown_reached;
        PlayerState *winners[MAX_PLAYERS];
        int num_winners;
    } GameState;

    /* Hand result */
    typedef struct
    {
        int player_id;
        mask_t best_hand;
        int hand_rank;
        int64_t winnings;
        bool is_winner;
        bool is_low_winner;
        char hand_description[128];
    } HandResult;

    /* Game engine */
    typedef struct
    {
        EvalContext *eval_ctx;
        GameRules default_rules;
        GameState *current_state; /* non-owned */

        /* Statistics tracking */
        uint64_t hands_played;
        uint64_t actions_validated;
        uint64_t rule_violations;

        /* Configuration */
        bool strict_validation;
        bool log_violations;
        bool auto_complete_hands;

        /* Mixed game support */
        struct mixed_game *mixed_game;
        bool mixed_mode_active;
    } GameEngine;

    /* Game engine lifecycle */
    GameEngine *game_engine_create(EvalContext *eval_ctx, const GameRules *rules);
    void game_engine_destroy(GameEngine *engine);

    /* Game state management */
    GameState *game_state_create(const GameRules *rules, const BlindStructure *blinds, int num_players);
    void game_state_destroy(GameState *state);
    GameState *game_state_copy(const GameState *state);

    /* Game setup */
    bool game_engine_setup_hand(GameEngine *engine, GameState *state, int button_position);
    bool game_engine_deal_hole_cards(GameEngine *engine, GameState *state);
    bool game_engine_deal_community_card(GameEngine *engine, GameState *state);
    bool game_engine_deal_flop(GameEngine *engine, GameState *state);
    bool game_engine_deal_turn(GameEngine *engine, GameState *state);
    bool game_engine_deal_river(GameEngine *engine, GameState *state);

    /* Action validation */
    bool game_engine_validate_action(GameEngine *engine, GameState *state, const PlayerAction *action);
    bool game_engine_is_action_valid(GameEngine *engine, GameState *state,
                                     player_action_type_t action_type, int64_t amount, int player_id);
    PlayerAction *game_engine_get_valid_actions(GameEngine *engine, GameState *state, int player_id);
    int game_engine_get_num_valid_actions(GameEngine *engine, GameState *state, int player_id);

    /* Action execution */
    bool game_engine_execute_action(GameEngine *engine, GameState *state, const PlayerAction *action);
    bool game_engine_advance_action(GameEngine *engine, GameState *state);
    bool game_engine_advance_round(GameEngine *engine, GameState *state);

    /* Hand completion */
    bool game_engine_complete_hand(GameEngine *engine, GameState *state, HandResult *results);
    bool game_engine_evaluate_showdown(GameEngine *engine, GameState *state, HandResult *results);
    bool game_engine_distribute_pot(GameEngine *engine, GameState *state, HandResult *results);

    /* Rule validation */
    bool game_engine_validate_hand_strength(GameEngine *engine, GameState *state, mask_t hand, int player_id);
    bool game_engine_validate_betting_action(GameEngine *engine, GameState *state, const PlayerAction *action);
    bool game_engine_validate_game_rules(GameEngine *engine, const GameRules *rules);

    /* Utility functions */
    bool game_engine_is_hand_complete(const GameState *state);
    bool game_engine_is_showdown_reached(const GameState *state);
    int game_engine_get_active_player_count(const GameState *state);
    int64_t game_engine_get_pot_size(const GameState *state);
    int64_t game_engine_get_amount_to_call(const GameState *state, int player_id);
    int64_t game_engine_get_min_raise_amount(const GameState *state, int player_id);
    int64_t game_engine_get_max_bet_amount(const GameState *state, int player_id);

    /* Game-specific helpers */
    bool game_engine_is_holdem_variant(game_type_t game_type);
    bool game_engine_is_omaha_variant(game_type_t game_type);
    bool game_engine_is_stud_variant(game_type_t game_type);
    bool game_engine_is_draw_variant(game_type_t game_type);
    bool game_engine_has_community_cards(game_type_t game_type);
    bool game_engine_has_low_component(game_type_t game_type);

    /* Configuration helpers */
    GameRules game_engine_get_default_rules(game_type_t game_type);
    BettingLimits game_engine_get_default_limits(betting_structure_t structure);
    BlindStructure game_engine_get_default_blinds(int64_t small_blind);

    /* String representations */
    const char *game_engine_game_type_to_string(game_type_t game_type);
    const char *game_engine_betting_structure_to_string(betting_structure_t structure);
    const char *game_engine_action_to_string(player_action_type_t action);
    const char *game_engine_round_to_string(betting_round_t round);

    /* Debug and logging */
    void game_engine_print_state(const GameState *state);
    void game_engine_print_action(const PlayerAction *action);
    void game_engine_print_results(const HandResult *results, int num_results);

    /* Error handling */
    typedef enum
    {
        GAME_ENGINE_OK = 0,
        GAME_ENGINE_ERROR_INVALID_PARAMS,
        GAME_ENGINE_ERROR_INVALID_ACTION,
        GAME_ENGINE_ERROR_INVALID_PLAYER,
        GAME_ENGINE_ERROR_INVALID_AMOUNT,
        GAME_ENGINE_ERROR_GAME_COMPLETE,
        GAME_ENGINE_ERROR_INSUFFICIENT_FUNDS,
        GAME_ENGINE_ERROR_RULE_VIOLATION,
        GAME_ENGINE_ERROR_MEMORY,
        GAME_ENGINE_ERROR_COUNT
    } game_engine_error_t;

    game_engine_error_t game_engine_get_last_error(const GameEngine *engine);

    /* Mixed game management */
    bool game_engine_attach_mixed_game(GameEngine *engine, struct mixed_game *mixed);
    struct mixed_game *game_engine_detach_mixed_game(GameEngine *engine);
    bool game_engine_is_mixed_mode(const GameEngine *engine);
    bool game_engine_get_mixed_report(const GameEngine *engine, struct mixed_game_report *out_report);
    bool game_engine_choose_mixed_variant(GameEngine *engine, int variant_index);
    const char *game_engine_error_to_string(game_engine_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* GAME_ENGINE_H */
