/*
 * batch.c - Thread-local update batches (PAR-01)
 */

#include <poker_eval/solver/pe_batch.h>

#include "finite_double.h"

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

typedef struct
{
    const pe_update_group_t *group;
    const pe_update_soa_t *soa;
    size_t thread_index;
    size_t group_index;
} pe_ordered_group_t;

void pe_update_batch_clear(pe_update_batch_t *batch)
{
    if (batch)
    {
        batch->count = 0u;
        batch->iteration = 0u;
        batch->merge_comparisons = 0u;
        pe_update_soa_clear(&batch->soa);
    }
}

void pe_update_batch_destroy(pe_update_batch_t *batch)
{
    if (!batch)
        return;
    free(batch->items);
    pe_update_soa_destroy(&batch->soa);
    memset(batch, 0, sizeof(*batch));
}

int pe_update_batch_push(pe_update_batch_t *batch, pe_update_t update)
{
    pe_update_t *grown;
    size_t capacity;

    if (!batch || !pe_finite_double(update.delta) || !pe_finite_double(update.average_delta))
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

/*
 * Hash index over the SoA groups.
 *
 * begin_group has to find an infoset that is already in the batch, because a
 * traversal may reach the same infoset by several betting paths and the deltas
 * must accumulate into one span. Scanning every group to answer that question
 * is quadratic in the number of distinct infosets, which is exactly the cost
 * the SoA layout exists to remove. The table below answers it in constant time.
 *
 * Slots hold group_index + 1 so that a zeroed table means "empty" and needs no
 * separate sentinel. The load factor is kept at or below one half.
 */

static size_t soa_hash_slot(pe_infoset_id_t infoset, size_t capacity)
{
    /* Fibonacci hashing: infoset ids are dense and near-sequential, so the
       multiply is what spreads them across the table. */
    uint64_t mixed = (uint64_t)infoset * UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(mixed >> 32) & (capacity - 1u);
}

static void soa_index_insert(pe_update_soa_t *soa, pe_infoset_id_t infoset,
                             size_t group_index)
{
    size_t slot = soa_hash_slot(infoset, soa->group_index_capacity);

    while (soa->group_index_table[slot] != 0u)
        slot = (slot + 1u) & (soa->group_index_capacity - 1u);
    soa->group_index_table[slot] = (uint32_t)(group_index + 1u);
}

/* Grow and repopulate the table so it can hold group_count + 1 entries at a
   load factor of one half. Returns -1 only on allocation failure. */
static int soa_index_reserve(pe_update_soa_t *soa)
{
    size_t needed = (soa->group_count + 1u) * 2u;
    size_t capacity;
    uint32_t *table;
    size_t group_index;

    if (soa->group_index_capacity >= needed &&
        soa->group_index_table != NULL)
        return 0;
    capacity = soa->group_index_capacity != 0u ? soa->group_index_capacity : 32u;
    while (capacity < needed)
    {
        if (capacity > SIZE_MAX / 2u)
            return -1;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*table))
        return -1;
    table = (uint32_t *)calloc(capacity, sizeof(*table));
    if (table == NULL)
        return -1;
    free(soa->group_index_table);
    soa->group_index_table = table;
    soa->group_index_capacity = capacity;
    for (group_index = 0u; group_index < soa->group_count; ++group_index)
        soa_index_insert(soa, soa->groups[group_index].infoset, group_index);
    return 0;
}

/* Find an existing group for this exact shape, or SIZE_MAX. An infoset whose
   shape differs from the stored one is not a match: begin_group's contract
   keys on all three fields. */
static size_t soa_index_find(const pe_update_soa_t *soa,
                             pe_infoset_id_t infoset,
                             uint16_t actions, uint16_t combos)
{
    size_t slot;

    if (soa->group_index_table == NULL || soa->group_index_capacity == 0u)
        return SIZE_MAX;
    slot = soa_hash_slot(infoset, soa->group_index_capacity);
    while (soa->group_index_table[slot] != 0u)
    {
        size_t candidate = (size_t)soa->group_index_table[slot] - 1u;
        if (candidate < soa->group_count)
        {
            const pe_update_group_t *group = &soa->groups[candidate];
            if (group->infoset == infoset && group->actions == actions &&
                group->combos == combos)
                return candidate;
        }
        slot = (slot + 1u) & (soa->group_index_capacity - 1u);
    }
    return SIZE_MAX;
}

