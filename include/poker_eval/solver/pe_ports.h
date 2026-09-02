/*
 * pe_ports.h - Driven ports and dependency injection (architecture v3, CTR-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The right-hand side of the hexagon. The domain declares what it needs —
 * compute, hand evaluation, storage, persistence, telemetry — and receives
 * implementations; it never chooses them. That inversion is what lets the same
 * solve run on a reference CPU backend, on CUDA, or against a mocked storage in
 * a test, without a line of the domain changing.
 *
 * Only the telemetry port is defined so far (CTR-04). The other four are
 * forward-declared here and completed by the tickets that introduce them:
 *
 *   pe_compute_ops_t     GPU-01  (pe_compute.h)
 *   pe_evaluator_ops_t   GPU-02  (pe_evaluator.h)
 * *   pe_persist_ops_t     API-04  (pe_persist.h)
 *
 * A port left NULL means "use the default adapter", not "misconfigured". The
 * solver substitutes a default at creation, so nothing downstream tests for
 * NULL.
 */

#ifndef POKER_EVAL_PE_PORTS_H
#define POKER_EVAL_PE_PORTS_H

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_storage_port.h>
#include <poker_eval/solver/pe_telemetry.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Completed by the tickets listed above. pe_storage_ops_t is no longer among
   them: STO-03 defined it, and repeating the typedef here is a constraint
   violation in C99 — the very trap the forward declarations in pe_solver.h are
   arranged to avoid. The header that defines a port is the one that declares
   it. */
#ifndef POKER_EVAL_PE_COMPUTE_OPS_T_DEFINED
typedef struct pe_compute_ops_t   pe_compute_ops_t;
#define POKER_EVAL_PE_COMPUTE_OPS_T_DEFINED
#endif
typedef struct pe_evaluator_ops_t pe_evaluator_ops_t;
#ifndef POKER_EVAL_PE_PERSIST_OPS_T_DEFINED
typedef struct pe_persist_ops_t   pe_persist_ops_t;
#define POKER_EVAL_PE_PERSIST_OPS_T_DEFINED
#endif
struct pe_vector_game_t;
struct pe_external_game_t;

/*
 * Completes the tag forward-declared in pe_solver.h.
 *
 * Every member is a borrowed pointer: the solver keeps it but never owns it,
 * so each adapter must outlive the solver it was handed to. That is the price
 * of not allocating on the caller's behalf, and it keeps ownership obvious.
 *
 * Unlike pe_solver_config_t, a zero-initialised pe_solver_deps_t IS valid and
 * meaningful: it asks for the default adapter on every port.
 */
struct pe_solver_deps_t {
    /* Batch operations: strategy, updates, terminal evaluation, showdown.
       NULL selects the reference CPU backend. */
    const pe_compute_ops_t *compute;

    /* Hand evaluation, wrapping the existing equity engine and its GPU batch
       paths. NULL selects the CPU evaluator. */
    const pe_evaluator_ops_t *evaluator;

    /* Where regrets and average strategies live. NULL selects RAM. */
    const pe_storage_ops_t *storage;

    /* Checkpoints and solved-strategy artefacts. NULL disables persistence,
       which makes pe_solver_save and pe_solver_load fail rather than write
       somewhere unexpected. */
    const pe_persist_ops_t *persist;

    /* NULL selects pe_telemetry_null(), so the domain's emit path is the same
       whether or not anyone is listening. */
    const pe_telemetry_ops_t *telemetry;

    /* Optional vector-lane game used by the first executable solver driver.
       Appended to preserve the layout of legacy positional initializers. NULL
       keeps plan-only and legacy callers on the existing path until the
       complete game-rules port is available. */
    const struct pe_vector_game_t *vector_game;

    /* Optional Lane-B game used by external/outcome sampling.  The game is
       borrowed exactly like vector_game and is selected by the traversal
       axis in the resolved plan.  Keeping this port separate prevents a
       sampled preflop solve from being forced through the exhaustive vector
       surface. */
    const struct pe_external_game_t *external_game;
};

/**
 * Dependencies with every port left to its default.
 *
 * Equivalent to a zeroed structure; it exists so calling code can be explicit
 * about asking for defaults instead of relying on a reader knowing that zero
 * is meaningful here and is not in pe_solver_config_t.
 */
pe_solver_deps_t pe_solver_deps_default(void);

/**
 * The telemetry adapter the solver resolved to.
 *
 * Never NULL for a live solver: a caller who supplied none gets
 * pe_telemetry_null(). Reading it back is how a host — or a test — confirms
 * its adapter was installed rather than silently dropped, which is otherwise
 * indistinguishable from the sink because both are silent.
 *
 * Declared here rather than in pe_solver.h because it is a question about the
 * ports, and this is the header that knows them.
 *
 * @return The adapter, or NULL when `solver` is NULL.
 */
const pe_telemetry_ops_t *pe_solver_get_telemetry(const pe_solver_t *solver);

/**
 * The storage adapter resolved for a live solver.
 *
 * The adapter is borrowed from the solver and remains valid until destroy.
 * This accessor is intentionally read-only: callers inspect capabilities and
 * names here, while infoset reads go through pe_solver_strategy().
 */
const pe_storage_ops_t *pe_solver_get_storage(const pe_solver_t *solver);

/* Borrowed storage instance for result evaluators. Valid until destroy. */
void *pe_solver_get_storage_instance(const pe_solver_t *solver);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_PORTS_H */
