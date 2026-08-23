/*
 * solver.c - Solver lifecycle skeleton (architecture v3, CTR-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * CTR-01 stood the hexagon up without wiring anything into it. CTR-04 gives it
 * a real create/destroy: the instance now owns a copy of its configuration and
 * a resolved set of driven ports, where "resolved" means every port left NULL
 * by the caller has been replaced by its default adapter. Nothing downstream
 * ever tests a port for NULL — that check happens once, here.
 *
 * Nothing in this file includes cfr_core, the GPU headers or <stdio.h>: the
 * domain reaches the outside world only through the ports.
 *
 * The remaining entry points are still stubs reporting PE_SOLVER_ERR_NOT_IMPLEMENTED;
 * their argument checks are already the final behaviour and do not change when
 * a body lands.
 */

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/solver/pe_telemetry.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * The instance. Opaque to callers: everything reaches it through pe_solver.h,
 * so the layout is free to change as later tickets add the storage, the plan
 * and the run state.
 */
struct pe_solver_t {
    pe_solver_config_t config;

    /* Filled by the execution backend once a solve has produced a result.
       Keeping availability separate from the zero-valued metrics prevents a
       caller from mistaking an unrun solver for a solved zero-exploitability
       game. */
    pe_metrics_t metrics;
    int metrics_available;

    /* Resolved dependencies: no member is NULL once creation succeeds, except
       ports whose absence is meaningful (persist, where NULL means "refuse to
       save" rather than "write somewhere"). */
    pe_solver_deps_t deps;
};

/* ------------------------------------------------------------------ *
 * Lifecycle
 * ------------------------------------------------------------------ */

pe_solver_t *pe_solver_create(const pe_solver_config_t *cfg,
                              const pe_solver_deps_t *deps)
{
    pe_solver_t *solver;
    pe_telemetry_event_t event;

    /* `deps` is optional by contract; the configuration is not. */
    if (cfg == NULL)
        return NULL;

    solver = (pe_solver_t *)calloc(1, sizeof(*solver));
    if (solver == NULL)
        return NULL;

    /* The configuration is copied, so the caller may release theirs on
       return. The dependencies are not: they are borrowed pointers whose
       adapters must outlive the solver. */
    solver->config = *cfg;

    if (deps != NULL)
        solver->deps = *deps;
    else
        memset(&solver->deps, 0, sizeof(solver->deps));

    /* Resolve the ports exactly once. Substituting the sink here is what lets
       every later emit call be unconditional. */
    if (solver->deps.telemetry == NULL)
        solver->deps.telemetry = pe_telemetry_null();

    /* The first event a solver ever produces. It exists to prove the port is
       wired: a caller that installed an adapter sees it, and its absence means
       the injection silently dropped the adapter. */
    event.level = PE_LOG_INFO;
    event.category = "solver";
    event.message = "solver created";
    event.iteration = 0;
    pe_telemetry_emit(solver->deps.telemetry, &event);

    return solver;
}

void pe_solver_destroy(pe_solver_t *solver)
{
    if (solver == NULL)
        return;

    /* Give a buffering adapter its chance before the pointer goes away. */
    pe_telemetry_flush(solver->deps.telemetry);

    free(solver);
}

/* ------------------------------------------------------------------ *
 * Introspection of what was actually installed
 * ------------------------------------------------------------------ */

const pe_solver_config_t *pe_solver_get_config(const pe_solver_t *solver)
{
    if (solver == NULL)
        return NULL;
    return &solver->config;
}

const pe_telemetry_ops_t *pe_solver_get_telemetry(const pe_solver_t *solver)
{
    if (solver == NULL)
        return NULL;
    return solver->deps.telemetry;
}

/* ------------------------------------------------------------------ *
 * Validation and introspection
 * ------------------------------------------------------------------ */

/*
 * Capabilities available to a plan.
 *
 * Until the game-rules port exists there is nothing to ask, so validation
 * assumes everything is provided and checks what it can: the combination, the
 * parameter ranges and the memory budget. A capability that is genuinely
 * absent will be caught once a game can say so — the resolver already refuses
 * on missing capabilities, it is the source of the mask that is provisional.
 */
