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
#include <poker_eval/solver/pe_traversal.h>

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

    /* The port describes the adapter; the instance is owned by this solver.
       Keeping the pair here makes the default RAM adapter real rather than a
       promise in pe_ports.h, and gives result queries one stable store. */
    const pe_storage_ops_t *storage;
    void *storage_self;

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
    int state;
    uint64_t iteration;
};

enum {
    PE_SOLVER_STATE_CREATED = 0,
    PE_SOLVER_STATE_VALIDATED,
    PE_SOLVER_STATE_RUNNING,
    PE_SOLVER_STATE_PAUSED,
    PE_SOLVER_STATE_STOPPED,
    PE_SOLVER_STATE_COMPLETED
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
    solver->state = PE_SOLVER_STATE_CREATED;

    if (deps != NULL)
        solver->deps = *deps;
    else
        memset(&solver->deps, 0, sizeof(solver->deps));

    solver->storage = solver->deps.storage != NULL
        ? solver->deps.storage : pe_storage_ram_ops();
    if (solver->storage == NULL || solver->storage->create == NULL ||
        solver->storage->destroy == NULL ||
        solver->storage->create(&solver->storage_self,
                                (size_t)solver->config.problem.expected_infosets) != 0)
    {
        free(solver);
        return NULL;
    }
    /* The compute adapters consume the same resolved pair once the execution
       driver is installed. Keep the dependency view coherent for accessors
       and for that next tranche. */
    solver->deps.storage = solver->storage;

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

    if (solver->state == PE_SOLVER_STATE_RUNNING ||
        solver->state == PE_SOLVER_STATE_PAUSED)
        solver->state = PE_SOLVER_STATE_STOPPED;

    /* Give a buffering adapter its chance before the pointer goes away. */
    pe_telemetry_flush(solver->deps.telemetry);

