/*
 * cfr_core.h - Core CFR (Counterfactual Regret Minimization) solver
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Generic CFR solver with vtable interface for different game types.
 */

#ifndef POKER_EVAL_CFR_CORE_H
#define POKER_EVAL_CFR_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFR_MAX_PLAYERS 8

/* Upper bound on the number of actions any game can expose at a node.
   Adapters must clamp their returned action count to this; the core
   allocates fixed arrays and alloca()s based on it. */
#define CFR_MAX_ACTIONS 16

/* Forward declarations */
typedef struct cfr_game_t cfr_game_t;
typedef struct cfr_storage_t cfr_storage_t;
typedef struct cfr_config_t cfr_config_t;
struct cfr_metrics_snapshot_t;
typedef struct cfr_metrics_buffer_t cfr_metrics_buffer_t;

typedef void (*cfr_monitor_fn)(int iteration,
                               cfr_game_t *game,
                               cfr_storage_t *storage,
                               void *user_data);

typedef void (*cfr_metrics_listener_fn)(const struct cfr_metrics_snapshot_t *snapshot, void *user);

/* Private implementation detail for storage */
typedef struct {
    uint64_t key;
    int n;          /* number of actions */
    double *regret; /* size n */
    double *avg;    /* size n */
    int used;
    double ev_sum;
    double ev_sq_sum;
    uint64_t ev_count;
} entry_t;

/* Hash-map based storage for regrets and average strategy */
struct cfr_storage_t {
    entry_t *tab;
    size_t cap;
    size_t used_count;
};

/* CFR game interface (vtable) */
struct cfr_game_t {
    /* Game-specific traverse function */
    double (*traverse)(
        cfr_game_t* game,
        cfr_storage_t* storage,
        uint64_t state_key,
        int player,
        double reach_p0,
        double reach_p1,
        void* user_data
    );

    /* Query current player to act (required for multi-player support) */
    int (*current_player)(
        cfr_game_t* game,
        uint64_t state_key,
        void* user_data
    );

    /* Get available actions at a state */
    int (*get_actions)(
        cfr_game_t* game,
        uint64_t state_key,
        int* out_actions,
        int max_actions,
        void* user_data
    );

    /* Apply action to state */
    uint64_t (*apply_action)(
        cfr_game_t* game,
        uint64_t state_key,
        int action,
        void* user_data
    );

    /* Check if terminal state */
    int (*is_terminal)(
        cfr_game_t* game,
        uint64_t state_key,
        void* user_data
    );

    /* Get utility at terminal state */
    double (*get_utility)(
        cfr_game_t* game,
        uint64_t state_key,
        int player,
        void* user_data
    );

    /* Game-specific data */
    void* game_data;
    void* initial_state;
    size_t state_size;
    int num_players;
};

/* CFR configuration */
struct cfr_config_t {
    int max_iterations;
    int checkpoint_interval;
    double convergence_threshold;
    int enable_dcfr;       /* Discounted CFR */
    double dcfr_alpha;     /* Regret discount (default 1.5) */
    double dcfr_beta;      /* Strategy discount (default 0) */
    double dcfr_gamma;     /* Avg strategy weight (default 2) */
    int enable_ecfr;       /* Exponential CFR */
    double ecfr_lambda;    /* ECFR temperature (default 1.0) */
    int enable_mccfvfp;    /* Flow value focusing */
    double mccfvfp_flow_pow; /* Flow exponent (default 1.0) */
    int enable_linear_avg; /* Linear averaging */
    int seed;              /* RNG seed for sampling */
    int progress_interval; /* Emit progress every N iterations (0 = disabled) */
    int trace_iterations;  /* Emit detailed per-iteration timing */
    const char *checkpoint_path; /* Where to write checkpoints */
    const char *resume_path;     /* Optional checkpoint to resume from */
    int checkpoint_final;        /* Save final state on exit */
    cfr_monitor_fn monitor_fn;
    void *monitor_user;
    int monitor_period;
    int metrics_interval;
    int metrics_history;
    int metrics_level;
    double metrics_bb_value;
    double metrics_mchips_scale;
    cfr_metrics_buffer_t *metrics_buffer;
    cfr_metrics_listener_fn metrics_fn;
    void *metrics_user;
    volatile sig_atomic_t *stop_flag;
};

