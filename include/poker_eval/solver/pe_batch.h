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
} pe_update_batch_t;

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

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BATCH_H */
