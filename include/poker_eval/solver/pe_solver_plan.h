/*
 * pe_solver_plan.h - Registry, validation and execution plan (v3, CTR-06)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * A configuration says what the caller wants. A plan says what will actually
 * run. Between the two sits the registry, and its job is narrow but strict:
 * expand a preset into explicit axes, check the combination against the
 * capability matrix, and either produce an immutable plan or refuse.
 *
 * Refuse, not degrade. A solver that quietly substitutes a mode it can honour
 * for the one it was asked for produces numbers that look fine and answer a
 * different question. Every rejection here names the two things in conflict so
 * the caller can act on it.
 *
 * Presets are surface. PE_PRESET_CFR is a name for FULL_SCALAR + VANILLA +
 * REGRET_MATCHING + UNIFORM, nothing more; expanding one never bypasses
 * validation.
 */

#ifndef POKER_EVAL_PE_SOLVER_PLAN_H
#define POKER_EVAL_PE_SOLVER_PLAN_H

#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Diagnostics
 * ------------------------------------------------------------------ */

/*
 * Ordered by increasing severity, so the worst of a set is a maximum.
 *
 *   OK        nothing to report.
 *   WARNING   the plan runs, but the caller should know something.
 *   FALLBACK  the resolver chose something the caller did not ask for, in a
 *             case where choosing is its job — an AUTO backend, typically.
 *             Never used to paper over an unsatisfiable request.
 *   ERROR     the configuration cannot be honoured. No plan is produced.
 */
typedef enum {
    PE_VALID_OK = 0,
    PE_VALID_WARNING,
    PE_VALID_FALLBACK,
    PE_VALID_ERROR
} pe_valid_severity_t;

#define PE_DIAG_MESSAGE_MAX 192
#define PE_DIAG_MAX 16

typedef struct {
    pe_valid_severity_t severity;
    char message[PE_DIAG_MESSAGE_MAX];
} pe_diagnostic_t;

/* Completes the tag forward-declared in pe_solver.h. */
struct pe_diagnostics_t {
    pe_diagnostic_t items[PE_DIAG_MAX];
    size_t count;

    /* Diagnostics past PE_DIAG_MAX. Counted rather than dropped silently:
       a truncated report that looks complete is worse than no report. */
    size_t dropped;

    pe_valid_severity_t worst;
};

/** Reset a diagnostics buffer. Safe on NULL. */
void pe_diagnostics_clear(pe_diagnostics_t *diag);

/** Human-readable name of a severity, never NULL. */
const char *pe_valid_severity_name(pe_valid_severity_t severity);

/* ------------------------------------------------------------------ *
 * The plan
 * ------------------------------------------------------------------ */

/*
 * Completes the tag forward-declared in pe_solver.h.
 *
 * Immutable by convention: produced by pe_plan_resolve and read afterwards.
 * Every field is resolved — no preset left to expand, no PE_COMPUTE_AUTO left
 * to decide — so nothing downstream has to re-derive what was meant.
 */
struct pe_execution_plan_t {
    /* Which preset produced this, or PE_PRESET_CUSTOM when the axes were
       given explicitly. Kept for reporting; the axes below are authoritative. */
    pe_algorithm_preset_t preset;

    pe_traversal_mode_t traversal;
    pe_regret_mode_t    regret;
    pe_policy_mode_t    policy;
    pe_averaging_mode_t averaging;
    pe_pruning_mode_t   pruning;

    /* No stage is PE_COMPUTE_AUTO here. */
    pe_stage_backends_t stages;
    pe_precision_mode_t precision;

    int cpu_threads;
    int deterministic;

    /* What this combination needs, and what was available to satisfy it.
       required_caps & ~provided_caps is empty for a valid plan. */
    uint64_t required_caps;
    uint64_t provided_caps;

    /* Nonzero when the plan may be executed. Zero after an ERROR, in which
       case every other field is left at its zeroed value. */
    int valid;
};

/* ------------------------------------------------------------------ *
 * Presets
 * ------------------------------------------------------------------ */

/** CLI-facing name of a preset ("cfr", "cfr+", "external-mccfr"), or NULL. */
const char *pe_preset_name(pe_algorithm_preset_t preset);

/** Whether a preset preserves an experimental legacy behaviour. */
int pe_preset_is_experimental(pe_algorithm_preset_t preset);

/** Preset for a CLI name, case-insensitive. PE_PRESET_COUNT when unknown. */
pe_algorithm_preset_t pe_preset_from_name(const char *name);

