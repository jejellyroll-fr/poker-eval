#ifndef POKER_EVAL_HOLDEM_MULTI_RIVER_ADAPTER_H
#define POKER_EVAL_HOLDEM_MULTI_RIVER_ADAPTER_H

#include "cfr_core.h"
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HRM_MAX_PLAYERS 7

typedef struct {
    const EvalContext *ctx;
    int num_players;
    int to_act;
    mask_t board;
    mask_t hands[HRM_MAX_PLAYERS];
    int active[HRM_MAX_PLAYERS];
    double contributions[HRM_MAX_PLAYERS];
    double pot;
    double bet_size;
    double utilities[HRM_MAX_PLAYERS];
    int util_ready;
    int actions_taken;
} holdem_multi_state_t;

void hrm_build_game(const EvalContext *ctx,
                    int num_players,
                    const mask_t *hands,
                    mask_t board,
                    double bet_size,
                    cfr_game_t *out_game,
                    holdem_multi_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_HOLDEM_MULTI_RIVER_ADAPTER_H */
