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

typedef struct
{
    pe_update_t *items;
    size_t count;
    size_t capacity;
    /* Iteration at which this batch is applied. Zero means no algorithm
       discounting is requested; solver-driven batches always set it. */
    uint64_t iteration;
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

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BATCH_H */
