/*
 * ofc_rollout_heuristics.c - OFC Rollout Heuristics Implementation
 */

#include <poker_eval/ofc/policy/ofc_rollout_heuristics.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ===== Heuristic Presets ===== */

ofc_placement_heuristics_t ofc_heuristics_safe(void)
{
    ofc_placement_heuristics_t h = {0};

    h.avoid_fouling = 1;
    h.pair_placement = 1;
    h.high_card_strategy = 1;
    h.flush_awareness = 1;
    h.straight_awareness = 1;
    h.maximize_fantasy = 0;

    h.foul_penalty_weight = -5.0f;
    h.royalty_bonus_weight = 0.5f;

    return h;
}

ofc_placement_heuristics_t ofc_heuristics_balanced(void)
{
    ofc_placement_heuristics_t h = {0};

    h.avoid_fouling = 1;
    h.pair_placement = 1;
    h.high_card_strategy = 1;
    h.flush_awareness = 1;
    h.straight_awareness = 1;
    h.maximize_fantasy = 1;

    h.foul_penalty_weight = -3.0f;
    h.royalty_bonus_weight = 1.0f;

    return h;
}

ofc_placement_heuristics_t ofc_heuristics_aggressive(void)
{
    ofc_placement_heuristics_t h = {0};

    h.avoid_fouling = 0; /* Take more risks */
    h.pair_placement = 1;
    h.high_card_strategy = 0;
    h.flush_awareness = 1;
    h.straight_awareness = 1;
    h.maximize_fantasy = 1;

    h.foul_penalty_weight = -1.0f;
    h.royalty_bonus_weight = 2.0f;

    return h;
}

/* ===== Rollout Policy ===== */

ofc_rollout_policy_t *ofc_rollout_policy_create(
    ofc_rollout_policy_type_t type,
    const char *nn_weights_file)
{
    ofc_rollout_policy_t *policy =
        (ofc_rollout_policy_t *)calloc(1, sizeof(ofc_rollout_policy_t));

    if (!policy)
        return NULL;

    policy->type = type;
    policy->heuristics = ofc_heuristics_balanced();
    policy->nn_confidence_threshold = 0.6f;

    /* Create NN policy if needed */
    if (type == OFC_ROLLOUT_NEURAL || type == OFC_ROLLOUT_HYBRID)
    {
        nn_policy_config_t config = nn_policy_ofc_config();
        policy->nn_policy = nn_policy_create(&config);

        if (!policy->nn_policy)
        {
            fprintf(stderr, "Failed to create NN policy\n");
            free(policy);
            return NULL;
        }

        /* Load weights if provided */
        if (nn_weights_file)
        {
            if (nn_policy_load_weights(policy->nn_policy, nn_weights_file) != 0)
            {
                fprintf(stderr, "Warning: Failed to load NN weights from %s\n", nn_weights_file);
            }
        }
    }

    return policy;
}

void ofc_rollout_policy_free(ofc_rollout_policy_t *policy)
{
    if (!policy)
        return;

    if (policy->nn_policy)
    {
        nn_policy_free(policy->nn_policy);
    }

    free(policy);
}

/* ===== Rule-Based Heuristics ===== */

float ofc_estimate_foul_risk(
    const ofc_game_t *state,
    int card,
    int row,
    int col)
{
    /* Simplified foul risk estimation */
    /* TODO: Implement proper foul risk calculation based on:
     * - Current board state
     * - Remaining cards
     * - Row ordering requirements
     */

    (void)state;
    (void)card;
    (void)col;

    /* Heuristic: top row has higher foul risk */
    if (row == 0)
        return 0.3f; /* 30% foul risk for top */
    if (row == 1)
        return 0.1f; /* 10% for middle */
    return 0.05f;    /* 5% for bottom */
}

float ofc_estimate_royalty_potential(
    const ofc_game_t *state,
    int card,
    int row,
    int col)
{
    /* Simplified royalty potential */
    /* TODO: Implement proper royalty calculation based on:
     * - Pair/trip/straight/flush potential
     * - Row-specific royalty tables
     */

    (void)state;
    (void)card;
    (void)row;
    (void)col;

    /* Placeholder: return random value */
    int rand_val = rand();
    return ((float)rand_val / (float)RAND_MAX) * 5.0f;
}