typedef struct cfr_metrics_snapshot_t {
    int iteration;
    double elapsed_sec;
    double iteration_time_sec;
    long nodes_iteration;
    long long nodes_total;
    double nodes_per_sec;
    double iterations_per_sec;
    size_t infosets_total;
    double exploitability;
    double ev_mean;
    double ev_stddev;
    double mchips_per_sec;
    double bb_per_100;
    double volatility;
    int num_players;
    double player_ev[CFR_MAX_PLAYERS];
    double player_stddev[CFR_MAX_PLAYERS];
    double player_mchips[CFR_MAX_PLAYERS];
    double player_bb100[CFR_MAX_PLAYERS];
} cfr_metrics_snapshot_t;

/* CFR storage (hash-map based) */
cfr_storage_t* cfr_storage_create(void);
void cfr_storage_destroy(cfr_storage_t* storage);

/* Update regret for an infoset */
void cfr_storage_update_regret(
    cfr_storage_t* storage,
    uint64_t key,
    int n_actions,
    const double* regret_delta,
    double discount
);

/* Update average strategy for an infoset */
void cfr_storage_update_avg(
    cfr_storage_t* storage,
    uint64_t key,
    int n_actions,
    const double* strategy,
    double weight
);

/* Get current strategy (regret matching) */
void cfr_storage_get_strategy(
    cfr_storage_t* storage,
    uint64_t key,
    int n_actions,
    double* out_strategy
);

/* Configure strategy extraction mode (called by solver) */
void cfr_storage_set_strategy_mode(int use_ecfr, double ecfr_lambda);

int cfr_storage_save_checkpoint(
    cfr_storage_t* storage,
    const char* path,
    uint64_t iteration
);

int cfr_storage_load_checkpoint(
    cfr_storage_t* storage,
    const char* path,
    uint64_t* out_iteration
);

/* Get average strategy */
void cfr_storage_get_avg_strategy(
    cfr_storage_t* storage,
    uint64_t key,
    int n_actions,
    double* out_avg_strategy
);

int cfr_storage_has_entry(
    cfr_storage_t* storage,
    uint64_t key
);

int cfr_storage_peek_avg_strategy(
    cfr_storage_t* storage,
    uint64_t key,
    int n_actions,
    double* out_avg_strategy
);

/* Count infosets in storage */
size_t cfr_storage_count_infosets(cfr_storage_t* storage);

/* Dump average strategies to CSV file */
void cfr_storage_dump_avg(cfr_storage_t *s, FILE *f);

/* Accumulate expected value for an infoset */
void cfr_storage_accumulate_ev(cfr_storage_t *s, uint64_t infoset, double node_ev);

/* Iterate over all infosets with stats */
typedef void (*cfr_storage_iter_stats_fn)(
    uint64_t key,
    int n_actions,
    const double* regret,
    const double* avg_strategy,
    double ev_sum,
    double ev_sq_sum,
    uint64_t sample_count,
    void* user_data
);

void cfr_storage_iterate_stats(
    cfr_storage_t* storage,
    cfr_storage_iter_stats_fn callback,
    void* user_data
);

int cfr_storage_get_ev_stats(
    cfr_storage_t* storage,
    uint64_t key,
    double* out_mean,
    double* out_stddev,
    uint64_t* out_count
);

cfr_metrics_buffer_t *cfr_metrics_buffer_create(int capacity);
void cfr_metrics_buffer_destroy(cfr_metrics_buffer_t *buffer);
void cfr_metrics_buffer_clear(cfr_metrics_buffer_t *buffer);
int cfr_metrics_buffer_count(cfr_metrics_buffer_t *buffer);
int cfr_metrics_buffer_get_latest(cfr_metrics_buffer_t *buffer, cfr_metrics_snapshot_t *out_snapshot);
int cfr_metrics_buffer_get(cfr_metrics_buffer_t *buffer, int index_from_newest, cfr_metrics_snapshot_t *out_snapshot);


/* Iterate over all infosets */
typedef void (*cfr_iterate_callback)(
    uint64_t key,
    int n_actions,
    const double* regret,
    const double* avg_strategy,
    void* user_data
);

void cfr_storage_iterate(
    cfr_storage_t* storage,
    cfr_iterate_callback callback,
    void* user_data
);

/* CFR solver */
double cfr_solve(
    cfr_game_t* game,
    cfr_storage_t* storage,
    const cfr_config_t* config,
    double* out_exploitability
);

