/*
 * ismcts.h - Information Set Monte Carlo Tree Search for OFC
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * ISMCTS (Information Set MCTS) for Open Face Chinese Poker.
 * Handles imperfect information (opponent's cards unknown) and explores
 * placement strategies with UCT selection, foul-aware pruning, and
 * efficient rollout policies.
 *
 * Reference:
 * - Cowling et al. (2012): "Information Set Monte Carlo Tree Search"
 * - Priority ★★★★☆ per plan section 4.1
 */

#ifndef POKER_EVAL_OFC_SOLVER_ISMCTS_H
#define POKER_EVAL_OFC_SOLVER_ISMCTS_H

#include <poker_eval/ofc/ofc.h>
#include <poker_eval/core/pokereval_export.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== ISMCTS Configuration ===== */

/* UCT exploration constant (typical range: √2 to 2.0) */
#define ISMCTS_DEFAULT_UCT_C 1.414  /* √2 */

/* Maximum simulation depth (placement turns) */
#define ISMCTS_DEFAULT_MAX_DEPTH 13

/* Default number of MCTS iterations per decision */
#define ISMCTS_DEFAULT_ITERATIONS 10000

/* Foul-aware pruning threshold (probability of foul below which branch is pruned) */
#define ISMCTS_DEFAULT_FOUL_THRESHOLD 0.85

/* ===== ISMCTS Node Structure ===== */

/* Placement action - where to place a card */
typedef struct {
    int card_index;         /* Index of card being placed */
    ofc_position_t position; /* OFC_TOP, OFC_MIDDLE, or OFC_BOTTOM */
} ofc_placement_t;

/* Information Set Node - represents a game state from current player's perspective */
typedef struct ofc_ismcts_node_t {
    /* State information */
    ofc_hand_t my_hand;              /* Current player's hand state */
    StdDeck_CardMask unseen_cards;   /* Cards not yet seen (deck + opponents) */
    int cards_placed;                /* Number of cards placed so far */

    /* MCTS statistics */
    double total_reward;             /* Sum of rewards from simulations */
    int visit_count;                 /* Number of times node was visited */

    /* Tree structure */
    struct ofc_ismcts_node_t* parent;  /* Parent node */
    struct ofc_ismcts_node_t** children; /* Array of child nodes */
    ofc_placement_t* actions;         /* Corresponding actions */
    int num_children;                 /* Number of children */
    int children_capacity;            /* Allocated capacity */

    /* Pruning metadata */
    double foul_probability;          /* Estimated probability of fouling */
    bool is_terminal;                 /* Is this a terminal node? */
    bool is_pruned;                   /* Pruned due to foul risk? */
} ofc_ismcts_node_t;

/* ===== ISMCTS Solver Configuration ===== */

typedef struct {
    /* UCT parameters */
    double uct_c;                    /* UCT exploration constant */

    /* Iteration budget */
    int max_iterations;              /* Max MCTS iterations per decision */
    int max_depth;                   /* Max simulation depth */

    /* Pruning settings */
    bool enable_foul_pruning;        /* Enable foul-aware pruning */
    double foul_threshold;           /* Foul probability threshold for pruning */

    /* Rollout policy */
    bool use_heuristic_rollout;      /* Use heuristic vs random rollout */

    /* Determinization settings */
    int num_determinizations;        /* Number of world samples per decision */

    /* Random seed */
    uint64_t random_seed;
} ofc_ismcts_config_t;

/* ===== ISMCTS Solver State ===== */

typedef struct {
    ofc_ismcts_config_t config;
    ofc_ismcts_node_t* root;

    /* Statistics */
    uint64_t total_simulations;
    uint64_t total_pruned_nodes;
    uint64_t total_expansions;

    /* Random state */
    uint64_t rng_state;
} ofc_ismcts_solver_t;

/* ===== Public API ===== */

/**
 * Create default ISMCTS configuration
 */
POKEREVAL_EXPORT ofc_ismcts_config_t ofc_ismcts_default_config(void);

