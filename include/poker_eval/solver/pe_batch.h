/*
 * pe_batch.h - Thread-local update batches (PAR-01)
 */

#ifndef POKER_EVAL_PE_BATCH_H
#define POKER_EVAL_PE_BATCH_H

#include <poker_eval/solver/pe_storage_port.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One raw update emitted by a traversal. `delta` is the regret delta kept for
   compatibility with the VEC-03 traversal port; average_delta is the sibling
   contribution that PAR-01 keeps in the same thread-local batch. */
typedef struct
{
    pe_infoset_id_t infoset;
    uint16_t action;
    uint16_t combo;
    double delta;
    double average_delta;
} pe_update_t;

/*
 * Dense update representation used by the vector traversal.  All values for
 * one infoset are contiguous in action-major order, so the infoset id and
 * shape are paid once instead of once per (action, combo) pair.
 *
 * The values deliberately remain doubles for this first tranche.  HOT-05 can
 * add an optional FP32 storage path without changing the numerical contract
 * of this batch representation.
 */
typedef struct
{
    pe_infoset_id_t infoset;
    uint16_t actions;
    uint16_t combos;
    uint32_t offset;
} pe_update_group_t;

typedef struct
{
    pe_update_group_t *groups;
    size_t group_count;
    size_t group_capacity;
    double *deltas;
    double *average_deltas;
    size_t value_count;
    size_t value_capacity;
    uint64_t iteration;
} pe_update_soa_t;

typedef struct
{
    pe_update_t *items;
    size_t count;
    size_t capacity;
    /* Iteration at which this batch is applied. Zero means no algorithm
       discounting is requested; solver-driven batches always set it. */
    uint64_t iteration;
    uint64_t merge_comparisons;
    /* Optional vector form. Legacy producers continue to use items/count. */
    pe_update_soa_t soa;
} pe_update_batch_t;

/* A batch together with the stable logical thread id that produced it. The
   array passed to pe_update_batch_reduce may arrive in any order; the id is
   what defines the reduction order. */
typedef struct
{
    size_t thread_index;
    const pe_update_batch_t *batch;
} pe_update_batch_source_t;

void pe_update_batch_clear(pe_update_batch_t *batch);
void pe_update_batch_destroy(pe_update_batch_t *batch);

/** Append one update, growing the batch as needed. */
int pe_update_batch_push(pe_update_batch_t *batch, pe_update_t update);

/** Reset the SoA payload while retaining its allocations. */
void pe_update_soa_clear(pe_update_soa_t *soa);

/** Release all allocations owned by an SoA payload. */
void pe_update_soa_destroy(pe_update_soa_t *soa);

/**
 * Start (or find) one infoset group and return writable contiguous value
 * spans. New spans are zeroed; callers may accumulate into them. Reusing an
 * existing group is what keeps one storage resolution safe for repeated
 * visits to the same infoset during a traversal.
 */
int pe_update_batch_soa_begin_group(pe_update_batch_t *batch,
                                    pe_infoset_id_t infoset,
                                    uint16_t actions,
                                    uint16_t combos,
                                    double **out_deltas,
                                    double **out_average_deltas);

/** Number of scalar values represented by the vector payload. */
size_t pe_update_soa_value_count(const pe_update_soa_t *soa);

/**
 * Merge source into destination, reducing duplicate slots by summing both
 * delta channels. Source remains unchanged. Self-merge is refused because a
 * batch must not grow while it is being iterated.
 */
int pe_update_batch_merge(pe_update_batch_t *destination,
                          const pe_update_batch_t *source);

/**
 * Reduce thread batches in deterministic order. Slots are ordered by
 * infoset, action and combo; equal slots are summed by ascending
 * source.thread_index, then by their original position within that batch.
 * Source array order therefore represents arrival order only and has no
 * effect on the result.
 */
int pe_update_batch_reduce(const pe_update_batch_source_t *sources,
                           size_t source_count,
                           pe_update_batch_t *out_reduced);

/* Named entry point for callers whose inputs are known to be SoA-backed. */
int pe_update_soa_reduce(const pe_update_batch_source_t *sources,
                         size_t source_count,
                         pe_update_batch_t *out_reduced);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BATCH_H */
