/*
 * holdem_river_adapter.h - Hold'em river CFR adapter
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#ifndef POKER_EVAL_HOLDEM_RIVER_ADAPTER_H
#define POKER_EVAL_HOLDEM_RIVER_ADAPTER_H

#include "cfr_core.h"
#include "hand_clustering.h"
#include "strength_bucketing.h"
#include "board_texture.h"
#include "poker_eval/core/eval_context.h"
#include "poker_eval/core/modern_cardmask.h"
#include <poker_eval/solver/pe_compute.h>

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
    int bucket_mode; /* 0: none, 1: board, 2: board+player, 3: coarse, 4: k-means clusters */
    int bucket_bins;
    int bucket_thresh_count;
    int extra_feats;
    double bet_half;
    double bet_pot;
    uint32_t bucket_thresh[16];
    int suit_perm[4]; /* suit canonicalization mapping (label -> original suit) */
    /* FEAT-04: learned clustering abstraction used when bucket_mode == 4.
     * Trained outside the solve and shared across deals; the state neither owns
     * nor frees it. NULL falls back to the bucket_mode 3 abstraction. */
    pe_bucket_table_t *bucket_table;
    /* FEAT-13 (#190/#192): strength buckets (EHS/EHS2) + board-texture merging.
     * bucket_mode == 5 : strength buckets via pe_strength_table_t (board-specific,
     *   not owned by the state; NULL falls back to mode 3).
     * bucket_mode == 6 : board-texture merging via pe_board_texture_id, keyed on
     *   texture_level (pe_texture_filter_level_t, 0 = disabled).
     * bucket_mode == 7 : both combined (strength + texture pairing). */
    pe_strength_table_t *strength_table;
    int texture_level;
    /* FEAT-13 (#192): turn board (4 cards) when solved multi-street, so
     * texture merging can also collapse turn nodes reached from different
     * 4-board runs; MASK_EMPTY when solving river-only. */
    mask_t turn_board;
    /* Optional terminal evaluator borrowed from the compute layer. */
    const pe_compute_ops_t *compute_ops;
    void *compute_self;
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
