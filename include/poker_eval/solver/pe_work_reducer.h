/*
 * pe_work_reducer.h - deterministic result ledger for distributed work
 */

#ifndef POKER_EVAL_PE_WORK_REDUCER_H
#define POKER_EVAL_PE_WORK_REDUCER_H

#include <poker_eval/solver/pe_work_protocol.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_WORK_REDUCER_MAX_RESULTS 256u

typedef struct pe_work_result_record_t
{
    uint32_t worker_id;
    pe_work_result_t result;
    uint8_t *delta_storage;
} pe_work_result_record_t;

typedef struct pe_work_reducer_t
{
    pe_work_result_record_t records[PE_WORK_REDUCER_MAX_RESULTS];
    size_t count;
} pe_work_reducer_t;

void pe_work_reducer_init(pe_work_reducer_t *reducer);
void pe_work_reducer_destroy(pe_work_reducer_t *reducer);

/**
 * Accept and own a result delta. Results for the same public state may not
 * overlap in their half-open iteration intervals.
 */
int pe_work_reducer_accept(pe_work_reducer_t *reducer,
                           uint32_t worker_id,
                           const pe_work_result_t *result);

/** Sort accepted results by public state, iteration range and worker id. */
void pe_work_reducer_sort(pe_work_reducer_t *reducer);

size_t pe_work_reducer_count(const pe_work_reducer_t *reducer);
const pe_work_result_record_t *pe_work_reducer_get(
    const pe_work_reducer_t *reducer, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_REDUCER_H */