    if (solver->storage != NULL && solver->storage->destroy != NULL)
        solver->storage->destroy(solver->storage_self);

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

const pe_storage_ops_t *pe_solver_get_storage(const pe_solver_t *solver)
{
    if (solver == NULL)
        return NULL;
    return solver->storage;
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

static uint64_t pe_solver_validation_caps(const pe_solver_t *solver)
{
    uint64_t caps = pe_solver_available_caps(solver);

    if (!pe_gpu_terminal_eval_gate_is_open())
        caps &= ~((uint64_t)PE_CAP_GPU_TERMINAL_EVAL);
    if (!pe_gpu_regret_update_gate_is_open())
        caps &= ~((uint64_t)PE_CAP_GPU_REGRET_UPDATE);
    return caps;
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

    if (pe_plan_resolve(&solver->config, pe_solver_validation_caps(solver),
                        &plan, diag) == PE_VALID_ERROR)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    /* Nothing has been allocated at this point, and nothing will be if the
       estimate does not fit: that is the whole point of asking first. */
    if (pe_plan_estimate(&plan, &solver->config.problem,
                         solver->config.execution.max_ram_bytes,
                         &estimate, diag) == PE_VALID_ERROR)
        return (estimate.infosets == 0) ? PE_SOLVER_ERR_INVALID_CONFIG
                                        : PE_SOLVER_ERR_BUDGET_EXCEEDED;

    return PE_SOLVER_OK;
}

pe_solver_status_t pe_solver_capabilities(const pe_solver_t *solver,
                                   uint64_t *out_caps)
{
    if (solver == NULL || out_caps == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    *out_caps = pe_solver_available_caps(solver);
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_solver_estimate(const pe_solver_t *solver,
                               pe_estimate_t *out)
{
    pe_execution_plan_t plan;

    if (solver == NULL || out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    if (pe_plan_resolve(&solver->config, pe_solver_validation_caps(solver),
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

    if (pe_plan_resolve(&solver->config, pe_solver_validation_caps(solver),
                        out, NULL) == PE_VALID_ERROR)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    return PE_SOLVER_OK;
}

static pe_solver_status_t pe_solver_run_vector(pe_solver_t *solver,
                                                const pe_execution_plan_t *plan)
{
    pe_traversal_ctx_t traversal;
    pe_update_batch_t batch = {0};
    const pe_traversal_ops_t *ops;
    uint64_t iteration;
    int rc;

    if (solver->deps.vector_game == NULL ||
        plan->traversal != PE_TRAVERSAL_FULL_VECTOR ||
        solver->config.max_iterations == 0u ||
        solver->config.target_exploitability_mbb > 0.0)
        return PE_SOLVER_ERR_NOT_IMPLEMENTED;

    ops = pe_traversal_full_vector_ops();
    if (ops == NULL || pe_traversal_ctx_init(&traversal,
                                             solver->deps.vector_game) != 0)
        return PE_SOLVER_ERR_EXECUTION;

    solver->state = PE_SOLVER_STATE_RUNNING;
    for (iteration = 1u; iteration <= solver->config.max_iterations; ++iteration)
    {
        rc = ops->begin_iteration(&traversal, iteration);
        if (rc == 0)
            rc = ops->run_iteration(&traversal, &batch);
        if (rc == 0)
            rc = ops->end_iteration(&traversal, iteration);
        if (rc != 0)
        {
            pe_update_batch_destroy(&batch);
            pe_traversal_ctx_destroy(&traversal);
            solver->state = PE_SOLVER_STATE_STOPPED;
            return PE_SOLVER_ERR_EXECUTION;
        }
        solver->iteration = iteration;
    }

    pe_update_batch_destroy(&batch);
    pe_traversal_ctx_destroy(&traversal);
    solver->state = PE_SOLVER_STATE_COMPLETED;
    return PE_SOLVER_OK;
}

/* ------------------------------------------------------------------ *
 * Execution
 * ------------------------------------------------------------------ */

pe_solver_status_t pe_solver_run(pe_solver_t *solver)
{
    pe_solver_status_t validation;
    pe_execution_plan_t plan;

    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;

    /* Validation is deliberately pure, so CREATED is also the state after a
       successful pe_solver_validate() call.  A run is nevertheless a
       one-shot lifecycle transition: once dispatch was attempted, accepting
       another call would make a future backend allocate or append a second
       solve on the same instance without an explicit reset contract. */
    if (solver->state != PE_SOLVER_STATE_CREATED)
        return PE_SOLVER_ERR_INVALID_STATE;

    /* Do not dispatch an execution backend for a plan that cannot be
       honoured. Once a real backend is installed, this preflight remains the
       first gate and the NOT_IMPLEMENTED result below is replaced by the
       iteration driver. */
    validation = pe_solver_validate(solver, NULL);
    if (validation != PE_SOLVER_OK)
        return validation;
    solver->state = PE_SOLVER_STATE_VALIDATED;
    solver->iteration = 0u;

    if (pe_solver_plan(solver, &plan) != PE_SOLVER_OK)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    return pe_solver_run_vector(solver, &plan);
}

pe_solver_status_t pe_solver_pause(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (solver->state == PE_SOLVER_STATE_PAUSED)
        return PE_SOLVER_OK;
    if (solver->state != PE_SOLVER_STATE_RUNNING)
        return PE_SOLVER_ERR_INVALID_STATE;
    solver->state = PE_SOLVER_STATE_PAUSED;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_solver_resume(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (solver->state == PE_SOLVER_STATE_RUNNING)
        return PE_SOLVER_OK;
    if (solver->state != PE_SOLVER_STATE_PAUSED)
        return PE_SOLVER_ERR_INVALID_STATE;
    solver->state = PE_SOLVER_STATE_RUNNING;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_solver_stop(pe_solver_t *solver)
{
    if (solver == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (solver->state == PE_SOLVER_STATE_STOPPED)
        return PE_SOLVER_OK;
    if (solver->state != PE_SOLVER_STATE_RUNNING &&
        solver->state != PE_SOLVER_STATE_PAUSED)
        return PE_SOLVER_ERR_INVALID_STATE;
    solver->state = PE_SOLVER_STATE_STOPPED;
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_solver_progress(const pe_solver_t *solver,
                               pe_progress_t *out)
{
    if (solver == NULL || out == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (solver->state == PE_SOLVER_STATE_CREATED)
        return PE_SOLVER_ERR_INVALID_STATE;
    memset(out, 0, sizeof(*out));
    out->iteration = solver->iteration;
    out->total_iterations = solver->config.max_iterations;
    if (out->total_iterations > 0u)
        out->fraction = (double)out->iteration /
                        (double)out->total_iterations;
    out->running = solver->state == PE_SOLVER_STATE_RUNNING;
    out->paused = solver->state == PE_SOLVER_STATE_PAUSED;
    out->complete = solver->state == PE_SOLVER_STATE_COMPLETED;
    return PE_SOLVER_OK;
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
    if (solver->state != PE_SOLVER_STATE_COMPLETED)
        return PE_SOLVER_ERR_INVALID_STATE;
    if (solver->storage == NULL || solver->storage->shape == NULL ||
        solver->storage->values_const == NULL ||
        !pe_storage_serves(solver->storage, PE_VALUES_AVERAGE))
        return PE_SOLVER_ERR_EXECUTION;

    memset(out, 0, sizeof(*out));
    if (solver->storage->shape(solver->storage_self, query->infoset,
                               &out->action_count, &out->combo_count,
                               NULL) != 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    out->values = solver->storage->values_const(
        solver->storage_self, query->infoset, PE_VALUES_AVERAGE, &out->count);
    if (out->values == NULL)
        return PE_SOLVER_ERR_INVALID_STATE;
    return PE_SOLVER_OK;
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
