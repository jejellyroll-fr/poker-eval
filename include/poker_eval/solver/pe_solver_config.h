/*
 * pe_solver_config.h - Orthogonal configuration axes (architecture v3, CTR-03)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The v2 solver configured itself with a pile of booleans — enable_dcfr,
 * enable_ecfr, enable_linear_avg, enable_mccfvfp — which made illegal
 * combinations expressible and legal ones hard to read. v3 replaces them with
 * independent axes: how the tree is walked, how regret accumulates, how the
 * policy is derived, how the average is weighted, what is pruned, and at what
 * precision. Presets sit on top as the user-facing surface and resolve to
 * explicit axis values (CTR-06).
 *
 * Nothing here decides anything. This header is vocabulary: the registry
 * validates a combination, and the execution plan records what was resolved.
 *
 * Enumerator values cross the public ABI. New values are appended; existing
 * ones never move.
 */

#ifndef POKER_EVAL_PE_SOLVER_CONFIG_H
#define POKER_EVAL_PE_SOLVER_CONFIG_H

#include <poker_eval/solver/pe_solver.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Presets — the user-facing surface
 * ------------------------------------------------------------------ */

/*
 * A preset is a name for a combination of axes, not a mode of its own. The
 * registry expands it into explicit axis values and then validates those; a
 * preset never bypasses validation.
 *
 * PE_PRESET_CUSTOM means "the axes below are authoritative, expand nothing".
 * It is zero, so a configuration that sets its axes by hand needs no preset.
 */
typedef enum {
    PE_PRESET_CUSTOM = 0,
    PE_PRESET_CFR,               /* FULL_SCALAR + VANILLA + RM + UNIFORM     */
    PE_PRESET_CFR_VECTOR,        /* lane A reference                          */
    PE_PRESET_CFR_PLUS,          /* lane A, RM+ and delayed linear averaging  */
    PE_PRESET_DCFR,              /* lane A, discounted regret                 */
    PE_PRESET_CFR_PLUS_CHANCE,   /* lane A, sampled chance                    */
    PE_PRESET_EXTERNAL_MCCFR,    /* lane B                                    */
    PE_PRESET_EXTERNAL_DCFR,     /* lane B, only after External is validated  */
    PE_PRESET_OUTCOME_MCCFR,     /* lane B                                    */
    PE_PRESET_ECFR,              /* experimental, preserves v2 behaviour      */
    PE_PRESET_COUNT
} pe_algorithm_preset_t;

/* ------------------------------------------------------------------ *
 * Axis: how the tree is walked
 * ------------------------------------------------------------------ */

typedef enum {
    /* Lane A — values carried per combo. */
    PE_TRAVERSAL_FULL_VECTOR = 0,   /* every action, every chance outcome    */
    PE_TRAVERSAL_CHANCE_VECTOR,     /* every action, sampled chance          */

    /* Reference oracle — one value per state. Correct, slow, and the yardstick
       every other traversal is measured against. */
    PE_TRAVERSAL_FULL_SCALAR,

    /* Lane B — sampled. */
    PE_TRAVERSAL_EXTERNAL_SAMPLING, /* chance and opponents sampled          */
    PE_TRAVERSAL_OUTCOME_SAMPLING,  /* a single trajectory                   */

    PE_TRAVERSAL_COUNT
} pe_traversal_mode_t;

/* ------------------------------------------------------------------ *
 * Axis: how regret accumulates
 * ------------------------------------------------------------------ */

typedef enum {
    PE_REGRET_VANILLA = 0,   /* plain accumulation                            */
    PE_REGRET_PLUS,          /* CFR+: regret matching+ and clamp at zero      */
    PE_REGRET_DCFR,          /* discounted, alpha/beta/gamma below            */
    PE_REGRET_LEGACY_EXP,    /* preserves the v2 ECFR behaviour as-is         */
    PE_REGRET_COUNT
} pe_regret_mode_t;

/* ------------------------------------------------------------------ *
 * Axis: how the policy is derived from regret
 * ------------------------------------------------------------------ */

typedef enum {
    PE_POLICY_REGRET_MATCHING = 0,
    PE_POLICY_EXPONENTIAL,          /* softmax over regret, temperature below */
    PE_POLICY_COUNT
} pe_policy_mode_t;

/* ------------------------------------------------------------------ *
 * Axis: how the average strategy is weighted
 * ------------------------------------------------------------------ */

typedef enum {
    PE_AVG_UNIFORM = 0,
    PE_AVG_LINEAR,           /* weight t                                      */
    PE_AVG_POWER,            /* weight (t/(t+1))^gamma, DCFR's companion      */
    PE_AVG_DELAYED_LINEAR,   /* skip the first averaging_delay iterations     */
    PE_AVG_COUNT
} pe_averaging_mode_t;

/* ------------------------------------------------------------------ *
 * Axis: pruning
 * ------------------------------------------------------------------ */

typedef enum {
    PE_PRUNE_NONE = 0,
    PE_PRUNE_RBP,            /* regret-based pruning                          */
    PE_PRUNE_COUNT
} pe_pruning_mode_t;

/* ------------------------------------------------------------------ *
 * Axis: numeric precision
 * ------------------------------------------------------------------ */

typedef enum {
    PE_PREC_F64 = 0,         /* reference precision                           */
    PE_PREC_F32,
    PE_PREC_MIXED,           /* F32 accumulation, F64 reduction               */
    PE_PREC_FIXED16,         /* 16-bit fixed point, lane B at scale           */
    PE_PREC_COUNT
} pe_precision_mode_t;

