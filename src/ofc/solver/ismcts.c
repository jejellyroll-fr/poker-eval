/*
 * ismcts.c - Information Set Monte Carlo Tree Search for OFC (Implementation)
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include <poker_eval/ofc/solver/ismcts.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/core/eval.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

/* ===== Random Number Generator (PCG) ===== */

static inline uint64_t pcg_next(uint64_t* state) {
    uint64_t oldstate = *state;
    *state = oldstate * 6364136223846793005ULL + 1442695040888963407ULL;
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
}

static inline double pcg_uniform(uint64_t* state) {
    uint64_t val = pcg_next(state);
    return (double)val / (double)UINT64_MAX;
}

/* ===== Configuration ===== */

ofc_ismcts_config_t ofc_ismcts_default_config(void) {
    ofc_ismcts_config_t cfg;
    cfg.uct_c = ISMCTS_DEFAULT_UCT_C;
    cfg.max_iterations = ISMCTS_DEFAULT_ITERATIONS;
    cfg.max_depth = ISMCTS_DEFAULT_MAX_DEPTH;
    cfg.enable_foul_pruning = true;
    cfg.foul_threshold = ISMCTS_DEFAULT_FOUL_THRESHOLD;
    cfg.use_heuristic_rollout = false; /* Start with random rollout */
    cfg.num_determinizations = 1;      /* Single-world ISMCTS */
    cfg.random_seed = 1234567890ULL;
    return cfg;
}

/* ===== Node Management ===== */

ofc_ismcts_node_t* ofc_ismcts_node_create(
    const ofc_hand_t* hand,
    const StdDeck_CardMask* unseen,
    int cards_placed
) {
    ofc_ismcts_node_t* node = (ofc_ismcts_node_t*)calloc(1, sizeof(ofc_ismcts_node_t));
    if (!node) return NULL;

    if (hand) {
        memcpy(&node->my_hand, hand, sizeof(ofc_hand_t));
    }
    if (unseen) {
        node->unseen_cards = *unseen;
    } else {
        StdDeck_CardMask_RESET(node->unseen_cards);
    }

    node->cards_placed = cards_placed;
    node->total_reward = 0.0;
    node->visit_count = 0;
    node->parent = NULL;
    node->children = NULL;
    node->actions = NULL;
    node->num_children = 0;
    node->children_capacity = 0;
    node->foul_probability = 0.0;
    node->is_terminal = (cards_placed >= OFC_TOTAL_CARDS);
    node->is_pruned = false;

    return node;
}

void ofc_ismcts_node_free(ofc_ismcts_node_t* node) {
    if (!node) return;

    /* Recursively free children */
    for (int i = 0; i < node->num_children; i++) {
        ofc_ismcts_node_free(node->children[i]);
    }

    free(node->children);
    free(node->actions);
    free(node);
}

/* ===== Solver Management ===== */

ofc_ismcts_solver_t* ofc_ismcts_create(const ofc_ismcts_config_t* config) {
    ofc_ismcts_solver_t* solver = (ofc_ismcts_solver_t*)calloc(1, sizeof(ofc_ismcts_solver_t));
    if (!solver) return NULL;

    if (config) {
        memcpy(&solver->config, config, sizeof(ofc_ismcts_config_t));
    } else {
        solver->config = ofc_ismcts_default_config();
    }

    solver->root = NULL;
    solver->total_simulations = 0;
    solver->total_pruned_nodes = 0;
    solver->total_expansions = 0;
    solver->rng_state = solver->config.random_seed;

    return solver;
}

void ofc_ismcts_free(ofc_ismcts_solver_t* solver) {
    if (!solver) return;
    ofc_ismcts_node_free(solver->root);
    free(solver);
}

void ofc_ismcts_reset(ofc_ismcts_solver_t* solver) {
    if (!solver) return;
    ofc_ismcts_node_free(solver->root);
    solver->root = NULL;
    solver->total_simulations = 0;
    solver->total_pruned_nodes = 0;
    solver->total_expansions = 0;
}

/* ===== UCT Selection ===== */

ofc_ismcts_node_t* ofc_ismcts_select_child(ofc_ismcts_node_t* node, double uct_c) {
    if (!node || node->num_children == 0) return NULL;

    double best_value = -INFINITY;
    ofc_ismcts_node_t* best_child = NULL;

    double log_parent_visits = log((double)node->visit_count);

    for (int i = 0; i < node->num_children; i++) {
        ofc_ismcts_node_t* child = node->children[i];

        /* Skip pruned nodes */
        if (child->is_pruned) continue;

        /* UCB1 formula: exploit + explore */
        double exploit = 0.0;
        if (child->visit_count > 0) {
            exploit = child->total_reward / (double)child->visit_count;
        }

        double explore = 0.0;
        if (child->visit_count > 0) {
            explore = uct_c * sqrt(log_parent_visits / (double)child->visit_count);
        } else {
            /* Unvisited node - high priority */
            explore = INFINITY;
        }

        double ucb_value = exploit + explore;

        if (ucb_value > best_value) {
            best_value = ucb_value;
            best_child = child;
        }
    }

    return best_child;
}

