/*
 * pe_compute.h - Compute port and CPU-parallel adapter contract (PAR-03)
 */

#ifndef POKER_EVAL_PE_COMPUTE_H
#define POKER_EVAL_PE_COMPUTE_H

#include <poker_eval/solver/pe_batch.h>
#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_vector.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_compute_config_t
{
    int cpu_threads;
    int deterministic;
    size_t sample_batch_size;
    size_t terminal_batch_size;
    size_t update_batch_size;

    /* Optional storage target for apply_update_batch. A NULL pair keeps the
       adapter usable as a compute-only validator until the full solver loop
       injects its storage port. */
    const pe_storage_ops_t *storage;
    void *storage_self;
} pe_compute_config_t;

/* Batch types that will be completed by the evaluator and traversal tickets.
   Keeping them opaque lets cpu_par register now without inventing layouts. */
typedef struct pe_infoset_batch_t pe_infoset_batch_t;
typedef struct pe_strategy_batch_t pe_strategy_batch_t;
typedef struct pe_terminal_batch_t pe_terminal_batch_t;
typedef struct pe_value_batch_t pe_value_batch_t;
typedef struct pe_showdown_job_t pe_showdown_job_t;

typedef struct pe_compute_ops_t
{
    const char *name;
    uint64_t (*capabilities)(void *self);
    int (*create)(void **self, const pe_compute_config_t *cfg);
    void (*destroy)(void *self);

    int (*strategy_batch)(void *self, const pe_infoset_batch_t *in,
                          pe_strategy_batch_t *out);
    int (*apply_update_batch)(void *self, const pe_update_batch_t *batch);
    int (*terminal_eval_batch)(void *self, const pe_terminal_batch_t *in,
                               pe_value_batch_t *out);
    int (*vector_showdown)(void *self, const pe_showdown_job_t *job,
                           pe_value_vec_t *out);
    int (*sync)(void *self);
} pe_compute_ops_t;

/** The deterministic CPU-parallel adapter. */
const pe_compute_ops_t *pe_compute_cpu_par_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_COMPUTE_H */
