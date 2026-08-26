/*
 * pe_work_executor.h - one-shot worker execution boundary
 */

#ifndef POKER_EVAL_PE_WORK_EXECUTOR_H
#define POKER_EVAL_PE_WORK_EXECUTOR_H

#include <poker_eval/solver/pe_work_protocol.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pe_work_execute_fn)(const pe_work_unit_t *unit,
                                  pe_compute_kind_t backend,
                                  pe_work_result_t *out_result,
                                  void *user_data);

/** Select the fastest usable local backend from a runtime descriptor. */
pe_compute_kind_t pe_work_worker_backend(
    const pe_runtime_capabilities_t *runtime);

/** Send the worker's runtime announcement before accepting WorkUnits. */
int pe_work_worker_announce(pe_work_socket_t socket,
                            const pe_runtime_capabilities_t *runtime);

/**
 * Receive one UNIT, execute it through the callback and send one RESULT.
 * The callback owns any temporary delta storage until it returns.
 */
int pe_work_worker_run_once(pe_work_socket_t socket,
                            const pe_runtime_capabilities_t *runtime,
                            pe_work_execute_fn execute,
                            void *user_data);

/** Execute exactly `unit_count` sequential units; `processed` is optional. */
int pe_work_worker_run_batch(pe_work_socket_t socket,
                             const pe_runtime_capabilities_t *runtime,
                             pe_work_execute_fn execute,
                             void *user_data,
                             size_t unit_count,
                             size_t *processed);

/** Announce, execute and close a complete worker session. */
int pe_work_worker_serve(pe_work_socket_t socket,
                         const pe_runtime_capabilities_t *runtime,
                         pe_work_execute_fn execute,
                         void *user_data,
                         size_t unit_count,
                         size_t *processed);

/**
 * Announce and serve multiple units until an explicit SHUTDOWN frame arrives.
 * The connection remains open between units; the caller closes it afterwards.
 */
int pe_work_worker_serve_forever(pe_work_socket_t socket,
                                 const pe_runtime_capabilities_t *runtime,
                                 pe_work_execute_fn execute,
                                 void *user_data,
                                 size_t *processed);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_EXECUTOR_H */