/**
 * Expand a preset into explicit axes.
 *
 * Writes traversal, regret, policy and averaging; leaves pruning and the
 * numeric parameters alone, so a caller may combine a preset with explicit
 * overrides. PE_PRESET_CUSTOM writes nothing.
 *
 * Expansion is not validation: every declared preset expands, including ones
 * whose plan will then be refused because the capabilities are not there yet.
 *
 * @return 0 on success, -1 on a NULL argument or an unknown preset.
 */
int pe_preset_expand(pe_algorithm_preset_t preset, pe_algorithm_config_t *inout);

/* ------------------------------------------------------------------ *
 * Axis names — needed to say what conflicts with what
 * ------------------------------------------------------------------ */

const char *pe_traversal_name(pe_traversal_mode_t mode);
const char *pe_regret_name(pe_regret_mode_t mode);
const char *pe_policy_name(pe_policy_mode_t mode);
const char *pe_averaging_name(pe_averaging_mode_t mode);
const char *pe_pruning_name(pe_pruning_mode_t mode);
const char *pe_precision_name(pe_precision_mode_t mode);
const char *pe_compute_kind_name(pe_compute_kind_t kind);

/* ------------------------------------------------------------------ *
 * Resolution
 * ------------------------------------------------------------------ */

/**
 * Capabilities a configuration requires, once its preset is expanded.
 *
 * Exposed separately so a caller can ask "what would this need?" without
 * having anything able to provide it yet — which is how a CLI answers
 * --show-capabilities before a game is loaded.
 */
uint64_t pe_config_required_caps(const pe_solver_config_t *cfg);

/**
 * Expand, validate, and produce a plan.
 *
 * @param cfg             Configuration to resolve.
 * @param provided_caps   What the game and the available backends together
 *                        can do. Anything required but absent is an error.
 * @param out_plan        Receives the resolved plan. Zeroed, with valid = 0,
 *                        when the result is PE_VALID_ERROR: a refused
 *                        configuration must not leave a half-built plan that
 *                        a caller could mistake for a usable one.
 * @param out_diag        Receives the diagnostics. May be NULL.
 * @return The worst severity encountered.
 */
pe_valid_severity_t pe_plan_resolve(const pe_solver_config_t *cfg,
                                    uint64_t provided_caps,
                                    pe_execution_plan_t *out_plan,
                                    pe_diagnostics_t *out_diag);

/* ------------------------------------------------------------------ *
 * Estimation (STO-05)
 * ------------------------------------------------------------------ */

/*
 * Bytes the dense-ID storage keeps per infoset outside the value arrays.
 * Mirrors sizeof(pe_infoset_meta_t); named here so the estimate does not have
 * to include the concrete storage header to know it.
 */
#define PE_STORAGE_META_BYTES 24u

/* Recursion depth the scratch estimate assumes. Generous: a poker tree is
   nowhere near this deep, and over-reporting scratch is harmless next to
   under-reporting storage. */
#define PE_ESTIMATE_SCRATCH_DEPTH 256u

/** Bytes one value slot occupies at a given precision. */
uint32_t pe_precision_bytes(pe_precision_mode_t precision);

/** How many value arrays a plan will keep populated. */
uint32_t pe_plan_value_arrays(const pe_execution_plan_t *plan);

/**
 * What a solve of this size, under this plan, would cost.
 *
 * @param budget_bytes  Host budget; 0 means none.
 * @param out_diag      May be NULL.
 * @return PE_VALID_OK, or PE_VALID_ERROR when the problem size is empty or
 *         the estimate does not fit the budget. `out` is filled either way
 *         except on a NULL argument, so a caller can report what did not fit.
 */
pe_valid_severity_t pe_plan_estimate(const pe_execution_plan_t *plan,
                                     const pe_problem_config_t *problem,
                                     uint64_t budget_bytes,
                                     pe_estimate_t *out,
                                     pe_diagnostics_t *out_diag);

/**
 * Render a resolved plan as text, one field per line.
 *
 * This is what --print-execution-plan shows: the point is that a user can see
 * the backend that will actually run each stage, not the one they asked for.
 *
 * snprintf semantics: always NUL-terminated when buflen is non-zero, and the
 * return value is the length the full text would have.
 */
size_t pe_plan_to_string(const pe_execution_plan_t *plan, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_SOLVER_PLAN_H */
