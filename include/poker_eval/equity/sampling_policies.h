/*
 * sampling_policies.h - Monte Carlo/QMC sampling policies for equity estimation
 */

#ifndef POKER_EVAL_EQUITY_SAMPLING_POLICIES_H
#define POKER_EVAL_EQUITY_SAMPLING_POLICIES_H

#include <poker_eval/deck/deck_std.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PE_SAMPLING_MC_UNIFORM = 0,
    PE_SAMPLING_QMC_HALTON,
    PE_SAMPLING_QMC_SOBOL, /* Sobol (base-2) with per-dimension digital Owen scramble */
    PE_SAMPLING_IMPORTANCE_BOARD_AWARE, /* Heuristic importance sampling */
    PE_SAMPLING_STRATIFIED_BOARD_AWARE  /* Analytic-PDF stratified sampling */
} pe_sampling_policy_t;

/* Lightweight context for sampling policy */
typedef struct {
    pe_sampling_policy_t policy;
    uint64_t seq_index;   /* current global sequence index */
    /* Stratified policy (pair-class) runtime context */
    int last_num_cards;
    StdDeck_CardMask last_dead;
    uint64_t pair_counts[3]; /* [distinct, one_pair, trips] */
    uint64_t pair_total;     /* total C(N,K) */
    double pair_weights[3];  /* target importance weights per class */
    double pair_Z;           /* Z = sum_c weights[c]*count[c] */
    int custom_weights_set;  /* 1 if weights set via API/env, else 0 */
} pe_sampling_ctx_t;

/* Batch of boards type */
#include <poker_eval/equity/batched_montecarlo.h>

/* API */
void pe_sampling_set_policy(pe_sampling_policy_t policy);
pe_sampling_policy_t pe_sampling_get_policy(void);
void pe_sampling_reset(uint64_t seq_start);

/* Generate a batch of random/quasi-random boards according to current policy */
void pe_sampling_generate_boards(BoardBatch* batch,
                                 StdDeck_CardMask dead,
                                 int num_cards_to_draw,
                                 int batch_size);

/* Importance-sampling weight w(b) ∝ 1 / q(b), where q(b) ∝ f(class(b)).
 * For non-importance policies, returns 1.0. */
double pe_sampling_importance_weight(StdDeck_CardMask board,
                                     int num_cards_to_draw);

/* Runtime control for stratified policy weights (Distinct/OnePair/Trips). */
void pe_sampling_set_pair_weights(const double weights[3]);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_EQUITY_SAMPLING_POLICIES_H */
