/*
 * plan.c - Validation and execution plan (architecture v3, CTR-06)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The gate between "what was asked for" and "what will run". Its single rule:
 * a configuration that cannot be honoured is refused, never quietly replaced
 * by one that can. A solver that substitutes a mode it happens to support
 * produces plausible numbers answering a different question, and nothing
 * downstream can tell.
 *
 * Every refusal names both sides of the conflict, because "invalid
 * configuration" tells a caller nothing they can act on.
 *
 * snprintf is used to build those messages. That is formatting into a
 * caller-visible buffer, not I/O: nothing here opens, writes to or reads from
 * anything. The domain still reaches the outside world only through the ports.
 */

#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/core/safe_format.h>

#include "finite_double.h"

#include <stdarg.h>
#include <stddef.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Diagnostics
 * ------------------------------------------------------------------ */

void pe_diagnostics_clear(pe_diagnostics_t *diag)
{
    if (diag == NULL)
        return;
    memset(diag, 0, sizeof(*diag));
    diag->worst = PE_VALID_OK;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void pe_diag_add(pe_diagnostics_t *diag,
                        pe_valid_severity_t severity,
                        const char *fmt, ...)
{
    va_list args;

    if (diag == NULL)
        return;

    if (severity > diag->worst)
        diag->worst = severity;

    /* Past the cap, keep counting. A report that looks complete but silently
       lost half its findings is worse than one that says so. */
    if (diag->count >= PE_DIAG_MAX)
    {
        diag->dropped++;
        return;
    }

    diag->items[diag->count].severity = severity;
    va_start(args, fmt);
    /* `fmt` is not a literal here by construction; the format attribute above
       is what makes the compiler check it at every call site instead. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    (void)pe_safe_vformat(diag->items[diag->count].message,
                          PE_DIAG_MESSAGE_MAX, fmt, args);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    va_end(args);
    diag->count++;
}

/* Severity of a diagnostics set, usable when the caller passed NULL. */
static pe_valid_severity_t pe_worst(const pe_diagnostics_t *diag,
                                    pe_valid_severity_t fallback)
{
    return (diag != NULL) ? diag->worst : fallback;
}

/* ------------------------------------------------------------------ *
 * What a configuration needs
 * ------------------------------------------------------------------ */

static uint64_t pe_traversal_caps(pe_traversal_mode_t traversal)
{
    switch (traversal)
    {
    case PE_TRAVERSAL_FULL_VECTOR:
        return PE_CAP_VECTOR_FORM | PE_CAP_ENUMERATED_CHANCE;
    case PE_TRAVERSAL_CHANCE_VECTOR:
        return PE_CAP_VECTOR_FORM | PE_CAP_DIRECT_CHANCE_SAMPLING;
    case PE_TRAVERSAL_FULL_SCALAR:
        return PE_CAP_ENUMERATED_CHANCE;
    case PE_TRAVERSAL_EXTERNAL_SAMPLING:
    case PE_TRAVERSAL_OUTCOME_SAMPLING:
        return PE_CAP_DIRECT_CHANCE_SAMPLING;
    case PE_TRAVERSAL_COUNT:
    default:
        break;
    }
    /* Reached only for a value outside the enumeration. Requiring nothing lets
       validation report the real problem rather than a phantom capability. */
    return 0;
}

static uint64_t pe_stage_caps(pe_compute_kind_t kind, uint64_t gpu_cap)
{
    switch (kind)
    {
    case PE_COMPUTE_CPU_PAR:
        return PE_CAP_CPU_PARALLEL | PE_CAP_BATCH_UPDATES;
    case PE_COMPUTE_CUDA:
    case PE_COMPUTE_OPENCL:
    case PE_COMPUTE_HIP:
    case PE_COMPUTE_METAL:
        return gpu_cap | PE_CAP_BATCH_UPDATES;
    case PE_COMPUTE_AUTO:
    case PE_COMPUTE_CPU_REF:
    case PE_COMPUTE_COUNT:
    default:
        break;
    }
    /* cpu_ref needs nothing, and AUTO has already been resolved away by the
       time requirements are computed. */
    return 0;
}

uint64_t pe_config_required_caps(const pe_solver_config_t *cfg)
{
    pe_algorithm_config_t algo;
    uint64_t caps = 0;

    if (cfg == NULL)
        return 0;

    /* Answer for the expanded configuration: asking what `--algorithm dcfr`
       requires must not depend on the caller having expanded it first. */
    algo = cfg->algorithm;
    if (pe_preset_expand(algo.preset, &algo) != 0)
        return 0;

    caps |= pe_traversal_caps(algo.traversal);

    if (algo.pruning == PE_PRUNE_RBP)
        caps |= PE_CAP_RBP;

    caps |= pe_stage_caps(cfg->execution.stages.traversal, PE_CAP_GPU_TRAVERSAL);
    caps |= pe_stage_caps(cfg->execution.stages.update, PE_CAP_GPU_REGRET_UPDATE);
    caps |= pe_stage_caps(cfg->execution.stages.terminal_eval, PE_CAP_GPU_TERMINAL_EVAL);

    if (cfg->execution.deterministic)
        caps |= PE_CAP_DETERMINISTIC;

    return caps;
}

/* ------------------------------------------------------------------ *
 * Resolution
 * ------------------------------------------------------------------ */

/* A stage left at AUTO follows the global backend; a global backend left at
   AUTO means the resolver decides, and for now it decides cpu_ref. That is a
   genuine choice made on the caller's behalf, so it is reported as a FALLBACK
   rather than passed over in silence. */
static pe_compute_kind_t pe_resolve_stage(pe_compute_kind_t stage,
                                          pe_compute_kind_t backend,
                                          const char *stage_name,
                                          pe_diagnostics_t *diag)
{
    if (stage != PE_COMPUTE_AUTO)
        return stage;

    if (backend != PE_COMPUTE_AUTO)
        return backend;

    pe_diag_add(diag, PE_VALID_FALLBACK,
                "stage %s: no backend requested, resolved to %s",
                stage_name, pe_compute_kind_name(PE_COMPUTE_CPU_REF));
    return PE_COMPUTE_CPU_REF;
}

/* Combinations the matrix does not declare. Each rejection names both sides. */
static void pe_check_combination(const pe_algorithm_config_t *algo,
                                 pe_diagnostics_t *diag)
{
    int sampled = (algo->traversal == PE_TRAVERSAL_EXTERNAL_SAMPLING ||
                   algo->traversal == PE_TRAVERSAL_OUTCOME_SAMPLING);

    /* RBP prunes on cumulative regret, which outcome sampling only ever sees
       through one trajectory: the two together prune on noise. */
    if (algo->pruning == PE_PRUNE_RBP &&
        algo->traversal == PE_TRAVERSAL_OUTCOME_SAMPLING)
    {
        pe_diag_add(diag, PE_VALID_ERROR,
                    "pruning %s is not valid with traversal %s: "
                    "single-trajectory regret is too noisy to prune on",
                    pe_pruning_name(algo->pruning),
                    pe_traversal_name(algo->traversal));
    }

    /* Sampling needs averaging weighted by the inverse sampling probability.
       DCFR keeps POWER as its temporal weighting; its traversal is responsible
       for applying the sampling correction in addition to that weight. */
    if (sampled && algo->averaging != PE_AVG_IMPORTANCE &&
        !(algo->traversal == PE_TRAVERSAL_EXTERNAL_SAMPLING &&
          algo->regret == PE_REGRET_DCFR &&
          algo->averaging == PE_AVG_POWER))
    {
        pe_diag_add(diag, PE_VALID_ERROR,
                    "averaging %s is not valid with traversal %s: "
                    "sampled traversals need importance-weighted averaging (LNB-02)",
                    pe_averaging_name(algo->averaging),
                    pe_traversal_name(algo->traversal));
    }

    /* The exponential policy exists to carry the legacy ECFR behaviour, whose
       regret rule is inseparable from it. */
    if (algo->policy == PE_POLICY_EXPONENTIAL &&
        algo->regret != PE_REGRET_LEGACY_EXP)
    {
        pe_diag_add(diag, PE_VALID_ERROR,
                    "policy %s is only valid with regret %s, not %s",
                    pe_policy_name(algo->policy),
                    pe_regret_name(PE_REGRET_LEGACY_EXP),
                    pe_regret_name(algo->regret));
    }

    /* DCFR's gamma is carried by the averaging weight; pairing discounted
       regret with an unweighted average discards half the algorithm. */
    if (algo->regret == PE_REGRET_DCFR && algo->averaging != PE_AVG_POWER)
    {
        pe_diag_add(diag, PE_VALID_WARNING,
                    "regret %s is normally paired with averaging %s, not %s",
                    pe_regret_name(algo->regret),
                    pe_averaging_name(PE_AVG_POWER),
                    pe_averaging_name(algo->averaging));
    }
}

static int pe_check_enum_ranges(const pe_solver_config_t *cfg,
                                const pe_algorithm_config_t *algo,
                                pe_diagnostics_t *diag)
{
    int valid = 1;
    if ((int)algo->traversal < 0 || algo->traversal >= PE_TRAVERSAL_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "algorithm traversal mode %d is out of range",
                    (int)algo->traversal);
    }
    if ((int)algo->regret < 0 || algo->regret >= PE_REGRET_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "algorithm regret mode %d is out of range",
                    (int)algo->regret);
    }
    if ((int)algo->policy < 0 || algo->policy >= PE_POLICY_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "algorithm policy mode %d is out of range",
                    (int)algo->policy);
    }
    if ((int)algo->averaging < 0 || algo->averaging >= PE_AVG_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "algorithm averaging mode %d is out of range",
                    (int)algo->averaging);
    }
    if ((int)algo->pruning < 0 || algo->pruning >= PE_PRUNE_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "algorithm pruning mode %d is out of range",
                    (int)algo->pruning);
    }
    if ((int)cfg->execution.precision < 0 ||
        cfg->execution.precision >= PE_PREC_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "execution precision mode %d is out of range",
                    (int)cfg->execution.precision);
    }
    if ((int)cfg->execution.backend < 0 ||
        cfg->execution.backend >= PE_COMPUTE_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "execution backend %d is out of range",
                    (int)cfg->execution.backend);
    }
    if ((int)cfg->execution.stages.traversal < 0 ||
        cfg->execution.stages.traversal >= PE_COMPUTE_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "traversal backend %d is out of range",
                    (int)cfg->execution.stages.traversal);
    }
    if ((int)cfg->execution.stages.update < 0 ||
        cfg->execution.stages.update >= PE_COMPUTE_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "update backend %d is out of range",
                    (int)cfg->execution.stages.update);
    }
    if ((int)cfg->execution.stages.terminal_eval < 0 ||
        cfg->execution.stages.terminal_eval >= PE_COMPUTE_COUNT)
    {
        valid = 0;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "terminal evaluation backend %d is out of range",
                    (int)cfg->execution.stages.terminal_eval);
    }
    return valid;
}