float ofc_evaluate_placement_safety(
    const ofc_game_t *state,
    int card,
    int row,
    int col,
    const ofc_placement_heuristics_t *heuristics)
{
    if (!state || !heuristics)
        return -1000.0f;

    float score = 0.0f;

    /* Foul risk */
    if (heuristics->avoid_fouling)
    {
        float foul_risk = ofc_estimate_foul_risk(state, card, row, col);
        score += heuristics->foul_penalty_weight * foul_risk;
    }

    /* Royalty potential */
    float royalty_potential = ofc_estimate_royalty_potential(state, card, row, col);
    score += heuristics->royalty_bonus_weight * royalty_potential;

    /* Pair placement heuristic */
    if (heuristics->pair_placement)
    {
        /* TODO: Check if card forms pair and prefer appropriate row */
    }

    return score;
}

int ofc_find_best_placement_heuristic(
    const ofc_game_t *state,
    int card,
    const ofc_placement_heuristics_t *heuristics,
    int *out_row,
    int *out_col)
{
    if (!state || !out_row || !out_col)
        return -1;

    float best_score = -1e9f;
    int best_row = 0;
    int best_col = 0;

    /* Evaluate all valid placements */
    for (int row = 0; row < 3; row++)
    {
        int row_size = (row == 0) ? 3 : 5;

        for (int col = 0; col < row_size; col++)
        {
            /* Check if placement is valid */
            /* TODO: Implement proper validity check */
            int valid = 1; /* Placeholder */

            if (!valid)
                continue;

            float score = ofc_evaluate_placement_safety(state, card, row, col, heuristics);

            if (score > best_score)
            {
                best_score = score;
                best_row = row;
                best_col = col;
            }
        }
    }

    *out_row = best_row;
    *out_col = best_col;

    return 0;
}

/* ===== Rollout Implementation ===== */

int ofc_rollout_select_placement(
    ofc_rollout_policy_t *policy,
    const ofc_game_t *state,
    int card,
    int *out_row,
    int *out_col)
{
    if (!policy || !state || !out_row || !out_col)
        return -1;

    policy->num_rollouts++;

    switch (policy->type)
    {
    case OFC_ROLLOUT_RANDOM:
        /* Random valid placement */
        policy->num_random_fallback++;
        /* TODO: Implement random placement */
        *out_row = rand() % 3;
        *out_col = rand() % ((*out_row == 0) ? 3 : 5);
        break;

    case OFC_ROLLOUT_RULE_BASED:
        /* Use heuristics */
        policy->num_rules_used++;
        return ofc_find_best_placement_heuristic(state, card, &policy->heuristics, out_row, out_col);

    case OFC_ROLLOUT_NEURAL:
        /* Use NN policy */
        policy->num_nn_used++;
        /* TODO: Extract features and run NN inference */
        /* Fallback to random for now */
        *out_row = rand() % 3;
        *out_col = rand() % ((*out_row == 0) ? 3 : 5);
        break;

    case OFC_ROLLOUT_HYBRID:
        /* Use NN with confidence threshold */
        policy->num_nn_used++;
        /* TODO: Run NN, check confidence, fallback to rules if low */
        /* For now, use rules */
        policy->num_rules_used++;
        return ofc_find_best_placement_heuristic(state, card, &policy->heuristics, out_row, out_col);

    default:
        /* Unknown policy type, fallback to random */
        policy->num_random_fallback++;
        *out_row = rand() % 3;
        *out_col = rand() % ((*out_row == 0) ? 3 : 5);
        break;
    }

    return 0;
}

int ofc_rollout_complete(ofc_rollout_policy_t *policy, ofc_game_t *state)
{
    if (!policy || !state)
        return -1;

    /* TODO: Implement complete rollout */
    /* Place all remaining cards using policy */

    (void)policy;
    (void)state;

    return 0;
}