/* ===== Expansion ===== */

int ofc_ismcts_expand_node(
    ofc_ismcts_solver_t* solver,
    ofc_ismcts_node_t* node,
    int card
) {
    if (!solver || !node || node->is_terminal) return 0;

    /* Determine valid placements */
    int valid_positions[OFC_NUM_HANDS];
    int num_valid = 0;

    for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
        int max_cards = (pos == OFC_TOP) ? OFC_TOP_CARDS :
                       (pos == OFC_MIDDLE) ? OFC_MIDDLE_CARDS : OFC_BOTTOM_CARDS;

        if (node->my_hand.card_count[pos] < max_cards) {
            valid_positions[num_valid++] = pos;
        }
    }

    if (num_valid == 0) {
        node->is_terminal = true;
        return 0;
    }

    /* Allocate children arrays */
    node->children_capacity = num_valid;
    node->children = (ofc_ismcts_node_t**)calloc(num_valid, sizeof(ofc_ismcts_node_t*));
    node->actions = (ofc_placement_t*)calloc(num_valid, sizeof(ofc_placement_t));
    if (!node->children || !node->actions) {
        free(node->children);
        free(node->actions);
        return 0;
    }

    /* Create child nodes for each valid placement */
    for (int i = 0; i < num_valid; i++) {
        ofc_position_t pos = (ofc_position_t)valid_positions[i];

        /* Clone parent hand and place card */
        ofc_hand_t child_hand = node->my_hand;
        StdDeck_CardMask_SET(child_hand.hands[pos], card);
        child_hand.card_count[pos]++;

        /* Create child node */
        ofc_ismcts_node_t* child = ofc_ismcts_node_create(
            &child_hand,
            &node->unseen_cards,
            node->cards_placed + 1
        );

        if (!child) continue;

        child->parent = node;

        /* Store action */
        node->actions[i].card_index = card;
        node->actions[i].position = pos;

        /* Foul-aware pruning */
        if (solver->config.enable_foul_pruning) {
            child->foul_probability = ofc_ismcts_estimate_foul_probability(
                &child_hand,
                &node->unseen_cards
            );

            if (child->foul_probability > solver->config.foul_threshold) {
                child->is_pruned = true;
                solver->total_pruned_nodes++;
            }
        }

        node->children[i] = child;
        node->num_children++;
    }

    solver->total_expansions++;
    return node->num_children;
}

/* ===== Simulation (Rollout) ===== */

double ofc_ismcts_simulate(
    ofc_ismcts_solver_t* solver,
    const ofc_ismcts_node_t* node,
    int card
) {
    if (!solver || !node) return 0.0;

    /* Clone hand state for simulation */
    ofc_hand_t sim_hand = node->my_hand;
    StdDeck_CardMask unseen = node->unseen_cards;
    int cards_placed = node->cards_placed;

    /* Random rollout - place remaining cards randomly */
    for (int depth = cards_placed; depth < OFC_TOTAL_CARDS && depth < solver->config.max_depth; depth++) {
        /* Find valid positions */
        int valid_pos[OFC_NUM_HANDS];
        int num_valid = 0;

        for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
            int max_cards = (pos == OFC_TOP) ? OFC_TOP_CARDS :
                           (pos == OFC_MIDDLE) ? OFC_MIDDLE_CARDS : OFC_BOTTOM_CARDS;
            if (sim_hand.card_count[pos] < max_cards) {
                valid_pos[num_valid++] = pos;
            }
        }

        if (num_valid == 0) break;

        /* Random placement */
        int choice = (int)(pcg_next(&solver->rng_state) % num_valid);
        ofc_position_t pos = (ofc_position_t)valid_pos[choice];

        /* Sample a random card from unseen */
        int unseen_count = 0;
        int unseen_cards[StdDeck_N_CARDS];
        for (int c = 0; c < StdDeck_N_CARDS; c++) {
            if (StdDeck_CardMask_CARD_IS_SET(unseen, c)) {
                unseen_cards[unseen_count++] = c;
            }
        }

        if (unseen_count == 0) break;

        int rand_idx = (int)(pcg_next(&solver->rng_state) % unseen_count);
        int drawn_card = unseen_cards[rand_idx];

        /* Place card */
        StdDeck_CardMask_SET(sim_hand.hands[pos], drawn_card);
        sim_hand.card_count[pos]++;
        StdDeck_CardMask_UNSET(unseen, drawn_card);
    }

    /* Evaluate final hand (simplified scoring) */
    /* TODO: Use proper OFC scoring from ofc.h */
    /* For now, return a heuristic based on hand quality */

    double reward = 0.0;

    /* Check for foul */
    /* TODO: Call proper foul detection */
    /* Simplified: assume no foul for now */

    /* Estimate hand strength */
    /* This is a placeholder - real implementation should use ofc_score_hand() */
    for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
        if (sim_hand.card_count[pos] > 0) {
            reward += (double)sim_hand.card_count[pos];
        }
    }

    return reward;
}

