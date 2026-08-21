/*
 * solver.c - Solver lifecycle skeleton (architecture v3, CTR-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * CTR-01 stands the hexagon up without wiring anything into it: every entry
 * point of pe_solver.h exists and links, argument checking is in place, and
 * the bodies report PE_ERR_NOT_IMPLEMENTED. Nothing here includes cfr_core,
 * the GPU headers or <stdio.h> — the domain reaches the outside world only
 * through the driven ports, which CTR-04 introduces.
 *
 * Later tickets replace these bodies one at a time; the argument checks are
 * already the final behaviour and do not change when a body lands.
 */

#include <poker_eval/solver/pe_solver.h>

#include <stddef.h>

/* ------------------------------------------------------------------ *
 * Lifecycle
 * ------------------------------------------------------------------ */

pe_solver_t *pe_solver_create(const pe_solver_config_t *cfg,
                              const pe_solver_deps_t *deps)
{
    /* CTR-04 injects the default adapters and allocates the instance; until
       then there is nothing to hand back. `deps` is allowed to be NULL by the
       contract, so only `cfg` is required. */
    (void)cfg;
    (void)deps;
    return NULL;
}

void pe_solver_destroy(pe_solver_t *solver)
{
    /* Documented as safe on NULL, and it already is. */
    (void)solver;
}

/* ------------------------------------------------------------------ *
 * Validation and introspection
 * ------------------------------------------------------------------ */

pe_status_t pe_solver_validate(const pe_solver_t *solver,
                               pe_diagnostics_t *out)
{
    /* `out` is optional: a caller may want the status alone. */
    (void)out;
    if (solver == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_capabilities(const pe_solver_t *solver,
                                   uint64_t *out_caps)
{
    if (solver == NULL || out_caps == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_estimate(const pe_solver_t *solver,
                               pe_estimate_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_plan(const pe_solver_t *solver,
                           pe_execution_plan_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

/* ------------------------------------------------------------------ *
 * Execution
 * ------------------------------------------------------------------ */

pe_status_t pe_solver_run(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_pause(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_resume(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_stop(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_progress(const pe_solver_t *solver,
                               pe_progress_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

/* ------------------------------------------------------------------ *
 * Results
 * ------------------------------------------------------------------ */

pe_status_t pe_solver_strategy(const pe_solver_t *solver,
                               const pe_strategy_query_t *query,
                               pe_strategy_view_t *out)
{
    if (solver == NULL || query == NULL || out == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_metrics(const pe_solver_t *solver,
                              pe_metrics_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

/* ------------------------------------------------------------------ *
 * Persistence
 * ------------------------------------------------------------------ */

pe_status_t pe_solver_save(const pe_solver_t *solver,
                           const pe_persist_target_t *target)
{
    if (solver == NULL || target == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}

pe_status_t pe_solver_load(pe_solver_t *solver,
                           const pe_persist_source_t *source)
{
    if (solver == NULL || source == NULL)
        return PE_ERR_NULL_ARGUMENT;
    return PE_ERR_NOT_IMPLEMENTED;
}
