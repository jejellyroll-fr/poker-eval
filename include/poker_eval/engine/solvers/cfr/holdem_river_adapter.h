/*
 * holdem_river_adapter.h - Hold'em river CFR adapter
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#ifndef POKER_EVAL_HOLDEM_RIVER_ADAPTER_H
#define POKER_EVAL_HOLDEM_RIVER_ADAPTER_H

#include "cfr_core.h"
#include "poker_eval/core/eval_context.h"
#include "poker_eval/core/modern_cardmask.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hold'em river adapter */
typedef struct {
    mask_t board;
    mask_t p0_hand;
    mask_t p1_hand;
    double pot;
    double stack_p0;
    double stack_p1;
    int num_bet_sizes;
    double bet_sizes[8];  /* Fractions of pot */
    int max_raises;
    unsigned int hist;
    int to_act;
    mask_t h0;
    mask_t h1;
    const void* ctx;
    double to_call;
    int raises_left;
    double bet_fracs[8];
    int raise_cap;
    int bucket_mode;
    int bucket_bins;
    int bucket_thresh_count;
    int extra_feats;
    double bet_half;
    double bet_pot;
    uint32_t bucket_thresh[16];
} holdem_river_state_t;

/* Create Hold'em river adapter */
cfr_game_t* holdem_river_adapter_create(const holdem_river_state_t* initial_state);

/* Build the game */
void hr_build_game(const EvalContext *ctx, mask_t h0, mask_t h1, mask_t board, cfr_game_t *out_game, holdem_river_state_t *out_state);

/* Destroy adapter */
void holdem_river_adapter_destroy(cfr_game_t* game);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_HOLDEM_RIVER_ADAPTER_H */