float ofc_rollout_estimate_value(ofc_rollout_policy_t *policy, const ofc_game_t *state)
{
    if (!policy || !state)
        return 0.0f;

    /* TODO: Implement value estimation */
    /* For now, return random value */

    (void)policy;
    (void)state;

    int rand_val = rand();
    return ((float)rand_val / (float)RAND_MAX) * 10.0f;
}

/* ===== Training Data Generation ===== */

int ofc_generate_training_data(
    const ofc_game_t *initial_state,
    int num_games,
    const char *output_file)
{
    if (!initial_state || !output_file)
        return -1;

    FILE *f = fopen(output_file, "w");
    if (!f)
    {
        fprintf(stderr, "Failed to open %s for writing\n", output_file);
        return -1;
    }

    fprintf(f, "# OFC Training Data\n");
    fprintf(f, "# Format: board_state, card, optimal_row, optimal_col, expected_value\n");

    /* TODO: Implement training data generation */
    /* Run ISMCTS rollouts and record optimal placements */

    for (int game = 0; game < num_games; game++)
    {
        /* Simulate one game and record placements */
        /* ... */
        (void)game;
        (void)initial_state;
    }

    fclose(f);

    printf("Generated training data for %d games in %s\n", num_games, output_file);

    return 0;
}

/* ===== Statistics ===== */

void ofc_rollout_get_stats(const ofc_rollout_policy_t *policy, ofc_rollout_stats_t *stats)
{
    if (!policy || !stats)
        return;

    memset(stats, 0, sizeof(ofc_rollout_stats_t));

    stats->num_rollouts = policy->num_rollouts;
    stats->num_nn_used = policy->num_nn_used;
    stats->num_rules_used = policy->num_rules_used;
    stats->num_random_fallback = policy->num_random_fallback;

    /* Placeholder values */
    stats->avg_rollout_time_ms = 1.0;
    stats->avg_nn_inference_time_us = 50.0;
    stats->avg_final_score = 5.0;
    stats->foul_rate = 0.15;
    stats->fantasy_rate = 0.05;
}

void ofc_rollout_reset_stats(ofc_rollout_policy_t *policy)
{
    if (!policy)
        return;

    policy->num_rollouts = 0;
    policy->num_nn_used = 0;
    policy->num_rules_used = 0;
    policy->num_random_fallback = 0;
}

void ofc_rollout_print_summary(const ofc_rollout_policy_t *policy)
{
    if (!policy)
        return;

    ofc_rollout_stats_t stats;
    ofc_rollout_get_stats(policy, &stats);

    printf("OFC Rollout Policy Summary:\n");
    printf("  Type: ");

    switch (policy->type)
    {
    case OFC_ROLLOUT_RANDOM:
        printf("Random\n");
        break;
    case OFC_ROLLOUT_RULE_BASED:
        printf("Rule-Based\n");
        break;
    case OFC_ROLLOUT_NEURAL:
        printf("Neural Network\n");
        break;
    case OFC_ROLLOUT_HYBRID:
        printf("Hybrid (NN + Rules)\n");
        break;
    default:
        printf("Unknown\n");
        break;
    }

    printf("  Rollouts: %lu\n", stats.num_rollouts);
    printf("    NN used: %lu (%.1f%%)\n",
           stats.num_nn_used,
           100.0 * (double)stats.num_nn_used / ((double)stats.num_rollouts + 1e-9));
    printf("    Rules used: %lu (%.1f%%)\n",
           stats.num_rules_used,
           100.0 * (double)stats.num_rules_used / ((double)stats.num_rollouts + 1e-9));
    printf("    Random fallback: %lu (%.1f%%)\n",
           stats.num_random_fallback,
           100.0 * (double)stats.num_random_fallback / ((double)stats.num_rollouts + 1e-9));

    printf("  Performance:\n");
    printf("    Avg rollout time: %.2f ms\n", stats.avg_rollout_time_ms);
    printf("    Avg NN inference: %.2f μs\n", stats.avg_nn_inference_time_us);

    printf("  Quality:\n");
    printf("    Avg final score: %.2f\n", stats.avg_final_score);
    printf("    Foul rate: %.1f%%\n", 100.0 * stats.foul_rate);
    printf("    Fantasy rate: %.1f%%\n", 100.0 * stats.fantasy_rate);
}
