/*
 * work_reducer.c - deterministic result ledger for distributed work
 */

#include <poker_eval/solver/pe_work_reducer.h>

#include <stdlib.h>
#include <string.h>

static int record_before(const pe_work_result_record_t *left,
                         const pe_work_result_record_t *right)
{
    if (left->result.public_state != right->result.public_state)
        return left->result.public_state < right->result.public_state;
    if (left->result.iteration_begin != right->result.iteration_begin)
        return left->result.iteration_begin < right->result.iteration_begin;
    if (left->result.iteration_end != right->result.iteration_end)
        return left->result.iteration_end < right->result.iteration_end;
    return left->worker_id < right->worker_id;
}

static int records_overlap(const pe_work_result_t *left,
                           const pe_work_result_t *right)
{
    return left->public_state == right->public_state &&
           left->iteration_begin < right->iteration_end &&
           right->iteration_begin < left->iteration_end;
}

void pe_work_reducer_init(pe_work_reducer_t *reducer)
{
    if (reducer)
        *reducer = (pe_work_reducer_t){0};
}

void pe_work_reducer_destroy(pe_work_reducer_t *reducer)
{
    size_t i;
    if (!reducer)
        return;
    for (i = 0u; i < reducer->count; ++i)
        free(reducer->records[i].delta_storage);
    pe_work_reducer_init(reducer);
}

int pe_work_reducer_accept(pe_work_reducer_t *reducer,
                           uint32_t worker_id,
                           const pe_work_result_t *result)
{
    size_t i;
    uint8_t *delta_storage = NULL;

    if (!reducer || worker_id == 0u || pe_work_result_validate(result) != 0 ||
        reducer->count >= PE_WORK_REDUCER_MAX_RESULTS)
        return -1;
    for (i = 0u; i < reducer->count; ++i)
        if (records_overlap(&reducer->records[i].result, result))
            return -1;
    if (result->delta_size != 0u) {
        size_t delta_index;
        delta_storage = (uint8_t *)malloc(result->delta_size);
        if (!delta_storage)
            return -1;
        for (delta_index = 0u; delta_index < result->delta_size; ++delta_index)
            delta_storage[delta_index] = result->delta[delta_index];
    }
    reducer->records[reducer->count].worker_id = worker_id;
    reducer->records[reducer->count].result = *result;
    reducer->records[reducer->count].delta_storage = delta_storage;
    reducer->records[reducer->count].result.delta = delta_storage;
    ++reducer->count;
    return 0;
}

void pe_work_reducer_sort(pe_work_reducer_t *reducer)
{
    size_t i;
    if (!reducer)
        return;
    for (i = 1u; i < reducer->count; ++i) {
        pe_work_result_record_t current = reducer->records[i];
        size_t j = i;
        while (j != 0u && record_before(&current, &reducer->records[j - 1u])) {
            reducer->records[j] = reducer->records[j - 1u];
            --j;
        }
        reducer->records[j] = current;
    }
}

size_t pe_work_reducer_count(const pe_work_reducer_t *reducer)
{
    return reducer ? reducer->count : 0u;
}

const pe_work_result_record_t *pe_work_reducer_get(
    const pe_work_reducer_t *reducer, size_t index)
{
    if (!reducer || index >= reducer->count)
        return NULL;
    return &reducer->records[index];
}
