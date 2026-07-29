#ifndef POKER_EVAL_GAMES_MIXED_GAME_H
#define POKER_EVAL_GAMES_MIXED_GAME_H

#include <poker_eval/engine/game_engine.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIXED_GAME_MAX_VARIANTS 16

typedef enum {
    MIXED_ROTATION_SEQUENTIAL = 0,
    MIXED_ROTATION_DEALER_CHOICE,
    MIXED_ROTATION_BUTTON_CHOICE,
    MIXED_ROTATION_RANDOM
} mixed_rotation_t;

typedef struct {
    game_type_t game;          /* Variant to play */
    GameRules custom_rules;    /* Optional overrides when custom_rules.game_type == game */
    int hands_per_round;       /* Number of consecutive hands before rotating */
} mixed_variant_t;

#ifndef MIXED_GAME_T_DEFINED
#define MIXED_GAME_T_DEFINED
typedef struct mixed_game mixed_game_t;
#endif

/* Lifecycle */
mixed_game_t *mixed_game_create(const mixed_variant_t *variants,
                                int variant_count,
                                mixed_rotation_t rotation);
void mixed_game_destroy(mixed_game_t *mixed);

/* Runtime */
const mixed_variant_t *mixed_game_current(const mixed_game_t *mixed);
void mixed_game_reset(mixed_game_t *mixed);

/* Advance rotation after a hand has finished */
void mixed_game_advance(mixed_game_t *mixed,
                        const GameState *state,
                        int dealer_player_id);

/* Statistics */
typedef struct mixed_variant_stats {
    game_type_t game;
    int hands_played;
    double total_winnings;
    int wins;
    int ties;
    int losses;
} mixed_variant_stats_t;

typedef struct mixed_game_report {
    mixed_rotation_t rotation;
    int variant_count;
    int current_index;
    int total_hands_played;
    mixed_variant_stats_t stats[MIXED_GAME_MAX_VARIANTS];
} mixed_game_report_t;

void mixed_game_record_result(mixed_game_t *mixed,
                              const HandResult *results,
                              int result_count);

void mixed_game_get_report(const mixed_game_t *mixed,
                           mixed_game_report_t *out_report);

/* Explicit selection for dealer/button choice */
bool mixed_game_set_current_variant(mixed_game_t *mixed, int variant_index);

/* Retrieve effective rules for current variant */
bool mixed_game_get_current_rules(const mixed_game_t *mixed, GameRules *out_rules);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GAMES_MIXED_GAME_H */
