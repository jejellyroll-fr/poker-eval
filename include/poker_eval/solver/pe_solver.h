/*
 * pe_solver.h - Public solver lifecycle (architecture v3, CTR-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Primary driving port of the hexagonal solver described in
 * docs/project/SOLVER_ARCHITECTURE_V3.md. Everything a caller needs to run a
 * solve goes through this header: configuration in, lifecycle control, results
 * out. The domain behind it knows nothing about CUDA, OpenCL, the filesystem
 * or stderr — those reach it as driven adapters injected via pe_solver_deps_t.
 *
 * This is the CTR-01 skeleton: the contract and its granularity are fixed here
 * and every entry point is present, but the implementations are stubs that
 * report PE_SOLVER_ERR_NOT_IMPLEMENTED. The types referenced by the prototypes are
 * forward-declared and completed by later tickets:
 *
 *   pe_solver_config_t     CTR-03  (pe_solver_config.h)
 *   pe_solver_deps_t       CTR-04  (pe_ports.h)
 *   pe_execution_plan_t    CTR-06  (pe_solver_plan.h)
 *   pe_diagnostics_t       CTR-06  (pe_solver_plan.h)
 *   pe_estimate_t          STO-05
 *   pe_progress_t          API-01
 *   pe_metrics_t           API-01
 *   pe_strategy_query_t    API-01
 *   pe_strategy_view_t     API-01
 *   pe_persist_target_t    API-04  (pe_persist.h)
 *   pe_persist_source_t    API-04  (pe_persist.h)
 *
 * Each of those headers includes this one and defines the struct body for the
 * tag declared here, so a typedef is never repeated (C99 forbids it).
 */

#ifndef POKER_EVAL_PE_SOLVER_H
#define POKER_EVAL_PE_SOLVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Status codes
 * ------------------------------------------------------------------ */

/*
 * Every entry point that can fail reports a pe_solver_status_t. PE_SOLVER_OK is
 * zero so `if (pe_solver_run(s) != PE_SOLVER_OK)` reads the way callers expect.
 *
 * Named pe_solver_status_t rather than pe_status_t because that name is taken
 * twice already: <poker_eval/core/status.h> and <poker_eval/range.h> each
 * define a different enum under it, reconciled by a PE_STATUS_T_DEFINED guard
 * where the first include wins. Adding a third would have made including this
 * header alongside either of those a hard error — which is exactly what
 * happened the moment the solver needed to parse a range.
 *
 * This set covers exactly what the CTR-01 skeleton can return. Later tickets
 * extend it (invalid configuration, budget overrun, I/O failure, ...) by
 * appending values; existing values never change, since they cross the public
 * ABI.
 */
typedef enum {
    PE_SOLVER_OK = 0,
    PE_SOLVER_ERR_NULL_ARGUMENT,   /* a required pointer argument was NULL */
    PE_SOLVER_ERR_INVALID_STATE,   /* the call is not legal in the solver's state */
    PE_SOLVER_ERR_OUT_OF_MEMORY,   /* allocation failed */
    PE_SOLVER_ERR_NOT_IMPLEMENTED, /* the entry point exists but has no body yet */
    PE_SOLVER_ERR_INVALID_CONFIG,  /* the configuration cannot be honoured */
    PE_SOLVER_ERR_BUDGET_EXCEEDED, /* the estimate does not fit max_ram_bytes */
    PE_SOLVER_ERR_EXECUTION        /* an injected execution port failed */
} pe_solver_status_t;

/* ------------------------------------------------------------------ *
 * Forward declarations
 * ------------------------------------------------------------------ */

/* The solver instance is opaque: callers hold a pointer and nothing else. */
typedef struct pe_solver_t pe_solver_t;

typedef struct pe_solver_config_t   pe_solver_config_t;
typedef struct pe_solver_deps_t     pe_solver_deps_t;
typedef struct pe_execution_plan_t  pe_execution_plan_t;
typedef struct pe_diagnostics_t     pe_diagnostics_t;
typedef struct pe_estimate_t        pe_estimate_t;
typedef struct pe_progress_t        pe_progress_t;
typedef struct pe_metrics_t         pe_metrics_t;
typedef struct pe_strategy_query_t  pe_strategy_query_t;
typedef struct pe_strategy_view_t   pe_strategy_view_t;
typedef struct pe_persist_target_t  pe_persist_target_t;
typedef struct pe_persist_source_t  pe_persist_source_t;

