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

#include <poker_eval/solver/pe_telemetry.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFR_MAX_PLAYERS 8

/* Upper bound on the number of actions any game exposes at a node. */
#define CFR_MAX_ACTIONS 16

/* Default recursion depth limit for tree walks; a value of 0 in
   cfr_config_t::max_depth selects this default. */
#define CFR_DEFAULT_MAX_DEPTH 1000

/* Street values used by selective CFR memory retention.  They intentionally
 * match mpf_street_t, while keeping the core solver independent of MPF. */
#define CFR_STREET_PREFLOP 0
#define CFR_STREET_FLOP 1
#define CFR_STREET_TURN 2
#define CFR_STREET_RIVER 3
#define CFR_STREET_SHOWDOWN 4
/* Forward declarations */
typedef struct cfr_game_t cfr_game_t;
typedef struct cfr_storage_t cfr_storage_t;
typedef struct cfr_config_t cfr_config_t;
struct cfr_metrics_snapshot_t;
typedef struct cfr_metrics_buffer_t cfr_metrics_buffer_t;

/* ============================================================================
 * Generic Terminal Utility Abstraction (ISSUE-14, #170)
 *
 * Standard poker solvers evaluate terminal nodes with linear chip deltas.  Real
 * solving needs non-linear, non-zero-sum payoffs: tournament equity (ICM),
 * rake / uncalled-bet adjustments, or risk-averse utility functions.  Rather than
 * hard-coding payout formulas (Malmuth-Harville, rake rules, ...) inside the CFR
 * core, terminal evaluation is delegated to an optional pe_utility_fn callback,
 * leaving the individual feature engines (ICM, rake, risk profiles) to be built
 * on top of this generic layer.
 *
 * When a pe_utility_fn is configured on a game that also implements the
 * cfr_game_t::get_final_stacks callback, the core derives the players' final
 * stacks once and produces every player's payoff via
 * utility_fn(final_stacks, num_players, player, user_data).  When utility_fn is
 * NULL the solver performs exactly the legacy get_utility-per-player evaluation
 * with zero overhead, so default linear-chip behaviour is 100% backward
 * compatible.
 * ============================================================================ */
typedef double (*pe_utility_fn)(const int32_t *final_stacks, int num_players,
                                int player_id, void *user_data);

typedef struct {
    pe_utility_fn utility_fn; /* Custom payoff fn (NULL = default linear chips) */
    void *user_data;          /* Opaque context passed to utility_fn */
    int is_non_linear;        /* Hint for leaf caching / regret accumulation */
} pe_cfr_utility_config_t;

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
    double *locked; /* size n, NULL when not locked */
    int used;
    /* Periodic relock EV-loss measurement (FEAT-11). The loss is accumulated
     * counterfactually reach-weighted across every state that maps to this
     * infoset during a single relock traversal, so a poker infoset (many
     * states -> one key) reports an aggregate rather than the last-visited
     * state. lock_ev_num/den hold the weighted sum of (br - forced) and the
     * total reach; lock_br_num holds the weighted sum of the per-state
     * best-action value. The reported loss is num/den, br is br_num/den, and
     * forced = br - loss. */
    double lock_ev_num;
    double lock_ev_den;
    double lock_br_num;
    int lock_ev_valid;
    double ev_sum;
    double ev_sq_sum;
    uint64_t ev_count;
} entry_t;

/* Hash-map based storage for regrets and average strategy */
struct cfr_storage_t {
    entry_t *tab;
    size_t cap;
    size_t used_count;
    uint32_t keep_avg_strategy_mask;
    uint32_t keep_ev_mask;
    /* Strategy extraction mode (EXT-01). Held per storage rather than in a
     * process-wide static: the previous global made the solver non-reentrant,
     * so two solves in one process silently shared an ECFR temperature. */
    int use_ecfr;
    double ecfr_lambda;
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

    /* Return a stable infoset key for storage (optional). This is required
       when state_key is a temporary heap pointer that release_state frees. */
    uint64_t (*get_infoset_key)(const void* state);

    /* Optional street for selective average-strategy/EV retention. Return a
       CFR_STREET_* value, or a negative value when unknown. */
    int (*get_street)(cfr_game_t* game, uint64_t state_key, void* user_data);

