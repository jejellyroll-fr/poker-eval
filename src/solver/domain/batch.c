/*
 * batch.c - Thread-local update batches (PAR-01)
 */

#include <poker_eval/solver/pe_batch.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

int pe_update_batch_merge(pe_update_batch_t *destination,
                          const pe_update_batch_t *source)
{
    size_t i;

    if (!destination || !source || destination == source)
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