/* ===== Backpropagation ===== */

void ofc_ismcts_backpropagate(ofc_ismcts_node_t* node, double reward) {
    ofc_ismcts_node_t* current = node;
    while (current != NULL) {
        current->visit_count++;
        current->total_reward += reward;
        current = current->parent;
    }
}

/* ===== Foul Probability Estimation ===== */

double ofc_ismcts_estimate_foul_probability(
    const ofc_hand_t* hand,
    const StdDeck_CardMask* unseen
) {
    if (!hand) return 0.0;

    /* Simplified heuristic for foul probability */
    /* TODO: Implement more sophisticated estimation */

    int top_count = hand->card_count[OFC_TOP];
    int middle_count = hand->card_count[OFC_MIDDLE];
    int bottom_count = hand->card_count[OFC_BOTTOM];

    /* If hands are complete, check actual foul */
    if (top_count == OFC_TOP_CARDS &&
        middle_count == OFC_MIDDLE_CARDS &&
        bottom_count == OFC_BOTTOM_CARDS) {
        /* TODO: Use proper hand comparison from OFC library */
        /* For now, return 0 (assume no foul) */
        return 0.0;
    }

    /* If hands incomplete, estimate based on current state */
    /* Simple heuristic: return low probability if many cards left */
    int total_placed = top_count + middle_count + bottom_count;
    double progress = (double)total_placed / (double)OFC_TOTAL_CARDS;

    /* Foul risk increases as we place more cards */
    return progress * 0.2; /* Max 20% foul risk in this simple model */
}

/* ===== Main ISMCTS Search ===== */

int ofc_ismcts_find_best_placement(
    ofc_ismcts_solver_t* solver,
    const ofc_hand_t* my_hand,
    int card,
    const StdDeck_CardMask* unseen_cards,
    ofc_position_t* out_position
) {
    if (!solver || !my_hand || !unseen_cards || !out_position) {
        return -1;
    }

    /* Create root node for this decision */
    if (solver->root) {
        ofc_ismcts_node_free(solver->root);
    }

    int cards_placed = my_hand->card_count[OFC_TOP] +
                      my_hand->card_count[OFC_MIDDLE] +
                      my_hand->card_count[OFC_BOTTOM];

    solver->root = ofc_ismcts_node_create(my_hand, unseen_cards, cards_placed);
    if (!solver->root) {
        return -1;
    }

    /* ISMCTS main loop */
    for (int iter = 0; iter < solver->config.max_iterations; iter++) {
        ofc_ismcts_node_t* node = solver->root;

        /* Selection - traverse tree using UCT */
        while (node->num_children > 0 && !node->is_terminal) {
            ofc_ismcts_node_t* selected = ofc_ismcts_select_child(node, solver->config.uct_c);
            if (!selected) break;
            node = selected;
        }

        /* Expansion - if node not fully expanded, expand it */
        if (!node->is_terminal && node->num_children == 0) {
            ofc_ismcts_expand_node(solver, node, card);

            /* Select one child for simulation */
            if (node->num_children > 0) {
                node = ofc_ismcts_select_child(node, solver->config.uct_c);
            }
        }

        /* Simulation - rollout from this node */
        if (node) {
            double reward = ofc_ismcts_simulate(solver, node, card);

            /* Backpropagation - update statistics */
            ofc_ismcts_backpropagate(node, reward);

            solver->total_simulations++;
        }
    }

    /* Select best action based on visit counts */
    if (solver->root->num_children == 0) {
        return -1; /* No valid placements */
    }

    int best_child_idx = -1;
    int max_visits = -1;

    for (int i = 0; i < solver->root->num_children; i++) {
        ofc_ismcts_node_t* child = solver->root->children[i];
        if (child->is_pruned) continue;

        if (child->visit_count > max_visits) {
            max_visits = child->visit_count;
            best_child_idx = i;
        }
    }

    if (best_child_idx < 0) {
        return -1; /* All children pruned */
    }

    *out_position = solver->root->actions[best_child_idx].position;
    return 0;
}

/* ===== Statistics ===== */

void ofc_ismcts_get_stats(
    const ofc_ismcts_solver_t* solver,
    uint64_t* out_iterations,
    uint64_t* out_expansions,
    uint64_t* out_pruned
) {
    if (!solver) return;

    if (out_iterations) *out_iterations = solver->total_simulations;
    if (out_expansions) *out_expansions = solver->total_expansions;
    if (out_pruned) *out_pruned = solver->total_pruned_nodes;
}