static void pe_check_ranges(const pe_solver_config_t *cfg, pe_diagnostics_t *diag)
{
    const pe_algorithm_config_t *a = &cfg->algorithm;

    if (!pe_finite_double(a->outcome_epsilon) ||
        a->outcome_epsilon < 0.0 || a->outcome_epsilon > 1.0)
        pe_diag_add(diag, PE_VALID_ERROR,
                    "outcome_epsilon must be a probability, got %f",
                    a->outcome_epsilon);

    if (a->averaging == PE_AVG_DELAYED_LINEAR && a->averaging_delay < 0)
        pe_diag_add(diag, PE_VALID_ERROR,
                    "averaging_delay must not be negative, got %d",
                    a->averaging_delay);

    if (a->policy == PE_POLICY_EXPONENTIAL && a->exponential_lambda <= 0.0)
        pe_diag_add(diag, PE_VALID_ERROR,
                    "exponential_lambda must be positive, got %f",
                    a->exponential_lambda);

    if (cfg->execution.cpu_threads < 0)
        pe_diag_add(diag, PE_VALID_ERROR,
                    "cpu_threads must not be negative, got %d",
                    cfg->execution.cpu_threads);

    if (cfg->execution.stages.traversal == PE_COMPUTE_CPU_REF &&
        cfg->execution.cpu_threads > 1)
        pe_diag_add(diag, PE_VALID_WARNING,
                    "backend %s is single-threaded; cpu_threads = %d has no effect",
                    pe_compute_kind_name(PE_COMPUTE_CPU_REF),
                    cfg->execution.cpu_threads);

    if (!(cfg->target_exploitability_mbb <= DBL_MAX) ||
        !(cfg->target_exploitability_mbb >= -DBL_MAX) ||
        cfg->target_exploitability_mbb < 0.0)
        pe_diag_add(diag, PE_VALID_ERROR,
                    "target_exploitability_mbb must be finite and non-negative, got %f",
                    cfg->target_exploitability_mbb);

    if (cfg->target_exploitability_mbb > 0.0 &&
        cfg->exploitability_interval == 0)
        pe_diag_add(diag, PE_VALID_ERROR,
                    "exploitability_interval must be positive when an exploitability "
                    "target is configured");

    if (cfg->max_iterations == 0 &&
        !(cfg->target_exploitability_mbb > 0.0) &&
        !(cfg->target_exploitability_mbb < 0.0))
        pe_diag_add(diag, PE_VALID_ERROR,
                    "at least one stop condition is required: max_iterations or "
                    "target_exploitability_mbb");
}

