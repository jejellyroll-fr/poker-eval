#ifndef POKER_EVAL_DRAW_GAME_ADAPTER_H
#define POKER_EVAL_DRAW_GAME_ADAPTER_H

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/draw_chance.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_DRAW_CFR_MAX_ACTIONS 16

typedef double (*pe_draw_terminal_value_fn)(mask_t player0_hand,
                                            mask_t player1_hand,
                                            int player,
                                            void *user_data);

typedef struct {
    pe_draw_variant_t variant;
    mask_t player0_hand;
    mask_t player1_hand;
    int action_count[2];
    mask_t discard_actions[2][PE_DRAW_CFR_MAX_ACTIONS];
    pe_draw_terminal_value_fn terminal_value;
    void *terminal_user_data;
} pe_draw_cfr_config_t;

typedef struct {
    const pe_draw_cfr_config_t *config;
    mask_t hands[2];
    mask_t discard;
    pe_draw_chance_t chance;
    int phase; /* 0=p0 discard, 1=p0 chance, 2=p1 discard, 3=p1 chance, 4=terminal */
} pe_draw_cfr_state_t;

/* Build a two-player one-draw CFR game. The action menus are supplied by the
 * caller so abstractions can expose a bounded subset of discard masks. */
int pe_draw_cfr_build_game(const pe_draw_cfr_config_t *config,
                           cfr_game_t *out_game,
                           pe_draw_cfr_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_DRAW_GAME_ADAPTER_H */
