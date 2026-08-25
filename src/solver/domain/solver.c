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
 * The vector lifecycle is executable here, including exploitability-target
 * stopping. The scalar, sampled and tree-port entry points still return
 * PE_SOLVER_ERR_NOT_IMPLEMENTED until their traversal adapters are wired.
 */

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/solver/pe_compute.h>
#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/solver/pe_persist.h>
#include <poker_eval/solver/pe_telemetry.h>
#include <poker_eval/solver/pe_traversal.h>
#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_external_best_response.h>
#include <poker_eval/solver/pe_outcome_traversal.h>

#include <stddef.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    double *values;
    size_t capacity;
} pe_strategy_cache_t;

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
    pe_strategy_cache_t *strategy_cache;

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
    int checkpoint_loaded;
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
    solver->strategy_cache = (pe_strategy_cache_t *)calloc(1u, sizeof(*solver->strategy_cache));
    if (solver->strategy_cache == NULL)
    {
        solver->storage->destroy(solver->storage_self);
        free(solver);
        return NULL;
    }
    if (solver->config.problem.expected_actions != 0u &&
        solver->config.problem.expected_combos != 0u &&
        (size_t)solver->config.problem.expected_actions >
            SIZE_MAX / (size_t)solver->config.problem.expected_combos)
    {
        free(solver->strategy_cache);
        solver->storage->destroy(solver->storage_self);
        free(solver);
        return NULL;
    }
    solver->strategy_cache->capacity =
        (size_t)solver->config.problem.expected_actions *
        (size_t)solver->config.problem.expected_combos;
    if (solver->strategy_cache->capacity == 0u)
        solver->strategy_cache->capacity = 1u;
    solver->strategy_cache->values = (double *)calloc(
        solver->strategy_cache->capacity, sizeof(double));
    if (solver->strategy_cache->values == NULL)
    {
        free(solver->strategy_cache);
        solver->storage->destroy(solver->storage_self);
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
    event.message = "solver created\n";
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
    if (solver->strategy_cache != NULL)
    {
        free(solver->strategy_cache->values);
        free(solver->strategy_cache);
    }

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

void *pe_solver_get_storage_instance(const pe_solver_t *solver)
{
    return solver ? solver->storage_self : NULL;
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

    if (solver->config.target_exploitability_mbb > 0.0 &&
        (!isfinite(solver->config.execution.big_blind) ||
         solver->config.execution.big_blind <= 0.0))
        return PE_SOLVER_ERR_INVALID_CONFIG;

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
    const pe_compute_ops_t *compute_ops;
    pe_compute_config_t compute_config;
    void *compute_self = NULL;
    uint64_t iteration;
    uint64_t completed_iterations;
    int target_reached = 0;
    const int target_enabled =
        solver->config.target_exploitability_mbb > 0.0;
    int rc;

    if (solver->deps.vector_game == NULL ||
        plan->traversal != PE_TRAVERSAL_FULL_VECTOR ||
        (solver->config.max_iterations == 0u && !target_enabled))
        return PE_SOLVER_ERR_NOT_IMPLEMENTED;

    ops = pe_traversal_full_vector_ops();
    if (ops == NULL || pe_traversal_ctx_init(&traversal,
                                             solver->deps.vector_game) != 0)
        return PE_SOLVER_ERR_EXECUTION;
    if (pe_traversal_ctx_set_storage(&traversal, solver->storage,
                                     solver->storage_self) != 0)
    {
        pe_traversal_ctx_destroy(&traversal);
        return PE_SOLVER_ERR_EXECUTION;
    }

    compute_ops = solver->deps.compute;
    if (compute_ops == NULL)
    {
        switch (plan->stages.update)
        {
        case PE_COMPUTE_CPU_PAR:
            compute_ops = pe_compute_cpu_par_ops();
            break;
        case PE_COMPUTE_CPU_REF:
        case PE_COMPUTE_AUTO:
            compute_ops = pe_compute_cpu_ref_ops();
            break;
        case PE_COMPUTE_CUDA:
            compute_ops = pe_compute_cuda_ops();
            break;
        case PE_COMPUTE_OPENCL:
            compute_ops = pe_compute_opencl_ops();
            break;
        case PE_COMPUTE_COUNT:
        default:
            compute_ops = NULL;
            break;
        }
    }
    memset(&compute_config, 0, sizeof(compute_config));
    compute_config.cpu_threads = solver->config.execution.cpu_threads;
    compute_config.deterministic = solver->config.execution.deterministic;
    compute_config.sample_batch_size = solver->config.execution.sample_batch_size;
    compute_config.terminal_batch_size = solver->config.execution.terminal_batch_size;
    compute_config.update_batch_size = solver->config.execution.update_batch_size;
    compute_config.regret_mode = plan->regret;
    compute_config.policy_mode = plan->policy;
    compute_config.averaging_mode = plan->averaging;
    compute_config.dcfr_alpha = solver->config.algorithm.dcfr_alpha;
    compute_config.dcfr_beta = solver->config.algorithm.dcfr_beta;
    compute_config.dcfr_gamma = solver->config.algorithm.dcfr_gamma;
    compute_config.averaging_delay = solver->config.algorithm.averaging_delay;
    compute_config.exponential_lambda =
        solver->config.algorithm.exponential_lambda;
    compute_config.storage = solver->storage;
    compute_config.storage_self = solver->storage_self;
    if (compute_ops == NULL || compute_ops->create == NULL ||
        compute_ops->destroy == NULL || compute_ops->apply_update_batch == NULL ||
        compute_ops->create(&compute_self, &compute_config) != 0)
    {
        pe_traversal_ctx_destroy(&traversal);
        return PE_SOLVER_ERR_EXECUTION;
    }

    solver->state = PE_SOLVER_STATE_RUNNING;
    completed_iterations = solver->iteration;
    for (iteration = completed_iterations;
         (solver->config.max_iterations == 0u ||
          iteration < solver->config.max_iterations) && !target_reached;)
    {
        ++iteration;
        rc = ops->begin_iteration(&traversal, iteration);
        if (rc == 0)
            rc = ops->run_iteration(&traversal, &batch);
        if (rc == 0)
            rc = ops->end_iteration(&traversal, iteration);
        if (rc == 0)
            batch.iteration = iteration;
        if (rc == 0 && compute_ops->apply_update_batch(compute_self, &batch) != 0)
            rc = -1;
        if (rc != 0)
        {
            pe_update_batch_destroy(&batch);
            pe_traversal_ctx_destroy(&traversal);
            compute_ops->destroy(compute_self);
            solver->state = PE_SOLVER_STATE_STOPPED;
            return PE_SOLVER_ERR_EXECUTION;
        }
        solver->iteration = iteration;

        if (target_enabled &&
            (iteration % solver->config.exploitability_interval == 0u ||
             (solver->config.max_iterations > 0u &&
              iteration == solver->config.max_iterations)))
        {
            pe_best_response_vector_config_t br_config =
                pe_best_response_vector_config_default();
            pe_exploitability_vector_result_t result = {0};
            double gaps[PE_SOLVER_MAX_PLAYERS] = {0.0};
            uint8_t player;
            int reached = 0;

            if (pe_exploitability_vector(solver->deps.vector_game,
                                          &br_config, &result) != PE_SOLVER_OK)
            {
                pe_update_batch_destroy(&batch);
                pe_traversal_ctx_destroy(&traversal);
                compute_ops->destroy(compute_self);
                solver->state = PE_SOLVER_STATE_STOPPED;
                return PE_SOLVER_ERR_EXECUTION;
            }
            for (player = 0u;
                 player < solver->deps.vector_game->player_count; ++player)
                gaps[player] = result.br_gap[player];
            if (pe_best_response_metrics_from_multiway(
                    solver->deps.vector_game->player_count, 1, gaps, 0.0,
                    0.0, solver->config.execution.big_blind,
                    &solver->metrics) != PE_SOLVER_OK ||
                pe_best_response_target_reached(
                    solver->metrics.exploitability_mbb_per_game,
                    solver->config.target_exploitability_mbb,
                    &reached) != PE_SOLVER_OK)
            {
                pe_update_batch_destroy(&batch);
                pe_traversal_ctx_destroy(&traversal);
                compute_ops->destroy(compute_self);
                solver->state = PE_SOLVER_STATE_STOPPED;
                return PE_SOLVER_ERR_EXECUTION;
            }
            solver->metrics_available = 1;
            target_reached = reached;
        }
    }

    if (compute_ops->sync != NULL && compute_ops->sync(compute_self) != 0)
    {
        pe_update_batch_destroy(&batch);
        pe_traversal_ctx_destroy(&traversal);
        compute_ops->destroy(compute_self);
        solver->state = PE_SOLVER_STATE_STOPPED;
        return PE_SOLVER_ERR_EXECUTION;
    }
    pe_update_batch_destroy(&batch);
    pe_traversal_ctx_destroy(&traversal);
    compute_ops->destroy(compute_self);
    solver->state = PE_SOLVER_STATE_COMPLETED;
    solver->checkpoint_loaded = 0;
    return PE_SOLVER_OK;
}

typedef struct
{
    const pe_external_game_t *base;
    const pe_storage_ops_t *storage;
    void *storage_self;
    pe_policy_mode_t policy;
    pe_regret_mode_t regret;
    double exponential_lambda;
} pe_sampled_adapter_t;

static int sampled_is_terminal(const void *state, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->is_terminal(state, adapter->base->user);
}

static int sampled_acting_player(const void *state, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->acting_player(state, adapter->base->user);
}

static uint16_t sampled_action_count(const void *state, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->action_count(state, adapter->base->user);
}

static uint64_t sampled_infoset_key(const void *state, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->infoset_key
        ? adapter->base->infoset_key(state, adapter->base->user) : 0u;
}

static const void *sampled_apply_action(const void *state, uint16_t action,
                                        void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->apply_action(state, action, adapter->base->user);
}

static const void *sampled_apply_chance(const void *state, int outcome,
                                        void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->apply_chance(state, outcome, adapter->base->user);
}

static int sampled_chance_with_user(const void *state, pe_rng_t *rng,
                                    pe_chance_sample_t *out, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->sample_chance_with_user(
        state, rng, out, adapter->base->user);
}

static const void *sampled_chance_child(const void *state, pe_rng_t *rng,
                                        pe_chance_sample_t *out, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->sample_chance_child(
        state, rng, out, adapter->base->user);
}

static double sampled_action_probability(const void *state, uint64_t key,
                                         uint16_t action, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    uint16_t actions = adapter->base->action_count(state, adapter->base->user);
    pe_infoset_id_t id;
    const double *regrets;
    size_t length = 0u;
    double positive = 0.0;
    double lambda;
    if (actions == 0u || action >= actions || !adapter->storage->resolve ||
        !adapter->storage->values_const)
        return 0.0;
    id = adapter->storage->resolve(adapter->storage_self, key, actions, 1u,
                                   PE_STREET_UNKNOWN);
    if (id == PE_INFOSET_ID_INVALID)
        return 0.0;
    regrets = adapter->storage->values_const(adapter->storage_self, id,
                                             PE_VALUES_REGRET, &length);
    if (!regrets || length < actions)
        return 1.0 / (double)actions;

    if (adapter->policy == PE_POLICY_EXPONENTIAL)
    {
        double maximum = 0.0;
        double total = 0.0;

        lambda = adapter->exponential_lambda;
        if (!isfinite(lambda) || lambda <= 0.0)
            lambda = 1.0;

        /* The legacy exponential policy deliberately ignores non-positive
           regrets.  Keep that behaviour for PE_REGRET_LEGACY_EXP; the v3
           compute policy remains an all-regret softmax. */
        if (adapter->regret == PE_REGRET_LEGACY_EXP)
        {
            for (uint16_t a = 0u; a < actions; ++a)
                if (isfinite(regrets[a]) && regrets[a] > maximum)
                    maximum = regrets[a];
            if (maximum > 0.0)
            {
                for (uint16_t a = 0u; a < actions; ++a)
                    if (isfinite(regrets[a]) && regrets[a] > 0.0)
                        total += exp(lambda * (regrets[a] - maximum));
                if (isfinite(total) && total > 0.0 &&
                    isfinite(regrets[action]) && regrets[action] > 0.0)
                    return exp(lambda * (regrets[action] - maximum)) / total;
                if (isfinite(total) && total > 0.0)
                    return 0.0;
            }
            return 1.0 / (double)actions;
        }

        for (uint16_t a = 0u; a < actions; ++a)
            if (isfinite(regrets[a]) && regrets[a] > maximum)
                maximum = regrets[a];
        for (uint16_t a = 0u; a < actions; ++a)
            if (isfinite(regrets[a]))
                total += exp(lambda * (regrets[a] - maximum));
        if (isfinite(total) && total > 0.0 && isfinite(regrets[action]))
            return exp(lambda * (regrets[action] - maximum)) / total;
        return 1.0 / (double)actions;
    }

    for (uint16_t a = 0u; a < actions; ++a)
        if (isfinite(regrets[a]) && regrets[a] > 0.0) positive += regrets[a];
    if (positive <= 0.0) return 1.0 / (double)actions;
    return regrets[action] > 0.0 ? regrets[action] / positive : 0.0;
}

static double sampled_terminal_value(const void *state, int player, void *user)
{
    pe_sampled_adapter_t *adapter = (pe_sampled_adapter_t *)user;
    return adapter->base->terminal_value(state, player, adapter->base->user);
}

static pe_solver_status_t pe_solver_sampled_measure_br(
    pe_solver_t *solver, const pe_external_game_t *sampled_game,
    uint64_t iteration, int *target_reached)
{
    pe_external_br_config_t br_config = pe_external_br_config_default();
    double gaps[PE_SOLVER_MAX_PLAYERS] = {0.0};
    uint8_t player;
    int reached = 0;

    if (!solver || !sampled_game || !target_reached)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    br_config.max_depth = 4096u;
    br_config.samples = solver->config.br_samples == 0u
        ? 256u
        : solver->config.br_samples;
    br_config.seed = solver->config.seed ^ iteration;
    for (player = 0u; player < sampled_game->player_count; ++player)
    {
        pe_external_br_result_t br_result;
        if (pe_external_best_response_sampled(sampled_game, player,
                                              &br_config, &br_result) != 0)
            return PE_SOLVER_ERR_EXECUTION;
        gaps[player] = br_result.br_gap;
    }
    if (pe_best_response_metrics_from_multiway(
            sampled_game->player_count, 1, gaps, 0.0, 0.0,
            solver->config.execution.big_blind, &solver->metrics) != PE_SOLVER_OK)
        return PE_SOLVER_ERR_EXECUTION;
    solver->metrics.guarantee = PE_GUARANTEE_EMPIRICAL;
    solver->metrics_available = 1;
    /* A zero target means "run to the iteration budget", not "disable
       telemetry".  Keep measuring and publishing the empirical BR at the
       configured interval, but only evaluate early stopping when the caller
       selected an exploitability target. */
    if (solver->config.target_exploitability_mbb > 0.0 &&
        pe_best_response_target_reached(
            solver->metrics.exploitability_mbb_per_game,
            solver->config.target_exploitability_mbb, &reached) != PE_SOLVER_OK)
        return PE_SOLVER_ERR_EXECUTION;
    *target_reached = reached;
    pe_telemetry_emitf(
        solver->deps.telemetry, PE_LOG_INFO, "solver", iteration,
        "progress iteration=%" PRIu64 " total=%" PRIu64
        " fraction=%.4f exploitability_mbb=%.6f target_mbb=%.6f\n",
        iteration, solver->config.max_iterations,
        solver->config.max_iterations > 0u
            ? (double)iteration / (double)solver->config.max_iterations : 0.0,
        solver->metrics.exploitability_mbb_per_game,
        solver->config.target_exploitability_mbb);
    pe_telemetry_flush(solver->deps.telemetry);
    return PE_SOLVER_OK;
}

static void pe_solver_sampled_emit_heartbeat(
    pe_solver_t *solver, uint64_t iteration)
{
    if (!solver)
        return;
    /* BR checks are deliberately less frequent than traversal updates: a
       desktop monitor must still show the iteration counter while a sampled
       BR measurement is being accumulated.  The last measured BR is kept
       until the next check and is never presented as a new measurement. */
    pe_telemetry_emitf(
        solver->deps.telemetry, PE_LOG_INFO, "solver", iteration,
        "progress iteration=%" PRIu64 " total=%" PRIu64
        " fraction=%.4f exploitability_mbb=%.6f target_mbb=%.6f\n",
        iteration, solver->config.max_iterations,
        solver->config.max_iterations > 0u
            ? (double)iteration / (double)solver->config.max_iterations : 0.0,
        solver->metrics_available
            ? solver->metrics.exploitability_mbb_per_game : 0.0,
        solver->config.target_exploitability_mbb);
    pe_telemetry_flush(solver->deps.telemetry);
}

/* Lane B keeps the state space sampled instead of expanding every private
 * deal and every future board.  This is the execution path intended for wide
 * preflop ranges: the external game owns the deal/range sampler, while the
 * solver still owns iteration, storage and compute/update dispatch. */
static pe_solver_status_t pe_solver_run_sampled(pe_solver_t *solver,
                                                 const pe_execution_plan_t *plan)
{
    const pe_external_game_t *game = solver->deps.external_game;
    const pe_compute_ops_t *compute_ops;
    pe_compute_config_t compute_config;
    void *compute_self = NULL;
    pe_update_batch_t batch = {0};
    pe_update_batch_t aggregated_batch = {0};
    pe_external_sampling_ctx_t external = {0};
    pe_outcome_sampling_ctx_t outcome = {0};
    pe_sampled_adapter_t adapter;
    pe_external_game_t sampled_game;
    uint64_t iteration;
    int use_outcome = plan->traversal == PE_TRAVERSAL_OUTCOME_SAMPLING;
    int target_reached = 0;
    int rc;

    if (!game || !game->root || solver->config.max_iterations == 0u ||
        !game->is_terminal || !game->acting_player || !game->action_count ||
        !game->apply_action || !game->terminal_value ||
        ((game->sample_chance || game->sample_chance_with_user) &&
         !game->apply_chance && !game->sample_chance_child))
        return PE_SOLVER_ERR_NOT_IMPLEMENTED;
    compute_ops = solver->deps.compute;
    if (compute_ops == NULL)
    {
        switch (plan->stages.update)
        {
        case PE_COMPUTE_CPU_PAR:
            compute_ops = pe_compute_cpu_par_ops();
            break;
        case PE_COMPUTE_CPU_REF:
        case PE_COMPUTE_AUTO:
            compute_ops = pe_compute_cpu_ref_ops();
            break;
        case PE_COMPUTE_CUDA:
            compute_ops = pe_compute_cuda_ops();
            break;
        case PE_COMPUTE_OPENCL:
            compute_ops = pe_compute_opencl_ops();
            break;
        case PE_COMPUTE_COUNT:
        default:
            compute_ops = NULL;
            break;
        }
    }
    memset(&compute_config, 0, sizeof(compute_config));
    compute_config.cpu_threads = solver->config.execution.cpu_threads;
    compute_config.deterministic = solver->config.execution.deterministic;
    compute_config.sample_batch_size = solver->config.execution.sample_batch_size;
    compute_config.terminal_batch_size = solver->config.execution.terminal_batch_size;
    compute_config.update_batch_size = solver->config.execution.update_batch_size;
    compute_config.regret_mode = plan->regret;
    compute_config.policy_mode = plan->policy;
    compute_config.averaging_mode = plan->averaging;
    compute_config.dcfr_alpha = solver->config.algorithm.dcfr_alpha;
    compute_config.dcfr_beta = solver->config.algorithm.dcfr_beta;
    compute_config.dcfr_gamma = solver->config.algorithm.dcfr_gamma;
    compute_config.averaging_delay = solver->config.algorithm.averaging_delay;
    compute_config.exponential_lambda =
        solver->config.algorithm.exponential_lambda;
    compute_config.storage = solver->storage;
    compute_config.storage_self = solver->storage_self;
    if (!compute_ops || !compute_ops->create || !compute_ops->destroy ||
        !compute_ops->apply_update_batch ||
        compute_ops->create(&compute_self, &compute_config) != 0)
        return PE_SOLVER_ERR_EXECUTION;

    adapter.base = game;
    adapter.storage = solver->storage;
    adapter.storage_self = solver->storage_self;
    adapter.policy = plan->policy;
    adapter.regret = plan->regret;
    adapter.exponential_lambda = solver->config.algorithm.exponential_lambda;
    memset(&sampled_game, 0, sizeof(sampled_game));
    sampled_game.root = game->root;
    sampled_game.user = &adapter;
    sampled_game.player_count = game->player_count;
    sampled_game.is_terminal = sampled_is_terminal;
    sampled_game.acting_player = sampled_acting_player;
    sampled_game.action_count = sampled_action_count;
    sampled_game.infoset_key = sampled_infoset_key;
    sampled_game.apply_action = sampled_apply_action;
    sampled_game.action_probability = sampled_action_probability;
    sampled_game.terminal_value = sampled_terminal_value;
    sampled_game.sample_chance = game->sample_chance;
    sampled_game.sample_chance_with_user = game->sample_chance_with_user
        ? sampled_chance_with_user : NULL;
    sampled_game.sample_chance_child = game->sample_chance_child
        ? sampled_chance_child : NULL;
    sampled_game.apply_chance = game->apply_chance ? sampled_apply_chance : NULL;

    if (use_outcome)
        rc = pe_outcome_sampling_ctx_init(
            &outcome, &sampled_game, solver->storage, solver->storage_self, 0,
            solver->config.algorithm.outcome_epsilon,
            solver->config.seed);
    else
        rc = pe_external_sampling_ctx_init(
            &external, &sampled_game, solver->storage, solver->storage_self, 0,
            solver->config.seed);
    if (rc != 0)
    {
        compute_ops->destroy(compute_self);
        return PE_SOLVER_ERR_EXECUTION;
    }

    solver->state = PE_SOLVER_STATE_RUNNING;
    {
        uint64_t heartbeat_interval = solver->config.exploitability_interval;
        if (heartbeat_interval == 0u)
            heartbeat_interval = 16u;
        else if (heartbeat_interval > 16u)
            heartbeat_interval /= 16u;
        if (heartbeat_interval == 0u)
            heartbeat_interval = 1u;
    for (iteration = solver->iteration + 1u;
         iteration <= solver->config.max_iterations &&
         solver->state == PE_SOLVER_STATE_RUNNING && !target_reached;
         ++iteration)
    {
        int updating_player = (int)((iteration - 1u) % game->player_count);
        /* A sampled Lane B iteration may contain several independent
           trajectories.  Keep the traversal state local to the thread, merge
           its updates once, and cross the compute port once.  The old value
           (zero) deliberately means one trajectory, preserving the reference
           behaviour while making the configured batch size useful for wide
           preflop ranges. */
        size_t samples = solver->config.execution.sample_batch_size;
        if (samples == 0u) samples = 1u;
        pe_update_batch_clear(&aggregated_batch);
        rc = 0;
        for (size_t sample_index = 0u; sample_index < samples; ++sample_index)
        {
            if (use_outcome)
            {
                outcome.updating_player = updating_player;
                rc = pe_outcome_sampling_run(&outcome, &batch);
            }
            else
            {
                external.updating_player = updating_player;
                rc = pe_external_sampling_run(&external, &batch);
            }
            if (rc != 0 || pe_update_batch_merge(&aggregated_batch, &batch) != 0)
            {
                rc = -1;
                break;
            }
        }
        if (rc == 0) {
            aggregated_batch.iteration = iteration;
            if (compute_ops->apply_update_batch(
                    compute_self, &aggregated_batch) != 0)
                rc = -1;
        }
        if (rc != 0)
        {
            pe_update_batch_destroy(&batch);
            pe_update_batch_destroy(&aggregated_batch);
            if (use_outcome) pe_outcome_sampling_ctx_destroy(&outcome);
            else pe_external_sampling_ctx_destroy(&external);
            compute_ops->destroy(compute_self);
            solver->state = PE_SOLVER_STATE_STOPPED;
            return PE_SOLVER_ERR_EXECUTION;
        }
        solver->iteration = iteration;

        if (solver->config.exploitability_interval > 0u &&
            (iteration % solver->config.exploitability_interval == 0u ||
             iteration == solver->config.max_iterations))
        {
            if (pe_solver_sampled_measure_br(solver, &sampled_game,
                                             iteration, &target_reached) != PE_SOLVER_OK)
            {
                pe_update_batch_destroy(&batch);
                pe_update_batch_destroy(&aggregated_batch);
                if (use_outcome) pe_outcome_sampling_ctx_destroy(&outcome);
                else pe_external_sampling_ctx_destroy(&external);
                compute_ops->destroy(compute_self);
                solver->state = PE_SOLVER_STATE_STOPPED;
                return PE_SOLVER_ERR_EXECUTION;
            }
        }
        else if (iteration == 1u || iteration % heartbeat_interval == 0u ||
                 iteration == solver->config.max_iterations)
        {
            pe_solver_sampled_emit_heartbeat(solver, iteration);
        }
    }
    }
    if (solver->state == PE_SOLVER_STATE_STOPPED)
    {
        pe_update_batch_destroy(&batch);
        pe_update_batch_destroy(&aggregated_batch);
        if (use_outcome) pe_outcome_sampling_ctx_destroy(&outcome);
        else pe_external_sampling_ctx_destroy(&external);
        compute_ops->destroy(compute_self);
        return PE_SOLVER_OK;
    }
    if (compute_ops->sync && compute_ops->sync(compute_self) != 0)
    {
        pe_update_batch_destroy(&batch);
        pe_update_batch_destroy(&aggregated_batch);
        if (use_outcome) pe_outcome_sampling_ctx_destroy(&outcome);
        else pe_external_sampling_ctx_destroy(&external);
        compute_ops->destroy(compute_self);
        solver->state = PE_SOLVER_STATE_STOPPED;
        return PE_SOLVER_ERR_EXECUTION;
    }

    /* Lane B cannot enumerate every private deal, so its BR is explicitly an
       empirical measurement. It is still useful for a configured target and
       for reporting: the caller receives a sampled empirical gap rather
       than a false exact/Nash claim. */
    if (!solver->metrics_available)
    {
        if (pe_solver_sampled_measure_br(solver, &sampled_game,
                                         solver->iteration,
                                         &target_reached) != PE_SOLVER_OK)
        {
            pe_update_batch_destroy(&batch);
            pe_update_batch_destroy(&aggregated_batch);
            if (use_outcome) pe_outcome_sampling_ctx_destroy(&outcome);
            else pe_external_sampling_ctx_destroy(&external);
            compute_ops->destroy(compute_self);
            solver->state = PE_SOLVER_STATE_STOPPED;
            return PE_SOLVER_ERR_EXECUTION;
        }
    }
    pe_update_batch_destroy(&batch);
    pe_update_batch_destroy(&aggregated_batch);
    if (use_outcome) pe_outcome_sampling_ctx_destroy(&outcome);
    else pe_external_sampling_ctx_destroy(&external);
    compute_ops->destroy(compute_self);
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
    if (!solver->checkpoint_loaded)
        solver->iteration = 0u;

    if (pe_solver_plan(solver, &plan) != PE_SOLVER_OK)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    if (plan.traversal == PE_TRAVERSAL_EXTERNAL_SAMPLING ||
        plan.traversal == PE_TRAVERSAL_OUTCOME_SAMPLING)
        return pe_solver_run_sampled(solver, &plan);
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
        !pe_storage_serves(solver->storage, PE_VALUES_AVERAGE) ||
        solver->strategy_cache == NULL || solver->strategy_cache->values == NULL)
        return PE_SOLVER_ERR_EXECUTION;

    memset(out, 0, sizeof(*out));
    if (solver->storage->shape(solver->storage_self, query->infoset,
                               &out->action_count, &out->combo_count,
                               NULL) != 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    const double *average = solver->storage->values_const(
        solver->storage_self, query->infoset, PE_VALUES_AVERAGE, &out->count);
    if (average == NULL || out->count > solver->strategy_cache->capacity)
        return PE_SOLVER_ERR_INVALID_STATE;
    for (uint16_t combo = 0u; combo < out->combo_count; ++combo)
    {
        double total = 0.0;
        for (uint16_t action = 0u; action < out->action_count; ++action)
        {
            size_t slot = pe_storage_slot_at(out->combo_count, action, combo);
            if (!isfinite(average[slot]) || average[slot] < 0.0)
                return PE_SOLVER_ERR_EXECUTION;
            total += average[slot];
        }
        for (uint16_t action = 0u; action < out->action_count; ++action)
        {
            size_t slot = pe_storage_slot_at(out->combo_count, action, combo);
            solver->strategy_cache->values[slot] = total > 0.0
                ? average[slot] / total
                : 1.0 / (double)out->action_count;
        }
    }
    out->values = solver->strategy_cache->values;
    return PE_SOLVER_OK;
}

size_t pe_solver_strategy_count(const pe_solver_t *solver)
{
    if (solver == NULL || solver->storage == NULL ||
        solver->storage->count == NULL)
        return 0u;
    return solver->storage->count(solver->storage_self);
}

pe_solver_status_t pe_solver_strategy_key_at(const pe_solver_t *solver,
                                             uint32_t infoset,
                                             uint64_t *out_key)
{
    if (solver == NULL || out_key == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (solver->storage == NULL || solver->storage->key_at == NULL)
        return PE_SOLVER_ERR_EXECUTION;
    if (solver->storage->key_at(solver->storage_self, infoset, out_key) != 0)
        return PE_SOLVER_ERR_INVALID_CONFIG;
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
    if (solver->deps.persist == NULL || solver->deps.persist->save == NULL)
        return PE_SOLVER_ERR_EXECUTION;
    if (solver->state != PE_SOLVER_STATE_COMPLETED &&
        solver->state != PE_SOLVER_STATE_PAUSED &&
        solver->state != PE_SOLVER_STATE_STOPPED)
        return PE_SOLVER_ERR_INVALID_STATE;
    return solver->deps.persist->save(NULL, target, &solver->config,
                                      solver->storage, solver->storage_self,
                                      solver->iteration) == 0
        ? PE_SOLVER_OK : PE_SOLVER_ERR_EXECUTION;
}

pe_solver_status_t pe_solver_load(pe_solver_t *solver,
                           const pe_persist_source_t *source)
{
    if (solver == NULL || source == NULL)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    if (solver->deps.persist == NULL || solver->deps.persist->load == NULL)
        return PE_SOLVER_ERR_EXECUTION;
    if (solver->state != PE_SOLVER_STATE_CREATED &&
        solver->state != PE_SOLVER_STATE_VALIDATED)
        return PE_SOLVER_ERR_INVALID_STATE;
    if (solver->deps.persist->load(NULL, source, &solver->config,
                                   solver->storage, solver->storage_self,
                                   &solver->iteration) != 0)
        return PE_SOLVER_ERR_EXECUTION;
    solver->state = PE_SOLVER_STATE_CREATED;
    solver->checkpoint_loaded = 1;
    return PE_SOLVER_OK;
}