static void pe_report_missing_caps(uint64_t missing, pe_diagnostics_t *diag)
{
    size_t i;

    for (i = 0; i < PE_CAP_COUNT; ++i)
    {
        uint64_t bit = pe_cap_at(i);
        if ((missing & bit) == 0)
            continue;
        pe_diag_add(diag, PE_VALID_ERROR,
                    "capability %s is required but not provided",
                    pe_cap_name(bit));
    }
}

pe_valid_severity_t pe_plan_resolve(const pe_solver_config_t *cfg,
                                    uint64_t provided_caps,
                                    pe_execution_plan_t *out_plan,
                                    pe_diagnostics_t *out_diag)
{
    pe_algorithm_config_t algo;
    pe_execution_plan_t plan;
    uint64_t required;
    uint64_t missing;

    pe_diagnostics_clear(out_diag);

    if (out_plan != NULL)
        memset(out_plan, 0, sizeof(*out_plan));

    if (cfg == NULL || out_plan == NULL)
    {
        pe_diag_add(out_diag, PE_VALID_ERROR,
                    "pe_plan_resolve requires a configuration and a plan buffer");
        return PE_VALID_ERROR;
    }

    algo = cfg->algorithm;
    if (pe_preset_expand(algo.preset, &algo) != 0)
    {
        pe_diag_add(out_diag, PE_VALID_ERROR,
                    "unknown algorithm preset %d", (int)algo.preset);
        return PE_VALID_ERROR;
    }

    if (pe_check_enum_ranges(cfg, &algo, out_diag))
        pe_check_combination(&algo, out_diag);
    pe_check_ranges(cfg, out_diag);

    memset(&plan, 0, sizeof(plan));
    plan.preset = cfg->algorithm.preset;
    plan.traversal = algo.traversal;
    plan.regret = algo.regret;
    plan.policy = algo.policy;
    plan.averaging = algo.averaging;
    plan.pruning = algo.pruning;
    plan.precision = cfg->execution.precision;
    plan.cpu_threads = cfg->execution.cpu_threads;
    plan.deterministic = cfg->execution.deterministic;

    plan.stages.traversal = pe_resolve_stage(cfg->execution.stages.traversal,
                                             cfg->execution.backend,
                                             "traversal", out_diag);
    plan.stages.update = pe_resolve_stage(cfg->execution.stages.update,
                                          cfg->execution.backend,
                                          "update", out_diag);
    plan.stages.terminal_eval = pe_resolve_stage(cfg->execution.stages.terminal_eval,
                                                 cfg->execution.backend,
                                                 "terminal_eval", out_diag);

    /* Requirements come from the resolved stages, not the requested ones: a
       stage that resolved to cpu_ref does not need a GPU capability. */
    required = pe_traversal_caps(algo.traversal);
    if (algo.pruning == PE_PRUNE_RBP)
        required |= PE_CAP_RBP;
    required |= pe_stage_caps(plan.stages.traversal, PE_CAP_GPU_TRAVERSAL);
    required |= pe_stage_caps(plan.stages.update, PE_CAP_GPU_REGRET_UPDATE);
    required |= pe_stage_caps(plan.stages.terminal_eval, PE_CAP_GPU_TERMINAL_EVAL);
    if (cfg->execution.deterministic)
        required |= PE_CAP_DETERMINISTIC;

    plan.required_caps = required;
    plan.provided_caps = provided_caps;

    missing = required & ~provided_caps;
    if (missing != 0)
        pe_report_missing_caps(missing, out_diag);

    if (pe_worst(out_diag, PE_VALID_OK) == PE_VALID_ERROR)
    {
        /* Leave nothing a caller could mistake for a usable plan. */
        memset(out_plan, 0, sizeof(*out_plan));
        return PE_VALID_ERROR;
    }

    plan.valid = 1;
    *out_plan = plan;
    return pe_worst(out_diag, PE_VALID_OK);
}

