/*
 * pe_work_coordinator.h - heterogeneous worker registry and dispatcher
 */

#ifndef POKER_EVAL_PE_WORK_COORDINATOR_H
#define POKER_EVAL_PE_WORK_COORDINATOR_H

#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/solver/pe_work_protocol.h>
#include <poker_eval/solver/pe_work_reducer.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_WORK_COORDINATOR_MAX_WORKERS 32u

typedef struct pe_work_worker_t
{
    uint32_t worker_id;
    pe_runtime_capabilities_t runtime;
} pe_work_worker_t;

typedef struct pe_work_coordinator_t
{
    pe_work_worker_t workers[PE_WORK_COORDINATOR_MAX_WORKERS];
    size_t worker_count;
} pe_work_coordinator_t;

typedef struct pe_work_worker_assignment_t
{
    uint32_t worker_id;
    pe_compute_kind_t backend;
    size_t first_unit;
    size_t unit_count;
    double units_per_s;
} pe_work_worker_assignment_t;

typedef struct pe_work_worker_channel_t
{
    uint32_t worker_id;
    pe_work_socket_t socket;
} pe_work_worker_channel_t;

void pe_work_coordinator_init(pe_work_coordinator_t *coordinator);

/** Add or replace a worker announcement identified by worker_id. */
int pe_work_coordinator_register(pe_work_coordinator_t *coordinator,
                                 uint32_t worker_id,
                                 const pe_runtime_capabilities_t *runtime);

/** Receive a worker announcement on a channel and register its capabilities. */
int pe_work_coordinator_accept_announcement(
    pe_work_coordinator_t *coordinator,
    uint32_t worker_id,
    pe_work_socket_t socket);

/** Accept one TCP worker connection and consume its capability announcement. */
int pe_work_coordinator_accept_tcp(pe_work_coordinator_t *coordinator,
                                   pe_work_socket_t listener,
                                   uint32_t worker_id,
                                   pe_work_worker_channel_t *out_channel);

/** Accept a bounded set of persistent worker connections. */
int pe_work_coordinator_accept_tcp_batch(
    pe_work_coordinator_t *coordinator,
    pe_work_socket_t listener,
    uint32_t first_worker_id,
    pe_work_worker_channel_t *out_channels,
    size_t capacity,
    size_t *out_count);

/** Remove a worker; returns -1 when the id is not registered. */
int pe_work_coordinator_unregister(pe_work_coordinator_t *coordinator,
                                   uint32_t worker_id);

/**
 * Schedule concrete unit ranges across registered workers. Each worker uses
 * its own recommended backend; workers with no usable backend are skipped.
 */
int pe_work_coordinator_schedule(
    const pe_work_coordinator_t *coordinator,
    size_t total_units,
    pe_work_worker_assignment_t *out,
    size_t capacity);

/**
 * Schedule and send a contiguous WorkUnit array over registered worker
 * channels. On success, `out` describes the ranges that were sent.
 */
int pe_work_coordinator_dispatch(
    const pe_work_coordinator_t *coordinator,
    const pe_work_unit_t *units,
    size_t unit_count,
    const pe_work_worker_channel_t *channels,
    size_t channel_count,
    pe_work_worker_assignment_t *out,
    size_t capacity);

/**
 * Schedule work and collect each result while dispatching the next unit.
 *
 * This is the safe entry point for persistent workers: it bounds in-flight
 * protocol data to one unit per channel and cannot deadlock when result
 * deltas are larger than the socket buffers.
 */
int pe_work_coordinator_dispatch_and_collect(
    const pe_work_coordinator_t *coordinator,
    const pe_work_unit_t *units,
    size_t unit_count,
    const pe_work_worker_channel_t *channels,
    size_t channel_count,
    pe_work_worker_assignment_t *out,
    size_t capacity,
    pe_work_reducer_t *reducer);

/**
 * Collect one RESULT per dispatched WorkUnit and append it to `reducer`.
 * Results are checked against the original unit metadata before acceptance.
 */
int pe_work_coordinator_collect_results(
    const pe_work_unit_t *units,
    size_t unit_count,
    const pe_work_worker_assignment_t *assignments,
    size_t assignment_count,
    const pe_work_worker_channel_t *channels,
    size_t channel_count,
    pe_work_reducer_t *reducer);

/** Send SHUTDOWN to all supplied persistent worker channels. */
int pe_work_coordinator_shutdown(
    const pe_work_worker_channel_t *channels, size_t channel_count);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_COORDINATOR_H */