#define PE_SOLVER_MAX_PLAYERS 8u

/**
 * What a reported exploitability measurement actually guarantees.
 *
 * UNSPECIFIED is intentional: a raw BR conversion has no game topology from
 * which to infer a guarantee and must not accidentally look like Nash.
 */
typedef enum {
    PE_GUARANTEE_UNSPECIFIED = 0,
    PE_GUARANTEE_NASH,
    PE_GUARANTEE_NO_REGRET_ONLY,
    PE_GUARANTEE_EMPIRICAL
} pe_guarantee_t;

/* BR-03/05 metrics are available before the full API-01 lifecycle is wired. */
struct pe_metrics_t {
    double exploitability_raw;
    double exploitability_mbb_per_game;
    double big_blind;

    pe_guarantee_t guarantee;
    uint8_t num_players;
    double br_gap[PE_SOLVER_MAX_PLAYERS];
    double cce_gap;
    double utility_imbalance;
};

/** Snapshot of lifecycle progress; values are stable for one call. */
struct pe_progress_t {
    uint64_t iteration;
    uint64_t total_iterations;
    double fraction;
    int running;
    int paused;
    int complete;
};

/** Identifies one infoset strategy query. */
struct pe_strategy_query_t {
    uint32_t infoset;
};

/** Borrowed view returned by a strategy query; valid until the next query. */
struct pe_strategy_view_t {
    const double *values;
    size_t count;
    uint16_t action_count;
    uint16_t combo_count;
};

/* ------------------------------------------------------------------ *
 * Lifecycle
 * ------------------------------------------------------------------ */

/**
 * Create a solver instance.
 *
 * @param cfg   Resolved configuration. Copied by the solver; the caller keeps
 *              ownership and may release it on return.
 * @param deps  Driven adapters (compute, evaluator, storage, persistence,
 *              telemetry). May be NULL, in which case the solver installs its
 *              default adapters. The pointed-to adapters must outlive the
 *              solver.
 * @return The instance, or NULL on failure.
 */
pe_solver_t *pe_solver_create(const pe_solver_config_t *cfg,
                              const pe_solver_deps_t *deps);

/**
 * Release a solver instance and everything it owns. Safe on NULL. A running
 * solve is stopped first.
 */
void pe_solver_destroy(pe_solver_t *solver);

/* ------------------------------------------------------------------ *
 * Introspection of what was actually installed
 * ------------------------------------------------------------------ */

/**
 * The configuration the solver is running, as copied at creation.
 *
 * The solver owns this copy, so it is unaffected by anything the caller does
 * to theirs after pe_solver_create returns. Valid until pe_solver_destroy.
 *
 * @return The configuration, or NULL when `solver` is NULL.
 */
const pe_solver_config_t *pe_solver_get_config(const pe_solver_t *solver);

/* ------------------------------------------------------------------ *
 * Validation and introspection — no solve, no allocation of solve memory
 * ------------------------------------------------------------------ */

/**
 * Check the configuration against the capability matrix without running
 * anything. A configuration that cannot be honoured is rejected here rather
 * than silently downgraded.
 *
 * @param out  Receives the diagnostics (errors, warnings, fallbacks). May be
 *             NULL when the caller only wants the status.
 */
pe_solver_status_t pe_solver_validate(const pe_solver_t *solver,
                               pe_diagnostics_t *out);

/** Capability bits the resolved plan actually provides. */
pe_solver_status_t pe_solver_capabilities(const pe_solver_t *solver,
                                   uint64_t *out_caps);

/*
 * What a solve would cost, before committing to it.
 *
 * Every figure is derived from the resolved plan and the declared problem
 * size; nothing here allocates or runs. The point of the structure is that
 * the parts are visible: a caller who is 30% over budget wants to know
 * whether it is the value arrays or the scratch, because only one of those
 * responds to abstraction.
 */