/* ------------------------------------------------------------------ *
 * Estimation (STO-05)
 * ------------------------------------------------------------------ */

uint32_t pe_precision_bytes(pe_precision_mode_t precision)
{
    switch (precision)
    {
    case PE_PREC_F32:
        return 4u;
    case PE_PREC_FIXED16:
        return 2u;
    case PE_PREC_MIXED:
        /* Accumulates in F32 and reduces in F64. The reduction buffer is what
           sizes the storage, so this is 8 rather than 4: an estimate that
           under-reports is worse than one that over-reports, because the
           caller only finds out by running out. */
        return 8u;
    case PE_PREC_F64:
    case PE_PREC_COUNT:
    default:
        return 8u;
    }
}

uint32_t pe_plan_value_arrays(const pe_execution_plan_t *plan)
{
    /* Regret and average are always kept. The current strategy is recomputed
       per node by every traversal that exists today, and the locked array is
       only allocated where something is locked — neither is counted until a
       plan says it needs one. */
    uint32_t n = 2u;

    if (plan != NULL && plan->pruning == PE_PRUNE_RBP)
        n += 1u;   /* RBP keeps the current strategy to decide what to skip */
    return n;
}

static int pe_u64_mul(uint64_t left, uint64_t right, uint64_t *out)
{
    if (right != 0u && left > UINT64_MAX / right)
        return -1;
    *out = left * right;
    return 0;
}