/* Best response value (for exploitability calculation) */
double cfr_best_response_value(
    cfr_game_t* game,
    cfr_storage_t* storage,
    int player,
    void* user_data
);

/* Calculate exploitability proxy */
double cfr_exploitability_proxy(
    cfr_game_t* game,
    cfr_storage_t* storage,
    void* user_data
);

/**
 * Compute the expected value (policy value) of the average strategy
 *
 * This function computes the expected value for a given player when both
 * players follow the computed average strategy. It traverses the game tree
 * from the root and computes the weighted sum of terminal utilities.
 *
 * @param game      Game interface with vtable
 * @param storage   CFR storage containing average strategies
 * @param player    Player index (0 or 1) to compute value for
 * @param user_data Optional game-specific data (passed to game callbacks)
 * @return Expected value for the specified player under the average strategy
 */
double cfr_compute_policy_value(
    cfr_game_t* game,
    cfr_storage_t* storage,
    int player,
    void* user_data
);

/**
 * Policy value computation result structure
 *
 * Contains detailed results from policy value computation including
 * per-player expected values and variance estimates.
 */
typedef struct cfr_policy_value_result_t {
    double ev[CFR_MAX_PLAYERS];        /* Expected value per player */
    double variance[CFR_MAX_PLAYERS];   /* Variance estimate per player */
    double std_dev[CFR_MAX_PLAYERS];    /* Standard deviation per player */
    int num_players;                    /* Number of players */
    size_t nodes_visited;               /* Total nodes visited in traversal */
    double reach_sum;                   /* Sum of reach probabilities (sanity check) */
} cfr_policy_value_result_t;

/**
 * Compute detailed policy values for all players
 *
 * Extended version that computes expected values for all players at once
 * and provides variance/std_dev estimates for analysis.
 *
 * @param game      Game interface with vtable
 * @param storage   CFR storage containing average strategies
 * @param user_data Optional game-specific data
 * @param out_result Output structure with detailed results (must not be NULL)
 * @return 0 on success, -1 on error
 */
int cfr_compute_policy_values_detailed(
    cfr_game_t* game,
    cfr_storage_t* storage,
    void* user_data,
    cfr_policy_value_result_t* out_result
);

/* ============================================================================
 * MULTIWAY BEST-RESPONSE AND EXPLOITABILITY
 * ============================================================================ */

/**
 * Best response value for multiway games (N players)
 *
 * Computes the expected value for a player when they play a best response
 * strategy while all other players follow their average strategies from storage.
 * This is an extension of cfr_best_response_value that supports N>2 players.
 *
 * @param game      Game interface with vtable (must have current_player callback)
 * @param storage   CFR storage containing average strategies
 * @param player    Player index to compute best response for
 * @param user_data Optional game-specific data
 * @return Expected value for the BR player, or 0.0 on error
 */
double cfr_best_response_value_multiway(
    cfr_game_t* game,
    cfr_storage_t* storage,
    int player,
    void* user_data
);

/**
 * Exploitability result structure for multiway games
 */
typedef struct cfr_exploitability_result_t {
    int num_players;
    double br_value[CFR_MAX_PLAYERS];       /* Best response value per player */
    double policy_value[CFR_MAX_PLAYERS];   /* Policy (avg strategy) value per player */
    double exploitability[CFR_MAX_PLAYERS]; /* Per-player exploitability */
    double total_exploitability;            /* Sum of all exploitabilities */
    double nash_distance;                   /* Distance to Nash equilibrium estimate */
} cfr_exploitability_result_t;

/**
 * Calculate exploitability for multiway games
 *
 * Computes the exploitability for each player and overall exploitability.
 * For 2 players, exploitability is BR_value[0] + BR_value[1].
 * For N players, it's the sum of (BR_value[i] - policy_value[i]) for all i.
 *
 * @param game      Game interface
 * @param storage   CFR storage
 * @param user_data Optional game-specific data
 * @param out_result Output structure (must not be NULL)
 * @return 0 on success, -1 on error
 */
int cfr_exploitability_multiway(
    cfr_game_t* game,
    cfr_storage_t* storage,
    void* user_data,
    cfr_exploitability_result_t* out_result
);

/**
 * Print exploitability result (debug)
 */
void cfr_exploitability_print(const cfr_exploitability_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_CORE_H */