struct pe_estimate_t {
    /** Inputs echoed back, so a printed estimate stands on its own. */
    uint64_t infosets;
    uint64_t slots;              /**< infosets * actions * combos */
    uint32_t bytes_per_slot;     /**< from the resolved precision */
    uint32_t value_arrays;       /**< how many the plan will keep */

    /** Value arrays, metadata and the key map. */
    uint64_t storage_bytes;
    /** Traversal scratch and the update batches. */
    uint64_t scratch_bytes;
    /** storage + scratch. What max_ram_bytes is compared against. */
    uint64_t host_bytes;
    /** 0 unless a stage was resolved onto a device. */
    uint64_t device_bytes;

    /** The budget in force, echoed; 0 when none was declared. */
    uint64_t budget_bytes;
    /** Nonzero when host_bytes fits the budget, or there is none. */
    int within_budget;
};

/**
 * Estimate the resources a solve would consume before committing to it.
 *
 * Memory is a first-class input of the configuration, not a consequence
 * discovered at run time: pe_solver_validate() refuses a plan whose estimate
 * exceeds pe_execution_config_t::max_ram_bytes, before anything is allocated.
 *
 * Returns PE_SOLVER_ERR_INVALID_CONFIG when the declared problem size is empty —
 * estimating nothing is not an estimate of zero.
 */
pe_solver_status_t pe_solver_estimate(const pe_solver_t *solver,
                               pe_estimate_t *out);

/**
 * The resolved, immutable execution plan: traversal, regret, averaging and
 * pruning ops, effective backend per stage, precision, capabilities. Exposed
 * so a caller can print what will actually run instead of what was asked for.
 */
pe_solver_status_t pe_solver_plan(const pe_solver_t *solver,
                           pe_execution_plan_t *out);

/* ------------------------------------------------------------------ *
 * Execution
 * ------------------------------------------------------------------ */

/** Run until a stop condition is met. Blocking. */
pe_solver_status_t pe_solver_run(pe_solver_t *solver);

/** Request a pause at the next safe point. Idempotent. */
pe_solver_status_t pe_solver_pause(pe_solver_t *solver);

/** Resume a paused solve. Idempotent. */
pe_solver_status_t pe_solver_resume(pe_solver_t *solver);

/** Request a graceful stop at the next safe point. Idempotent. */
pe_solver_status_t pe_solver_stop(pe_solver_t *solver);

/**
 * Current progress. Never blocks the solve loop, so it is safe to poll from
 * another thread while pe_solver_run is in flight.
 */
pe_solver_status_t pe_solver_progress(const pe_solver_t *solver,
                               pe_progress_t *out);

/* ------------------------------------------------------------------ *
 * Results
 * ------------------------------------------------------------------ */

/**
 * Read part of the solved strategy.
 *
 * @param query  What to read (infoset, street, player, ...).
 * @param out    Receives a view over the solver's storage. The view stays
 *               valid until the next mutating call on the solver.
 */
pe_solver_status_t pe_solver_strategy(const pe_solver_t *solver,
                               const pe_strategy_query_t *query,
                               pe_strategy_view_t *out);

/**
 * Metrics of the solve, including the plan that actually executed: effective
 * backend per stage, resolved precision, and the exploitability guarantee that
 * applies (never "Nash" for a multiway or non-zero-sum game).
 */
pe_solver_status_t pe_solver_metrics(const pe_solver_t *solver,
                              pe_metrics_t *out);

/* ------------------------------------------------------------------ *
 * Persistence
 * ------------------------------------------------------------------ */

/** Write a backend-independent checkpoint or a solved-strategy artefact. */
pe_solver_status_t pe_solver_save(const pe_solver_t *solver,
                           const pe_persist_target_t *target);

/** Restore from a checkpoint written by any backend. */
pe_solver_status_t pe_solver_load(pe_solver_t *solver,
                           const pe_persist_source_t *source);

#ifdef __cplusplus
}
#endif

/* BR-02 vector best response. The contract is included here so callers that
 * use the public solver header see the measurement API; the concrete vector
 * game definition remains in pe_traversal.h. */
#include <poker_eval/solver/pe_best_response.h>

#endif /* POKER_EVAL_PE_SOLVER_H */