static int pe_u64_add(uint64_t left, uint64_t right, uint64_t *out)
{
    if (left > UINT64_MAX - right)
        return -1;
    *out = left + right;
    return 0;
}

static pe_valid_severity_t pe_estimate_overflow(pe_diagnostics_t *out_diag)
{
    pe_diag_add(out_diag, PE_VALID_ERROR,
                "problem dimensions overflow the uint64 memory estimate");
    return PE_VALID_ERROR;
}

pe_valid_severity_t pe_plan_estimate(const pe_execution_plan_t *plan,
                                     const pe_problem_config_t *problem,
                                     uint64_t budget_bytes,
                                     pe_estimate_t *out,
                                     pe_diagnostics_t *out_diag)
{
    uint64_t slots;
    uint64_t per_slot;
    uint64_t arrays;
    uint64_t term;
    uint64_t storage_bytes;
    uint64_t scratch_bytes;

    if (plan == NULL || problem == NULL || out == NULL)
        return PE_VALID_ERROR;

    memset(out, 0, sizeof(*out));

    if (problem->expected_infosets == 0 || problem->expected_actions == 0 ||
        problem->expected_combos == 0)
    {
        pe_diag_add(out_diag, PE_VALID_ERROR,
                    "problem size is empty (%llu infosets, %u actions, %u combos): "
                    "estimating nothing is not an estimate of zero",
                    (unsigned long long)problem->expected_infosets,
                    (unsigned)problem->expected_actions,
                    (unsigned)problem->expected_combos);
        return PE_VALID_ERROR;
    }

    if (pe_u64_mul(problem->expected_infosets,
                   (uint64_t)problem->expected_actions, &slots) != 0 ||
        pe_u64_mul(slots, (uint64_t)problem->expected_combos, &slots) != 0)
        return pe_estimate_overflow(out_diag);
    per_slot = pe_precision_bytes(plan->precision);
    arrays = pe_plan_value_arrays(plan);

    out->infosets = problem->expected_infosets;
    out->slots = slots;
    out->bytes_per_slot = (uint32_t)per_slot;
    out->value_arrays = (uint32_t)arrays;

    /* Value arrays, plus the metadata and the key map the dense-ID storage
       keeps per infoset. The map is sized for a 70% load factor and rounded to
       a power of two, so it holds between 1.43 and 2.86 slots per infoset; 2
       is the midpoint and the error it carries is far below the slack a memory
       budget is set with. */
    if (pe_u64_mul(slots, per_slot, &storage_bytes) != 0 ||
        pe_u64_mul(storage_bytes, arrays, &storage_bytes) != 0 ||
        pe_u64_mul(problem->expected_infosets, PE_STORAGE_META_BYTES,
                   &term) != 0 ||
        pe_u64_add(storage_bytes, term, &storage_bytes) != 0 ||
        pe_u64_mul(problem->expected_infosets,
                   2u * sizeof(uint32_t), &term) != 0 ||
        pe_u64_add(storage_bytes, term, &storage_bytes) != 0)
        return pe_estimate_overflow(out_diag);
    out->storage_bytes = storage_bytes;

    /* One scratch frame per recursion level, and one update batch. Small next
       to the storage on any real solve, and the reason it is reported
       separately is that it does not shrink with abstraction. */
    if (pe_u64_mul(PE_ESTIMATE_SCRATCH_DEPTH,
                   (uint64_t)problem->expected_actions, &scratch_bytes) != 0 ||
        pe_u64_mul(scratch_bytes, 3u * sizeof(double), &scratch_bytes) != 0 ||
        pe_u64_mul((uint64_t)problem->expected_actions,
                   (uint64_t)problem->expected_combos, &term) != 0 ||
        pe_u64_mul(term, 2u * sizeof(double), &term) != 0 ||
        pe_u64_add(scratch_bytes, term, &scratch_bytes) != 0)
        return pe_estimate_overflow(out_diag);
    out->scratch_bytes = scratch_bytes;

    if (pe_u64_add(out->storage_bytes, out->scratch_bytes,
                   &out->host_bytes) != 0)
        return pe_estimate_overflow(out_diag);

    /* A device holds the value arrays and nothing else so far: the traversal
       runs on the host until GPU-4. */
    if (pe_compute_kind_is_gpu(plan->stages.traversal) ||
        pe_compute_kind_is_gpu(plan->stages.update))
        if (pe_u64_mul(slots, per_slot, &out->device_bytes) != 0 ||
            pe_u64_mul(out->device_bytes, arrays, &out->device_bytes) != 0)
            return pe_estimate_overflow(out_diag);

    out->budget_bytes = budget_bytes;
    out->within_budget = (budget_bytes == 0 || out->host_bytes <= budget_bytes);

    if (!out->within_budget)
    {
        pe_diag_add(out_diag, PE_VALID_ERROR,
                    "estimated %llu MiB of host memory exceeds the %llu MiB budget",
                    (unsigned long long)(out->host_bytes >> 20),
                    (unsigned long long)(budget_bytes >> 20));
        return PE_VALID_ERROR;
    }

    return PE_VALID_OK;
}