void pe_update_soa_clear(pe_update_soa_t *soa)
{
    if (soa == NULL)
        return;
    soa->group_count = 0u;
    soa->value_count = 0u;
    soa->iteration = 0u;
    /* Keep the allocation, drop the entries: the next traversal refills it. */
    if (soa->group_index_table != NULL)
        memset(soa->group_index_table, 0,
               soa->group_index_capacity * sizeof(*soa->group_index_table));
}

void pe_update_soa_destroy(pe_update_soa_t *soa)
{
    if (soa == NULL)
        return;
    free(soa->groups);
    free(soa->deltas);
    free(soa->average_deltas);
    free(soa->group_index_table);
    memset(soa, 0, sizeof(*soa));
}

size_t pe_update_soa_value_count(const pe_update_soa_t *soa)
{
    return soa != NULL ? soa->value_count : 0u;
}

int pe_update_batch_soa_begin_group(pe_update_batch_t *batch,
                                    pe_infoset_id_t infoset,
                                    uint16_t actions,
                                    uint16_t combos,
                                    double **out_deltas,
                                    double **out_average_deltas)
{
    pe_update_soa_t *soa;
    pe_update_group_t *groups;
    double *deltas;
    double *averages;
    size_t values;
    size_t new_value_count;
    size_t group_capacity;
    size_t value_capacity;

    if (batch == NULL || actions == 0u || combos == 0u ||
        out_deltas == NULL || out_average_deltas == NULL)
        return -1;
    if ((size_t)actions > SIZE_MAX / (size_t)combos)
        return -1;
    values = (size_t)actions * (size_t)combos;
    if (batch->soa.value_count > SIZE_MAX - values ||
        batch->soa.group_count == SIZE_MAX)
        return -1;
    new_value_count = batch->soa.value_count + values;
    if (new_value_count > UINT32_MAX)
        return -1;

    soa = &batch->soa;
    if ((soa->group_count != 0u && soa->groups == NULL) ||
        (soa->value_count != 0u &&
         (soa->deltas == NULL || soa->average_deltas == NULL)))
        return -1;
    {
        size_t existing_index = soa_index_find(soa, infoset, actions, combos);
        if (existing_index != SIZE_MAX)
        {
            const pe_update_group_t *existing = &soa->groups[existing_index];
            *out_deltas = soa->deltas + existing->offset;
            *out_average_deltas = soa->average_deltas + existing->offset;
            return 0;
        }
    }
    /* Reserve the index before any array grows, so a failure here leaves the
       batch exactly as it was. */
    if (soa_index_reserve(soa) != 0)
        return -1;
    group_capacity = soa->group_capacity;
    if (group_capacity < soa->group_count + 1u)
    {
        group_capacity = group_capacity != 0u ? group_capacity * 2u : 16u;
        if (group_capacity < soa->group_count + 1u ||
            group_capacity > SIZE_MAX / sizeof(*groups))
            group_capacity = soa->group_count + 1u;
        groups = (pe_update_group_t *)realloc(
            soa->groups, group_capacity * sizeof(*groups));
        if (groups == NULL)
            return -1;
        soa->groups = groups;
        soa->group_capacity = group_capacity;
    }

    value_capacity = soa->value_capacity;
    if (value_capacity < new_value_count)
    {
        value_capacity = value_capacity != 0u ? value_capacity : 64u;
        while (value_capacity < new_value_count)
        {
            if (value_capacity > SIZE_MAX / 2u)
            {
                value_capacity = new_value_count;
                break;
            }
            value_capacity *= 2u;
        }
        if (value_capacity > SIZE_MAX / sizeof(*deltas))
            return -1;
        deltas = (double *)realloc(soa->deltas,
                                    value_capacity * sizeof(*deltas));
        averages = (double *)realloc(soa->average_deltas,
                                     value_capacity * sizeof(*averages));
        if (deltas == NULL || averages == NULL)
        {
            /* realloc may already have moved the first array. Keep the valid
               pointer so destroy remains safe; the caller sees failure. */
            if (deltas != NULL)
                soa->deltas = deltas;
            if (averages != NULL)
                soa->average_deltas = averages;
            return -1;
        }
        soa->deltas = deltas;
        soa->average_deltas = averages;
        soa->value_capacity = value_capacity;
    }

    soa->groups[soa->group_count].infoset = infoset;
    soa->groups[soa->group_count].actions = actions;
    soa->groups[soa->group_count].combos = combos;
    soa->groups[soa->group_count].offset = (uint32_t)soa->value_count;
    soa_index_insert(soa, infoset, soa->group_count);
    soa->group_count++;
    *out_deltas = soa->deltas + soa->value_count;
    *out_average_deltas = soa->average_deltas + soa->value_count;
    memset(*out_deltas, 0, values * sizeof(**out_deltas));
    memset(*out_average_deltas, 0, values * sizeof(**out_average_deltas));
    soa->value_count = new_value_count;
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

static int ordered_group_compare(const void *left_ptr, const void *right_ptr)
{
    const pe_ordered_group_t *left = (const pe_ordered_group_t *)left_ptr;
    const pe_ordered_group_t *right = (const pe_ordered_group_t *)right_ptr;

    if (left->group->infoset != right->group->infoset)
        return left->group->infoset < right->group->infoset ? -1 : 1;
    if (left->thread_index != right->thread_index)
        return left->thread_index < right->thread_index ? -1 : 1;
    if (left->group_index != right->group_index)
        return left->group_index < right->group_index ? -1 : 1;
    return 0;
}

static int reduce_soa(const pe_update_batch_source_t *sources,
                     size_t source_count, pe_update_batch_t *out_reduced)
{
    pe_ordered_group_t *ordered;
    size_t total = 0u;
    size_t source_index;
    size_t at = 0u;
    uint64_t iteration = 0u;

    for (source_index = 0u; source_index < source_count; ++source_index)
    {
        const pe_update_batch_t *batch = sources[source_index].batch;
        if (batch == NULL || batch->count != 0u ||
            (batch->soa.group_count != 0u &&
             (batch->soa.groups == NULL || batch->soa.deltas == NULL ||
              batch->soa.average_deltas == NULL)) ||
            total > SIZE_MAX - batch->soa.group_count)
            return -1;
        if (batch->iteration != 0u)
        {
            if (iteration != 0u && iteration != batch->iteration)
                return -1;
            iteration = batch->iteration;
        }
        total += batch->soa.group_count;
    }
    out_reduced->iteration = iteration;
    if (total == 0u)
        return 0;
    if (total > SIZE_MAX / sizeof(*ordered))
        return -1;
    ordered = (pe_ordered_group_t *)malloc(total * sizeof(*ordered));
    if (ordered == NULL)
        return -1;
    for (source_index = 0u; source_index < source_count; ++source_index)
    {
        const pe_update_batch_t *batch = sources[source_index].batch;
        size_t group_index;
        for (group_index = 0u; group_index < batch->soa.group_count;
             ++group_index)
        {
            const pe_update_group_t *group = &batch->soa.groups[group_index];
            size_t values;
            if (group->actions == 0u || group->combos == 0u ||
                group->offset > batch->soa.value_count ||
                (size_t)group->actions > SIZE_MAX / (size_t)group->combos)
            {
                free(ordered);
                return -1;
            }
            values = (size_t)group->actions * (size_t)group->combos;
            if (values > batch->soa.value_count - group->offset)
            {
                free(ordered);
                return -1;
            }
            ordered[at].group = group;
            ordered[at].soa = &batch->soa;
            ordered[at].thread_index = sources[source_index].thread_index;
            ordered[at].group_index = group_index;
            at++;
        }
    }
    qsort(ordered, total, sizeof(*ordered), ordered_group_compare);
    at = 0u;
    while (at < total)
    {
        const pe_update_group_t *first = ordered[at].group;
        size_t values = (size_t)first->actions * first->combos;
        size_t next = at + 1u;
        double *deltas;
        double *averages;
        size_t value_index;

        if (pe_update_batch_soa_begin_group(
                out_reduced, first->infoset, first->actions, first->combos,
                &deltas, &averages) != 0)
        {
            free(ordered);
            return -1;
        }
        for (value_index = 0u; value_index < values; ++value_index) {
            deltas[value_index] =
                ordered[at].soa->deltas[first->offset + value_index];
            averages[value_index] =
                ordered[at].soa->average_deltas[first->offset + value_index];
        }
        for (value_index = 0u; value_index < values; ++value_index)
        {
            if (!pe_finite_double(deltas[value_index]) ||
                !pe_finite_double(averages[value_index]))
            {
                free(ordered);
                return -1;
            }
        }
        while (next < total)
        {
            const pe_update_group_t *group = ordered[next].group;
            size_t group_values;
            if (out_reduced->merge_comparisons != UINT64_MAX)
                out_reduced->merge_comparisons++;
            if (group->infoset != first->infoset)
                break;
            group_values = (size_t)group->actions * group->combos;
            if (group->actions != first->actions ||
                group->combos != first->combos || group_values != values)
            {
                free(ordered);
                return -1;
            }
            for (value_index = 0u; value_index < values; ++value_index)
            {
                double delta = deltas[value_index] +
                    ordered[next].soa->deltas[group->offset + value_index];
                double average = averages[value_index] +
                    ordered[next].soa->average_deltas[group->offset + value_index];
                if (!pe_finite_double(delta) || !pe_finite_double(average))
                {
                    free(ordered);
                    return -1;
                }
                deltas[value_index] = delta;
                averages[value_index] = average;
            }
            next++;
        }
        at = next;
    }
    out_reduced->soa.iteration = iteration;
    free(ordered);
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
    /* SoA batches are reduced in one deterministic pass. Refusing to merge
       them here prevents an accidental legacy call from silently discarding
       the vector payload. */
    if (destination->soa.group_count != 0u || source->soa.group_count != 0u)
        return -1;
    if (destination->iteration != 0u && source->iteration != 0u &&
        destination->iteration != source->iteration)
        return -1;
    if (destination->iteration == 0u)
        destination->iteration = source->iteration;

    for (i = 0u; i < source->count; ++i)
    {
        size_t j;
        pe_update_t update = source->items[i];
        int found = 0;

        if (!pe_finite_double(update.delta) || !pe_finite_double(update.average_delta))
            return -1;
        for (j = 0u; j < destination->count; ++j)
        {
            if (destination->merge_comparisons != UINT64_MAX)
                destination->merge_comparisons++;
            if (same_slot(&destination->items[j], &update))
            {
                double delta = destination->items[j].delta + update.delta;
                double average = destination->items[j].average_delta +
                                  update.average_delta;
                if (!pe_finite_double(delta) || !pe_finite_double(average))
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
    uint64_t iteration = 0u;

    if (!out_reduced || (source_count != 0u && !sources))
        return -1;
    pe_update_batch_clear(out_reduced);

    {
        size_t source_index;
        int has_soa = 0;
        for (source_index = 0u; source_index < source_count; ++source_index)
            if (sources[source_index].batch != NULL &&
                sources[source_index].batch->soa.group_count != 0u)
                has_soa = 1;
        if (has_soa)
            return reduce_soa(sources, source_count, out_reduced);
    }

    for (source_index = 0u; source_index < source_count; ++source_index)
    {
        const pe_update_batch_t *batch = sources[source_index].batch;
        if (!batch || (batch->count != 0u && !batch->items) ||
            total > SIZE_MAX - batch->count)
            return -1;
        if (batch->iteration != 0u) {
            if (iteration != 0u && iteration != batch->iteration)
                return -1;
            iteration = batch->iteration;
        }
        total += batch->count;
    }
    out_reduced->iteration = iteration;
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
            if (!pe_finite_double(ordered[at].update.delta) ||
                !pe_finite_double(ordered[at].update.average_delta))
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

        while (next < total)
        {
            if (out_reduced->merge_comparisons != UINT64_MAX)
                out_reduced->merge_comparisons++;
            if (!same_slot(&reduced, &ordered[next].update))
                break;
            double delta = reduced.delta + ordered[next].update.delta;
            double average = reduced.average_delta +
                              ordered[next].update.average_delta;
            if (!pe_finite_double(delta) || !pe_finite_double(average))
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

int pe_update_soa_reduce(const pe_update_batch_source_t *sources,
                         size_t source_count,
                         pe_update_batch_t *out_reduced)
{
    size_t source_index;

    if (out_reduced == NULL || (source_count != 0u && sources == NULL))
        return -1;
    for (source_index = 0u; source_index < source_count; ++source_index)
        if (sources[source_index].batch == NULL ||
            sources[source_index].batch->count != 0u)
            return -1;
    return pe_update_batch_reduce(sources, source_count, out_reduced);
}
