/*
 * batch.c - Thread-local update batches (PAR-01)
 */

#include <poker_eval/solver/pe_batch.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    pe_update_t update;
    size_t thread_index;
    size_t update_index;
} pe_ordered_update_t;

void pe_update_batch_clear(pe_update_batch_t *batch)
{
    if (batch)
        batch->count = 0u;
}

void pe_update_batch_destroy(pe_update_batch_t *batch)
{
    if (!batch)
        return;
    free(batch->items);
    memset(batch, 0, sizeof(*batch));
}

int pe_update_batch_push(pe_update_batch_t *batch, pe_update_t update)
{
    pe_update_t *grown;
    size_t capacity;

    if (!batch || !isfinite(update.delta) || !isfinite(update.average_delta))
        return -1;
    if (batch->count == batch->capacity)
    {
        if (batch->capacity > SIZE_MAX / 2u)
            return -1;
        capacity = batch->capacity ? batch->capacity * 2u : 64u;
        grown = (pe_update_t *)realloc(batch->items,
                                       capacity * sizeof(pe_update_t));
        if (!grown)
            return -1;
        batch->items = grown;
        batch->capacity = capacity;
    }
    batch->items[batch->count++] = update;
    return 0;
}

static int same_slot(const pe_update_t *left, const pe_update_t *right)
{
    return left->infoset == right->infoset &&
           left->action == right->action && left->combo == right->combo;
}

static int ordered_update_compare(const void *left_ptr, const void *right_ptr)
{
    const pe_ordered_update_t *left = (const pe_ordered_update_t *)left_ptr;
    const pe_ordered_update_t *right = (const pe_ordered_update_t *)right_ptr;

    if (left->update.infoset != right->update.infoset)
        return left->update.infoset < right->update.infoset ? -1 : 1;
    if (left->update.action != right->update.action)
        return left->update.action < right->update.action ? -1 : 1;
    if (left->update.combo != right->update.combo)
        return left->update.combo < right->update.combo ? -1 : 1;
    if (left->thread_index != right->thread_index)
        return left->thread_index < right->thread_index ? -1 : 1;
    if (left->update_index != right->update_index)
        return left->update_index < right->update_index ? -1 : 1;
    return 0;
}

int pe_update_batch_merge(pe_update_batch_t *destination,
                          const pe_update_batch_t *source)
{
    size_t i;

    if (!destination || !source || destination == source)
        return -1;
    if ((destination->count != 0u && !destination->items) ||
        (source->count != 0u && !source->items))
        return -1;

    for (i = 0u; i < source->count; ++i)
    {
        size_t j;
        pe_update_t update = source->items[i];
        int found = 0;

        if (!isfinite(update.delta) || !isfinite(update.average_delta))
            return -1;
        for (j = 0u; j < destination->count; ++j)
        {
            if (same_slot(&destination->items[j], &update))
            {
                double delta = destination->items[j].delta + update.delta;
                double average = destination->items[j].average_delta +
                                  update.average_delta;
                if (!isfinite(delta) || !isfinite(average))
                    return -1;
                destination->items[j].delta = delta;
                destination->items[j].average_delta = average;
                found = 1;
                break;
            }
        }
        if (!found && pe_update_batch_push(destination, update) != 0)
            return -1;
    }
    return 0;
}

int pe_update_batch_reduce(const pe_update_batch_source_t *sources,
                           size_t source_count,
                           pe_update_batch_t *out_reduced)
{
    pe_ordered_update_t *ordered;
    size_t total = 0u;
    size_t source_index;
    size_t at = 0u;

    if (!out_reduced || (source_count != 0u && !sources))
        return -1;
    pe_update_batch_clear(out_reduced);

    for (source_index = 0u; source_index < source_count; ++source_index)
    {
        const pe_update_batch_t *batch = sources[source_index].batch;
        if (!batch || (batch->count != 0u && !batch->items) ||
            total > SIZE_MAX - batch->count)
            return -1;
        total += batch->count;
    }
    if (total == 0u)
        return 0;
    if (total > SIZE_MAX / sizeof(*ordered))
        return -1;

    ordered = (pe_ordered_update_t *)malloc(total * sizeof(*ordered));
    if (!ordered)
        return -1;
    for (source_index = 0u; source_index < source_count; ++source_index)
    {
        const pe_update_batch_t *batch = sources[source_index].batch;
        size_t update_index;
        for (update_index = 0u; update_index < batch->count; ++update_index)
        {
            ordered[at].update = batch->items[update_index];
            ordered[at].thread_index = sources[source_index].thread_index;
            ordered[at].update_index = update_index;
            if (!isfinite(ordered[at].update.delta) ||
                !isfinite(ordered[at].update.average_delta))
            {
                free(ordered);
                return -1;
            }
            at++;
        }
    }

    qsort(ordered, total, sizeof(*ordered), ordered_update_compare);
    at = 0u;
    while (at < total)
    {
        pe_update_t reduced = ordered[at].update;
        size_t next = at + 1u;

        while (next < total && same_slot(&reduced, &ordered[next].update))
        {
            double delta = reduced.delta + ordered[next].update.delta;
            double average = reduced.average_delta +
                              ordered[next].update.average_delta;
            if (!isfinite(delta) || !isfinite(average))
            {
                free(ordered);
                return -1;
            }
            reduced.delta = delta;
            reduced.average_delta = average;
            next++;
        }
        if (pe_update_batch_push(out_reduced, reduced) != 0)
        {
            free(ordered);
            return -1;
        }
        at = next;
    }

    free(ordered);
    return 0;
}