/* ------------------------------------------------------------------ *
 * Text form
 * ------------------------------------------------------------------ */

size_t pe_plan_to_string(const pe_execution_plan_t *plan, char *buf, size_t buflen)
{
    char caps[PE_CAPS_STRING_MAX];
    int n;

    if (plan == NULL || (buf == NULL && buflen != 0))
        return 0;

    if (!plan->valid)
    {
        n = snprintf(buf, buflen, "execution plan: none (configuration refused)\n");
        return (n < 0) ? 0 : (size_t)n;
    }

    pe_caps_to_string(plan->required_caps, caps, sizeof(caps));

    n = snprintf(buf, buflen,
                 "execution plan\n"
                 "  preset          %s\n"
                 "  traversal       %s\n"
                 "  regret          %s\n"
                 "  policy          %s\n"
                 "  averaging       %s\n"
                 "  pruning         %s\n"
                 "  precision       %s\n"
                 "  stage/traversal %s\n"
                 "  stage/update    %s\n"
                 "  stage/terminal  %s\n"
                 "  cpu threads     %d\n"
                 "  deterministic   %s\n"
                 "  requires        %s\n",
                 pe_preset_name(plan->preset),
                 pe_traversal_name(plan->traversal),
                 pe_regret_name(plan->regret),
                 pe_policy_name(plan->policy),
                 pe_averaging_name(plan->averaging),
                 pe_pruning_name(plan->pruning),
                 pe_precision_name(plan->precision),
                 pe_compute_kind_name(plan->stages.traversal),
                 pe_compute_kind_name(plan->stages.update),
                 pe_compute_kind_name(plan->stages.terminal_eval),
                 plan->cpu_threads,
                 plan->deterministic ? "yes" : "no",
                 caps);

    return (n < 0) ? 0 : (size_t)n;
}