static uint64_t pe_solver_available_caps(const pe_solver_t *solver)
{
    (void)solver;
    return (uint64_t)PE_CAP_ALL;
}

pe_solver_status_t pe_solver_validate(const pe_solver_t *solver,
                               pe_diagnostics_t *out)
{
    pe_execution_plan_t plan;
    pe_estimate_t estimate;
    pe_diagnostics_t local;
    pe_diagnostics_t *diag = (out != NULL) ? out : &local;

    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    if (pe_plan_resolve(&solver->config, pe_solver_available_caps(solver),
                        &plan, diag) == PE_VALID_ERROR)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    /* Nothing has been allocated at this point, and nothing will be if the
       estimate does not fit: that is the whole point of asking first. */
    if (pe_plan_estimate(&plan, &solver->config.problem,
                         solver->config.execution.max_ram_bytes,
                         &estimate, diag) == PE_VALID_ERROR)
        return PE_SOLVER_ERR_BUDGET_EXCEEDED;

    return PE_SOLVER_OK;
}

pe_solver_status_t pe_solver_capabilities(const pe_solver_t *solver,
                                   uint64_t *out_caps)
{
    if (solver == NULL || out_caps == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

pe_solver_status_t pe_solver_estimate(const pe_solver_t *solver,
                               pe_estimate_t *out)
{
    pe_execution_plan_t plan;

    if (solver == NULL || out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    if (pe_plan_resolve(&solver->config, pe_solver_available_caps(solver),
                        &plan, NULL) == PE_VALID_ERROR)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    switch (pe_plan_estimate(&plan, &solver->config.problem,
                             solver->config.execution.max_ram_bytes, out, NULL))
    {
    case PE_VALID_ERROR:
        /* An empty problem size and a busted budget are different failures,
           and `out` distinguishes them: infosets is 0 for the first. */
        return (out->infosets == 0) ? PE_SOLVER_ERR_INVALID_CONFIG
                                    : PE_SOLVER_ERR_BUDGET_EXCEEDED;
    case PE_VALID_OK:
    case PE_VALID_WARNING:
    case PE_VALID_FALLBACK:
    default:
        return PE_SOLVER_OK;
    }
}

pe_solver_status_t pe_solver_plan(const pe_solver_t *solver,
                           pe_execution_plan_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    if (pe_plan_resolve(&solver->config, pe_solver_available_caps(solver),
                        out, NULL) == PE_VALID_ERROR)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    return PE_SOLVER_OK;
}

/* ------------------------------------------------------------------ *
 * Execution
 * ------------------------------------------------------------------ */

pe_solver_status_t pe_solver_run(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

pe_solver_status_t pe_solver_pause(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

pe_solver_status_t pe_solver_resume(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

pe_solver_status_t pe_solver_stop(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

pe_solver_status_t pe_solver_progress(const pe_solver_t *solver,
                               pe_progress_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

/* ------------------------------------------------------------------ *
 * Results
 * ------------------------------------------------------------------ */

pe_solver_status_t pe_solver_strategy(const pe_solver_t *solver,
                               const pe_strategy_query_t *query,
                               pe_strategy_view_t *out)
{
    if (solver == NULL || query == NULL || out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

pe_solver_status_t pe_solver_metrics(const pe_solver_t *solver,
                              pe_metrics_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (!solver->metrics_available)
        return PE_SOLVER_ERR_INVALID_STATE;
    *out = solver->metrics;
    return PE_SOLVER_OK;
}

/* ------------------------------------------------------------------ *
 * Persistence
 * ------------------------------------------------------------------ */

pe_solver_status_t pe_solver_save(const pe_solver_t *solver,
                           const pe_persist_target_t *target)
{
    if (solver == NULL || target == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}

pe_solver_status_t pe_solver_load(pe_solver_t *solver,
                           const pe_persist_source_t *source)
{
    if (solver == NULL || source == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    return PE_SOLVER_ERR_NOT_IMPLEMENTED;
}