    /* Release a state returned by apply_action (optional; may be NULL for
       games that do not allocate per-state heap storage). */
    void (*release_state)(
        cfr_game_t* game,
        uint64_t state_key,
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

    /* Optional: fill `out_stacks[0..num_players-1]` with each player's final
     * stack at a terminal node (ISSUE-14, #170).  Only consulted when
     * cfr_game_t::utility.utility_fn is configured: terminal payoffs are then
     * computed generically via utility_fn(final_stacks, ...) with the stack
     * vector derived once per terminal node (memoized) instead of once per
     * player.  May be NULL: when NULL, terminal evaluation falls back to
     * get_utility. */
    int (*get_final_stacks)(
        cfr_game_t* game,
        uint64_t state_key,
        int32_t* out_stacks,
        void* user_data
    );

    /* Chance node support (optional). When is_chance returns nonzero, the
     * state has get_chance_outcomes() outcomes, dealt one by one through
     * apply_chance(). All three callbacks are optional: a game that leaves
     * them NULL is treated as having no chance nodes.
     *
     * Outcomes are equally likely unless get_chance_weight is provided
     * (FEAT-14, #150): it returns a non-negative weight for outcome index
     * `outcome` (0..get_chance_outcomes()-1), and the solver normalizes the
     * weights of the current state before averaging. A NULL callback (or a
     * weight <= 0) behaves exactly like the uniform case. */
    int (*is_chance)(
        cfr_game_t* game,
        uint64_t state_key,
        void* user_data
    );
    int (*get_chance_outcomes)(
        cfr_game_t* game,
        uint64_t state_key,
        void* user_data
    );
    double (*get_chance_weight)(
        cfr_game_t* game,
        uint64_t state_key,
        int outcome,
        void* user_data
    );
    uint64_t (*apply_chance)(
        cfr_game_t* game,
        uint64_t state_key,
        int outcome,
        void* user_data
    );

    /* Game-specific data */
    void* game_data;
    void* initial_state;
    size_t state_size;
    int num_players;

    /* Generic terminal utility override (ISSUE-14, #170).  NULL utility_fn keeps
     * the legacy linear get_utility evaluation with zero overhead. */
    pe_cfr_utility_config_t utility;
};

/* CFR configuration */
struct cfr_config_t {
    int max_iterations;
    int max_depth;         /* Max tree recursion depth (0 = CFR_DEFAULT_MAX_DEPTH) */
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

    /* Optional default terminal utility config (ISSUE-14, #170).  Applied to the
     * game at the start of cfr_solve when the game itself has not set a
     * cfr_game_t::utility explicitly. */
    pe_cfr_utility_config_t utility;
    cfr_monitor_fn monitor_fn;
    void *monitor_user;
    int monitor_period;
    int metrics_interval;
    int metrics_history;
    int exploitability_interval; /* Full best-response exploitability every N
                                  * iterations (0 = disabled; also drives the
                                  * periodic convergence check). */
    uint32_t keep_avg_strategy_mask; /* 0 = retain every street */
    uint32_t keep_ev_mask;           /* 0 = retain every street */

    /* Postflop abstraction (FEAT-13): street-by-street node abstraction in the
     * style of MonkerSolver. strength_buckets_per_street is the target number
     * of strength buckets (the EHS/EHS2 k-means count from strength_bucketing.h)
     * used per street; texture_filter_level selects how aggressively boards are
     * merged by texture (Perfect / Large / Medium / Small / None, see
     * board_texture.h). When 0, the strength bucketing abstraction is disabled
     * and the solver falls back to its existing coarse strength binning. */
    int strength_buckets_per_street;          /* 0 = disabled */
    int texture_filter_level;                 /* pe_texture_filter_level_t */

    /* Periodic relocking engine (FEAT-11).
     *
     * When enable_periodic_relock is set, locked infosets are NOT frozen:
     * CFR keeps running there (regret/average strategy keep accumulating for
     * the non-locked actions), and every lock_period iterations the current
     * strategy is re-enforced to the locked target frequencies. This makes
     * target frequencies converge instantly while the un-locked actions keep
     * true (bounty-free) best-response EVs. The exact EV loss forced by the
     * sub-optimal lock is recorded per infoset via
     * cfr_storage_get_lock_ev_loss(). lock_period <= 0 disables relocking
     * inside this mode, which then degrades to the freeze behaviour of #118. */
    int enable_periodic_relock;
    int lock_period;

    int metrics_level;
    double metrics_bb_value;
    double metrics_mchips_scale;
    cfr_metrics_buffer_t *metrics_buffer;
    cfr_metrics_listener_fn metrics_fn;
    void *metrics_user;
    volatile sig_atomic_t *stop_flag;

