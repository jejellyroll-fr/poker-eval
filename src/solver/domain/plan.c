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

#include <stdarg.h>
#include <stddef.h>
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
    vsnprintf(diag->items[diag->count].message, PE_DIAG_MESSAGE_MAX, fmt, args);
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
       LNB-02 introduces it; until then the combination is refused rather than
       run with an averaging that silently biases the result. */
    if (sampled && (algo->averaging == PE_AVG_UNIFORM ||
                    algo->averaging == PE_AVG_LINEAR))
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

static void pe_check_ranges(const pe_solver_config_t *cfg, pe_diagnostics_t *diag)
{
    const pe_algorithm_config_t *a = &cfg->algorithm;

    if (a->outcome_epsilon < 0.0 || a->outcome_epsilon > 1.0)
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
