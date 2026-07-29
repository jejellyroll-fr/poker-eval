/*
 * holdem_turn_adapter.h - Hold'em turn CFR adapter
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#ifndef POKER_EVAL_HOLDEM_TURN_ADAPTER_H
#define POKER_EVAL_HOLDEM_TURN_ADAPTER_H

#include "cfr_core.h"
#include "poker_eval/core/poker_defs.h"
#include "poker_eval/core/modern_cardmask.h"
#include "poker_eval/engine/solvers/cfr/holdem_river_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    mask_t h0;
    mask_t h1;
    mask_t board4;
    mask_t full_board;
    holdem_river_state_t river_state;
} ht_state_t;

/* Create Hold'em turn adapter */
cfr_game_t* holdem_turn_adapter_create(void);

void ht_build_game_sampled_river(const EvalContext *ctx, mask_t h0, mask_t h1, mask_t board4, cfr_game_t *out_game, ht_state_t *out_state, unsigned seed);

/* Destroy adapter */
void holdem_turn_adapter_destroy(cfr_game_t* game);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_HOLDEM_TURN_ADAPTER_H */