    /* Where the solver's messages go (EXT-03). NULL keeps the historical
     * behaviour — every line on stderr, flushed — so an existing caller sees
     * no change. A host that wants to capture progress, route it to a UI or
     * silence it installs its own adapter instead of redirecting a stream. */
    const pe_telemetry_ops_t *telemetry;
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

/*
 * Configure strategy extraction for one storage (EXT-01).
 *
 * `use_ecfr` selects the exponential (ECFR) policy over regret matching, and
 * `ecfr_lambda` is its temperature; a non-positive lambda is clamped to 1.0.
 * The setting belongs to the storage, so two solves running in one process
 * cannot disturb each other.
 */
void cfr_storage_set_strategy_mode_for(cfr_storage_t *storage,
                                       int use_ecfr,
                                       double ecfr_lambda);

/*
 * Deprecated (EXT-01): the process-wide variant. It has no storage to act on
 * and is now a no-op. Call cfr_storage_set_strategy_mode_for() instead.
 *
 * Kept so that out-of-tree callers still link, and marked deprecated so they
 * find out at compile time rather than by silently losing their setting.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((deprecated("use cfr_storage_set_strategy_mode_for(storage, ...)")))
#endif
void cfr_storage_set_strategy_mode(int use_ecfr, double ecfr_lambda);
void cfr_storage_set_memory_masks(cfr_storage_t *storage,
                                  uint32_t keep_avg_strategy_mask,
                                  uint32_t keep_ev_mask);

void cfr_storage_get_strategy_at_street(cfr_storage_t*, uint64_t, int, int, double*);
void cfr_storage_update_regret_at_street(cfr_storage_t*, uint64_t, int, int, const double*, double);
void cfr_storage_update_avg_at_street(cfr_storage_t*, uint64_t, int, int, const double*, double);
void cfr_storage_get_avg_strategy_at_street(cfr_storage_t*, uint64_t, int, int, double*);
void cfr_storage_get_regret_strategy_at_street(cfr_storage_t*, uint64_t, int, int, double*);
void cfr_storage_overwrite_avg_at_street(cfr_storage_t*, uint64_t, int, int, const double*);
void cfr_storage_accumulate_ev_at_street(cfr_storage_t*, uint64_t, int, double);

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

/*
 * Lock an infoset to a fixed strategy. While locked, the solver uses these
 * probabilities for the descent and never updates regret or average strategy
 * at this infoset, so the exported (average) strategy stays exactly the lock.
 * The infoset's regret is zeroed and the average strategy is pinned to the
 * lock so best-response and export computations see it from the start.
 * `probs` should sum to 1.
 */
int cfr_storage_set_locked_strategy(
    cfr_storage_t* storage,
    uint64_t infoset,
    const double* probs,
    int n_actions
);

/*
 * Get the locked strategy for an infoset, if any.
 * Returns 1 and sets *out_probs to the stored array when locked, 0 otherwise.
 */
int cfr_storage_get_locked_strategy(
    cfr_storage_t* storage,
    uint64_t infoset,
    int n_actions,
    const double** out_probs
);

/*
 * Periodic relocking EV-loss measurement (FEAT-11).
 *
 * While the periodic relock engine runs, the solver accumulates, over each
 * relock traversal, the EV loss at a locked infoset: the counterfactually
 * reach-weighted gap between the value the locked player could obtain by
 * playing its best response and the value it obtains while being forced to the
 * locked frequencies. Because no synthetic regret bounty is injected, this is
 * a bounty-free (exact in the no-distortion sense) cost of the lock.
 *
 * The best-response value is fully recursive: for each action the solver
 * recomputes the child subtree's best response for the locked player
 * (opponents follow their average strategy, the locked player maximizes at
 * every downstream decision), so the loss isolates the cost of the forced mix
 * at THIS infoset only, with the player free everywhere below.
 *
 * The loss is aggregated across every state that maps to the infoset (a poker
 * infoset has many states -> one key), weighted by the acting player's
 * counterfactual reach, so the reported value reflects the whole infoset
 * rather than whichever state was traversed last.
 *
 * cfr_storage_begin_lock_ev_pass() resets the accumulators for all locked
 * infosets and is called once by the solver at the start of each relock
 * iteration. cfr_storage_record_lock_ev_loss() adds one state's weighted
 * contribution. cfr_storage_get_lock_ev_loss() returns the aggregated loss for
 * an infoset (0 / not-valid when none recorded). Pass out_br and out_forced to
 * also receive the weighted best-action value and the forced value. Any of the
 * out_* pointers may be NULL.
 */
void cfr_storage_begin_lock_ev_pass(cfr_storage_t* storage);

void cfr_storage_record_lock_ev_loss(
    cfr_storage_t* storage,
    uint64_t infoset,
    double br_value,
    double forced_value,
    double reach_weight
);

int cfr_storage_get_lock_ev_loss(
    cfr_storage_t* storage,
    uint64_t infoset,
    double* out_loss,
    double* out_br,
    double* out_forced
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

/*
 * Scale every accumulated regret by `factor` (EXT-07).
 *
 * Used to apply a discount once per iteration, before that iteration's deltas
 * are accumulated: R_t = R_(t-1) * d(t) + r_t. A factor of 1.0 returns
 * immediately.
 */
/*
 * Writable spans of an infoset's arrays (STO-04), creating the entry when
 * absent. Returns NULL when the array does not exist — the average is dropped
 * on streets excluded by the selective-memory masks.
 *
 * The pointer is invalidated by the next call that grows this storage.
 */
double *cfr_storage_regret_span(cfr_storage_t *storage, uint64_t key, int n_actions);
double *cfr_storage_avg_span(cfr_storage_t *storage, uint64_t key, int n_actions);

void cfr_storage_scale_regrets(cfr_storage_t *storage, double factor);

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

/* Generic terminal utility configuration (ISSUE-14, #170).
 *
 * Installs `utility_config` on the game so all subsequent terminal evaluations
 * (main CFR walk, best response / exploitability, policy values) route through
 * utility_config.utility_fn when the game also implements get_final_stacks.
 * Passing a config with utility_fn == NULL restores the legacy linear-chip
 * evaluation.
 *
 * @return 0 on success, -1 when game is NULL.
 */
int pe_cfr_set_utility_function(cfr_game_t* game,
                                pe_cfr_utility_config_t utility_config);

/* Chance-outcome weight (FEAT-14, #150): returns the weight of outcome index
 * `outcome` at `state_key`, or 1.0 when the game provides no get_chance_weight
 * callback (uniform outcomes). Solver walks feed this into the normalized
 * chance-node average, so games can skew deal probabilities (e.g. card
 * bunching) without duplicating the normalization logic at every call site. */
double cfr_chance_weight(
    cfr_game_t* game,
    uint64_t state_key,
    int outcome,
    void* user_data
);

/**
 * Compute a perfect-information best-response value.
 *
 * The maximizing player may choose a different action at every concrete
 * state, including states that share an information set. In an imperfect-
 * information game this is therefore an upper bound, not the legal
 * information-set best response.
 */
double cfr_best_response_perfect_info(
    cfr_game_t* game,
    cfr_storage_t* storage,
    int player,
    void* user_data
);

/*
 * Compute exact 2-player perfect-information best-response exploitability.
 *
 * This walks the game tree once per player and returns the sum of the
 * perfect-information best-response values, so it is expensive and should be
 * run on a configurable period (see cfr_config_t::exploitability_interval)
 * rather than on every iteration. For imperfect-information games it remains
 * an upper bound at equilibrium because the BR can observe hidden state.
 * For N-player games use cfr_exploitability_multiway instead.
 */
double cfr_exploitability_perfect_info(
    cfr_game_t* game,
    cfr_storage_t* storage,
    void* user_data
);

/** Compatibility alias for cfr_best_response_perfect_info().
 * Deprecated: new code should use the explicit perfect-information name. */
double cfr_best_response_value(
    cfr_game_t* game,
    cfr_storage_t* storage,
    int player,
    void* user_data
);

/** Compatibility alias for cfr_exploitability_perfect_info().
 * Deprecated: new code should use the explicit perfect-information name. */
double cfr_exploitability(
    cfr_game_t* game,
    cfr_storage_t* storage,
    void* user_data
);

/**
 * Compute an information-set-consistent best response.
 *
 * Unlike cfr_best_response_perfect_info(), the selected action is shared by
 * all concrete states in the same information set. The implementation uses
 * counterfactual reach and iterates the infoset action choices to a fixed
 * point. This is the legal exploitability measure for imperfect-information
 * games.
 */
double cfr_best_response_value_infoset(
    cfr_game_t* game,
    cfr_storage_t* storage,
    int player,
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
 * This is the multiway extension of the perfect-information best response and
 * supports N>2 players.
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

/* Multiway stability audit (ISSUE-15, #171). The CCE gap is the largest
 * unilateral deviation gain; total_exploitability remains the NashConv sum
 * exposed by cfr_exploitability_multiway(). */
typedef struct cfr_multiway_audit_result_t {
    int num_players;
    double cce_gap;
    double max_player_exploitability[CFR_MAX_PLAYERS];
    double total_pot_ev_imbalance;
    int has_collusive_ev_transfer;
    int has_nonfinite_metrics;
} cfr_multiway_audit_result_t;

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

/* Audit the current multiway average strategy. The conservation flag is a
 * diagnostic heuristic: in a zero-sum chip game, a non-zero total policy EV
 * indicates payoff leakage or an apparent transfer that needs investigation. */
int cfr_audit_multiway(
    cfr_game_t *game,
    cfr_storage_t *storage,
    void *user_data,
    cfr_multiway_audit_result_t *out_result
);

/**
 * Print exploitability result (debug)
 */
void cfr_exploitability_print(const cfr_exploitability_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_CORE_H */