/**
 * Initialize ISMCTS solver
 *
 * @param config  Solver configuration
 * @return Initialized solver, or NULL on failure
 */
POKEREVAL_EXPORT ofc_ismcts_solver_t* ofc_ismcts_create(const ofc_ismcts_config_t* config);

/**
 * Free ISMCTS solver and all associated memory
 *
 * @param solver  Solver to free
 */
POKEREVAL_EXPORT void ofc_ismcts_free(ofc_ismcts_solver_t* solver);

/**
 * Reset solver for a new decision
 *
 * @param solver  Solver to reset
 */
POKEREVAL_EXPORT void ofc_ismcts_reset(ofc_ismcts_solver_t* solver);

/**
 * Find best placement for a card using ISMCTS
 *
 * @param solver       ISMCTS solver
 * @param my_hand      Current player's hand state
 * @param card         Card to place
 * @param unseen_cards Cards not yet seen (deck + opponents' hands)
 * @param out_position Best position to place card (output)
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int ofc_ismcts_find_best_placement(
    ofc_ismcts_solver_t* solver,
    const ofc_hand_t* my_hand,
    int card,
    const StdDeck_CardMask* unseen_cards,
    ofc_position_t* out_position
);

/**
 * Get statistics from last search
 *
 * @param solver         ISMCTS solver
 * @param out_iterations Number of iterations performed (output)
 * @param out_expansions Number of node expansions (output)
 * @param out_pruned     Number of pruned nodes (output)
 */
POKEREVAL_EXPORT void ofc_ismcts_get_stats(
    const ofc_ismcts_solver_t* solver,
    uint64_t* out_iterations,
    uint64_t* out_expansions,
    uint64_t* out_pruned
);

/* ===== Internal Functions (for testing and advanced use) ===== */

/**
 * Create a new ISMCTS node
 */
POKEREVAL_EXPORT ofc_ismcts_node_t* ofc_ismcts_node_create(
    const ofc_hand_t* hand,
    const StdDeck_CardMask* unseen,
    int cards_placed
);

/**
 * Free ISMCTS node and all descendants
 */
POKEREVAL_EXPORT void ofc_ismcts_node_free(ofc_ismcts_node_t* node);

/**
 * UCT selection - choose best child using UCB1 formula
 *
 * @param node   Parent node
 * @param uct_c  UCT exploration constant
 * @return Best child node, or NULL if no valid children
 */
POKEREVAL_EXPORT ofc_ismcts_node_t* ofc_ismcts_select_child(
    ofc_ismcts_node_t* node,
    double uct_c
);

/**
 * Expand node - generate children for all valid placements
 *
 * @param solver Solver (for config and RNG)
 * @param node   Node to expand
 * @param card   Card being placed
 * @return Number of children created
 */
POKEREVAL_EXPORT int ofc_ismcts_expand_node(
    ofc_ismcts_solver_t* solver,
    ofc_ismcts_node_t* node,
    int card
);

/**
 * Simulate (rollout) - play out randomly/heuristically to terminal state
 *
 * @param solver Solver (for config and RNG)
 * @param node   Starting node
 * @param card   Card being placed
 * @return Estimated reward
 */
POKEREVAL_EXPORT double ofc_ismcts_simulate(
    ofc_ismcts_solver_t* solver,
    const ofc_ismcts_node_t* node,
    int card
);

/**
 * Backpropagate - update statistics up the tree
 *
 * @param node   Leaf node to start from
 * @param reward Reward to propagate
 */
POKEREVAL_EXPORT void ofc_ismcts_backpropagate(
    ofc_ismcts_node_t* node,
    double reward
);

/**
 * Estimate foul probability for a hand state
 *
 * @param hand   Hand state to evaluate
 * @param unseen Unseen cards
 * @return Estimated probability of fouling (0.0 to 1.0)
 */
POKEREVAL_EXPORT double ofc_ismcts_estimate_foul_probability(
    const ofc_hand_t* hand,
    const StdDeck_CardMask* unseen
);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_OFC_SOLVER_ISMCTS_H */
