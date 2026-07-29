/*
 * ofc_rollout_heuristics.h - Header for OFC Rollout Heuristics
 *
 * Public API for rule-based and NN-assisted placement heuristics in
 * Open-Face Chinese Poker (OFC).
 */

#ifndef OFC_ROLLOUT_HEURISTICS_H
#define OFC_ROLLOUT_HEURISTICS_H

#include <stddef.h>
#include <stdint.h>
#include <poker_eval/engine/policy/nn_policy.h> /* for nn_policy_t, nn_policy_config_t */
#include <poker_eval/ofc/ofc.h>                 /* for ofc_game_t and OFC types */
#include <poker_eval/core/card.h>               /* for card_t */

/* ===== Types ===== */

/* Use ofc_game_t from ofc.h for game state */

typedef struct
{
    int avoid_fouling;
    int pair_placement;
    int high_card_strategy;
    int flush_awareness;
    int straight_awareness;
    int maximize_fantasy;

    float foul_penalty_weight;
    float royalty_bonus_weight;
} ofc_placement_heuristics_t;

typedef enum
{
    OFC_ROLLOUT_RANDOM = 0,
    OFC_ROLLOUT_RULE_BASED,
    OFC_ROLLOUT_NEURAL,
    OFC_ROLLOUT_HYBRID
} ofc_rollout_policy_type_t;

typedef struct
{
    ofc_rollout_policy_type_t type;
    ofc_placement_heuristics_t heuristics;
    float nn_confidence_threshold;

    nn_policy_t *nn_policy;

    /* Counters */
    unsigned long num_rollouts;
    unsigned long num_nn_used;
    unsigned long num_rules_used;
    unsigned long num_random_fallback;
} ofc_rollout_policy_t;

typedef struct
{
    unsigned long num_rollouts;
    unsigned long num_nn_used;
    unsigned long num_rules_used;
    unsigned long num_random_fallback;

    double avg_rollout_time_ms;
    double avg_nn_inference_time_us;

    double avg_final_score;
    double foul_rate;
    double fantasy_rate;
} ofc_rollout_stats_t;

/* ===== Heuristic Presets ===== */

ofc_placement_heuristics_t ofc_heuristics_safe(void);
ofc_placement_heuristics_t ofc_heuristics_balanced(void);
ofc_placement_heuristics_t ofc_heuristics_aggressive(void);

/* ===== Policy Management ===== */

ofc_rollout_policy_t *ofc_rollout_policy_create(
    ofc_rollout_policy_type_t type,
    const char *nn_weights_file);

void ofc_rollout_policy_free(ofc_rollout_policy_t *policy);

/* ===== Heuristic Evaluation ===== */

float ofc_estimate_foul_risk(const ofc_game_t *state, int card, int row, int col);
float ofc_estimate_royalty_potential(const ofc_game_t *state, int card, int row, int col);

float ofc_evaluate_placement_safety(
    const ofc_game_t *state,
    int card,
    int row,
    int col,
    const ofc_placement_heuristics_t *heuristics);

int ofc_find_best_placement_heuristic(
    const ofc_game_t *state,
    int card,
    const ofc_placement_heuristics_t *heuristics,
    int *out_row,
    int *out_col);

/* ===== Rollout Interface ===== */

int ofc_rollout_select_placement(
    ofc_rollout_policy_t *policy,
    const ofc_game_t *state,
    int card,
    int *out_row,
    int *out_col);

int ofc_rollout_complete(ofc_rollout_policy_t *policy, ofc_game_t *state);
float ofc_rollout_estimate_value(ofc_rollout_policy_t *policy, const ofc_game_t *state);

/* ===== Training Data ===== */

int ofc_generate_training_data(
    const ofc_game_t *initial_state,
    int num_games,
    const char *output_file);

/* ===== Statistics ===== */

void ofc_rollout_get_stats(const ofc_rollout_policy_t *policy, ofc_rollout_stats_t *stats);
void ofc_rollout_reset_stats(ofc_rollout_policy_t *policy);
void ofc_rollout_print_summary(const ofc_rollout_policy_t *policy);

#endif /* OFC_ROLLOUT_HEURISTICS_H */