/* ------------------------------------------------------------------ *
 * Axis: which backend runs which stage
 * ------------------------------------------------------------------ */

typedef enum {
    PE_COMPUTE_AUTO = 0,     /* let the resolver choose, per stage            */
    PE_COMPUTE_CPU_REF,      /* one thread, F64, batch of 1, stable order     */
    PE_COMPUTE_CPU_PAR,
    PE_COMPUTE_CUDA,
    PE_COMPUTE_OPENCL,
    PE_COMPUTE_COUNT
} pe_compute_kind_t;

/*
 * Backends are chosen per stage, so terminal evaluation can run on a GPU long
 * before the traversal does. A stage left at PE_COMPUTE_AUTO follows
 * pe_execution_config_t::backend, and AUTO there means the resolver decides.
 */
typedef struct {
    pe_compute_kind_t traversal;
    pe_compute_kind_t update;
    pe_compute_kind_t terminal_eval;
} pe_stage_backends_t;

/* ------------------------------------------------------------------ *
 * Algorithm configuration
 * ------------------------------------------------------------------ */

typedef struct {
    pe_algorithm_preset_t preset;   /* CUSTOM = the axes below are used as-is */

    pe_traversal_mode_t   traversal;
    pe_regret_mode_t      regret;
    pe_policy_mode_t      policy;
    pe_averaging_mode_t   averaging;
    pe_pruning_mode_t     pruning;

    /* Discounted CFR. Canonical defaults from Brown & Sandholm: positive
       regrets are scaled by t^alpha/(t^alpha+1), negative ones by
       t^beta/(t^beta+1), and the average contribution by (t/(t+1))^gamma.
       Read only when regret is PE_REGRET_DCFR (alpha, beta) or averaging is
       PE_AVG_POWER (gamma). */
    double dcfr_alpha;
    double dcfr_beta;
    double dcfr_gamma;

    /* Temperature of PE_POLICY_EXPONENTIAL. */
    double exponential_lambda;

    /* Iterations skipped by PE_AVG_DELAYED_LINEAR. */
    int averaging_delay;

    /* Exploration of PE_TRAVERSAL_OUTCOME_SAMPLING, in [0, 1]. */
    double outcome_epsilon;
} pe_algorithm_config_t;

/* ------------------------------------------------------------------ *
 * Execution configuration
 * ------------------------------------------------------------------ */

typedef struct {
    /* Backend for every stage left at PE_COMPUTE_AUTO in `stages`. */
    pe_compute_kind_t   backend;
    pe_stage_backends_t stages;

    pe_precision_mode_t precision;

    /* Device index for a GPU backend; -1 selects any available device. */
    int device_id;

    /* Threads for PE_COMPUTE_CPU_PAR. 0 asks the backend to decide. */
    int cpu_threads;

    /* Require bit-identical results across runs with the same seed. Refuses
       any backend that cannot promise it rather than quietly relaxing. */
    int deterministic;

    /* Batch sizes crossing the compute port. 0 lets the resolved plan pick a
       size from the backend's capabilities. Batching is what keeps the port
       cheap: it is crossed once per batch, never once per node. */
    size_t sample_batch_size;
    size_t terminal_batch_size;
    size_t update_batch_size;

    /*
     * Host memory the solve may use, in bytes. 0 means no budget.
     *
     * Memory is an input here, not a consequence discovered when the machine
     * starts swapping: pe_solver_validate() refuses a configuration whose
     * estimate exceeds it, before anything is allocated.
     */
    uint64_t max_ram_bytes;
} pe_execution_config_t;

/*
 * How big the problem is expected to be.
 *
 * Supplied by the caller for now. Once the game port exists the game will fill
 * it — a game knows its own tree — but the estimate has to work before that,
 * because deciding whether a solve fits is exactly the question one asks
 * before committing to it.
 *
 * The counts are averages: an estimate is a budget check, not an inventory.
 */
typedef struct {
    uint64_t expected_infosets;
    uint16_t expected_actions;
    /** 1 outside the vector lane. */
    uint16_t expected_combos;
} pe_problem_config_t;

/* ------------------------------------------------------------------ *
 * Solver configuration
 * ------------------------------------------------------------------ */

/* Completes the tag forward-declared in pe_solver.h. */
struct pe_solver_config_t {
    pe_algorithm_config_t algorithm;
    pe_execution_config_t execution;
    pe_problem_config_t problem;

    /* Seed of the solver's root RNG stream. Every derived stream is a
       reproducible function of it, so the same seed replays the same run. */
    uint64_t seed;

    /* Stop after this many iterations. 0 means no iteration limit, in which
       case another stop condition must apply or the solve never ends. */
    uint64_t max_iterations;

    /* BR-04: zero disables the exploitability stop condition. */
    double target_exploitability_mbb;
    uint64_t exploitability_interval;
};

/* ------------------------------------------------------------------ *
 * Defaults
 * ------------------------------------------------------------------ */

/**
 * The reference configuration: exhaustive scalar traversal, vanilla regret,
 * regret matching, uniform averaging, no pruning, F64, on the single-threaded
 * reference backend. It is the slowest and the most trustworthy combination —
 * the one every other plan is checked against — which is why it is the default.
 *
 * NOTE: a zero-initialised pe_solver_config_t is NOT this configuration. The
 * axes are ordered for reading (lane A first, then the oracle, then lane B),
 * so zero means PE_TRAVERSAL_FULL_VECTOR on an AUTO backend. Always start from
 * this function rather than from `{0}`; test_pe_solver_config pins the
 * difference so it cannot be forgotten.
 */
pe_solver_config_t pe_solver_config_default(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_SOLVER_CONFIG_H */
