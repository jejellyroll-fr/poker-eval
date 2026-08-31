/* compute_gpu_updates.c - common GPU-06 update packing and validation. */

#include "compute_gpu_updates.h"
#include "../domain/finite_double.h"
#include <poker_eval/solver/pe_regret_dcfr.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int pack_alloc(pe_gpu_update_pack_t *pack, size_t group_count,
                      size_t update_count)
{
    if (group_count == SIZE_MAX || update_count == SIZE_MAX ||
        group_count > SIZE_MAX / sizeof(*pack->groups) ||
        update_count > SIZE_MAX / sizeof(*pack->slots) ||
        group_count > SIZE_MAX - 1u)
        return -1;
    pack->groups = (pe_gpu_update_group_t *)calloc(
        group_count, sizeof(*pack->groups));
    pack->infosets = (pe_infoset_id_t *)calloc(
        group_count, sizeof(*pack->infosets));
    pack->offsets = (uint32_t *)calloc(
        group_count + 1u, sizeof(*pack->offsets));
    pack->action_counts = (uint16_t *)calloc(
        group_count, sizeof(*pack->action_counts));
    pack->combo_counts = (uint16_t *)calloc(
        group_count, sizeof(*pack->combo_counts));
    pack->slots = (uint32_t *)calloc(update_count, sizeof(*pack->slots));
    pack->regret_deltas = (float *)calloc(
        update_count, sizeof(*pack->regret_deltas));
    pack->average_deltas = (float *)calloc(
        update_count, sizeof(*pack->average_deltas));
    if (pack->groups == NULL || pack->infosets == NULL || pack->offsets == NULL ||
        pack->action_counts == NULL || pack->combo_counts == NULL ||
        pack->slots == NULL || pack->regret_deltas == NULL ||
        pack->average_deltas == NULL)
        return -1;
    return 0;
}

/*
 * Open-addressing index over uint64 keys, holding value + 1 so that a zeroed
 * table means empty.
 *
 * Packing a batch asks two lookup questions per update -- which group is this
 * infoset, and has this slot been seen -- and both used to be answered by
 * scanning everything accumulated so far. That is quadratic in the number of
 * distinct infosets a batch touches, which for a wide preflop range is the
 * whole batch. The table answers both in constant time.
 */
typedef struct
{
    uint64_t *keys;   /* key + 1; zero means the slot is free */
    uint32_t *values;
    size_t capacity;
} pe_pack_index_t;

static void pack_index_destroy(pe_pack_index_t *index)
{
    if (index == NULL)
        return;
    free(index->keys);
    free(index->values);
    index->keys = NULL;
    index->values = NULL;
    index->capacity = 0u;
}

/* Sized once for the whole batch: at most `count` entries at a load factor of
   one half, so it never has to grow or rehash. */
static int pack_index_create(pe_pack_index_t *index, size_t count)
{
    size_t capacity = 32u;

    index->keys = NULL;
    index->values = NULL;
    index->capacity = 0u;
    if (count > SIZE_MAX / 4u)
        return -1;
    while (capacity < count * 2u)
    {
        if (capacity > SIZE_MAX / 2u)
            return -1;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(uint64_t) ||
        capacity > SIZE_MAX / sizeof(uint32_t))
        return -1;
    index->keys = (uint64_t *)calloc(capacity, sizeof(*index->keys));
    index->values = (uint32_t *)calloc(capacity, sizeof(*index->values));
    if (index->keys == NULL || index->values == NULL)
    {
        pack_index_destroy(index);
        return -1;
    }
    index->capacity = capacity;
    return 0;
}

/* Fibonacci hashing: infoset ids and slot numbers are both dense and near
   sequential, so the multiply is what spreads them across the table. */
static size_t pack_index_probe(const pe_pack_index_t *index, uint64_t key)
{
    uint64_t stored = key + 1u;
    size_t slot = (size_t)((key * UINT64_C(0x9E3779B97F4A7C15)) >> 32) &
                  (index->capacity - 1u);

    while (index->keys[slot] != 0u && index->keys[slot] != stored)
        slot = (slot + 1u) & (index->capacity - 1u);
    return slot;
}

/* SIZE_MAX when the key is absent. */
static size_t pack_index_find(const pe_pack_index_t *index, uint64_t key)
{
    size_t slot;

    if (index->capacity == 0u)
        return SIZE_MAX;
    slot = pack_index_probe(index, key);
    return index->keys[slot] == key + 1u ? (size_t)index->values[slot]
                                         : SIZE_MAX;
}

static void pack_index_insert(pe_pack_index_t *index, uint64_t key,
                              uint32_t value)
{
    size_t slot = pack_index_probe(index, key);
    index->keys[slot] = key + 1u;
    index->values[slot] = value;
}

/* Turn one logical update into the effective delta that a vanilla GPU add
 * must apply. Doing this on the host keeps the device kernels small while
 * preserving the exact configured CFR+/DCFR/averaging semantics. The
 * working values are advanced for every logical update, so duplicate slots
 * remain correct after their deltas are reduced for the device. */
static int pack_effective_update(const pe_compute_config_t *config,
                                 uint64_t iteration, double *regret,
                                 double *average, double delta,
                                 double average_delta, float *out_delta,
                                 float *out_average_delta)
{
    double next_regret;
    double next_average;

    if (config == NULL || regret == NULL || average == NULL ||
        out_delta == NULL || out_average_delta == NULL ||
        !pe_finite_double(*regret) || !pe_finite_double(*average) ||
        !pe_finite_double(delta) || !pe_finite_double(average_delta))
        return -1;
    next_regret = *regret;
    next_average = *average;
    if (config->regret_mode == PE_REGRET_DCFR)
    {
        pe_dcfr_params_t params = {
            config->dcfr_alpha, config->dcfr_beta, config->dcfr_gamma
        };
        if (pe_dcfr_discount_regrets(&next_regret, 1u, iteration,
                                     &params) != 0)
            return -1;
    }
    next_regret += delta;
    if (config->regret_mode == PE_REGRET_PLUS && next_regret < 0.0)
        next_regret = 0.0;

    switch (config->averaging_mode)
    {
    case PE_AVG_LINEAR:
        if (iteration == 0u)
            return -1;
        average_delta *= (double)iteration;
        break;
    case PE_AVG_POWER: {
        double weight;
        if (pe_dcfr_average_weight(iteration, config->dcfr_gamma,
                                   &weight) != 0)
            return -1;
        average_delta *= weight;
        break;
    }
    case PE_AVG_DELAYED_LINEAR:
        if (iteration <= (uint64_t)(config->averaging_delay < 0
                                         ? 0 : config->averaging_delay))
            average_delta = 0.0;
        else
            average_delta *= (double)(iteration -
                                      (uint64_t)config->averaging_delay);
        break;
    case PE_AVG_UNIFORM:
    case PE_AVG_IMPORTANCE:
    case PE_AVG_COUNT:
    default:
        break;
    }
    next_average += average_delta;
    if (!pe_finite_double(next_regret) || !pe_finite_double(next_average))
        return -1;
    *out_delta = (float)(next_regret - *regret);
    *out_average_delta = (float)(next_average - *average);
    if (!pe_finite_double((double)*out_delta) ||
        !pe_finite_double((double)*out_average_delta))
        return -1;
    *regret = next_regret;
    *average = next_average;
    return 0;
}

static int pack_add_update(const pe_compute_config_t *config,
                           uint64_t iteration, pe_gpu_update_pack_t *out,
                           pe_pack_index_t *group_index,
                           pe_pack_index_t *slot_index,
                           double *working_regrets,
                           double *working_averages,
                           pe_update_t update)
{
    size_t group;
    uint32_t slot;
    size_t prior;
    float regret_delta;
    float average_delta;

    group = pack_index_find(group_index, (uint64_t)update.infoset);
    if (!pe_finite_double(update.delta) ||
        !pe_finite_double(update.average_delta) || group == SIZE_MAX ||
        pe_infoset_layout_slot_at(&out->layout, group, &update, &slot) != 0 ||
        (size_t)slot >= out->total_slots ||
        pack_effective_update(config, iteration, &working_regrets[slot],
                              &working_averages[slot], update.delta,
                              update.average_delta, &regret_delta,
                              &average_delta) != 0)
        return -1;
    prior = pack_index_find(slot_index, (uint64_t)slot);
    if (prior != SIZE_MAX)
    {
        out->regret_deltas[prior] += regret_delta;
        out->average_deltas[prior] += average_delta;
        return pe_finite_double((double)out->regret_deltas[prior]) &&
               pe_finite_double((double)out->average_deltas[prior]) ? 0 : -1;
    }
    if (out->count == SIZE_MAX)
        return -1;
    out->slots[out->count] = slot;
    out->regret_deltas[out->count] = regret_delta;
    out->average_deltas[out->count] = average_delta;
    pack_index_insert(slot_index, (uint64_t)slot, (uint32_t)out->count);
    ++out->count;
    return 0;
}

int pe_gpu_update_pack_build(const pe_compute_config_t *config,
                             const pe_update_batch_t *batch,
                             pe_gpu_update_pack_t *out)
{
    const pe_storage_ops_t *storage;
    pe_pack_index_t group_index = {NULL, NULL, 0u};
    pe_pack_index_t slot_index = {NULL, NULL, 0u};
    double *working_regrets = NULL;
    double *working_averages = NULL;
    size_t total_slots = 0u;
    size_t group_input_count;
    size_t logical_count;
    int has_soa;
    size_t i;

    if (config == NULL || batch == NULL || out == NULL ||
        (batch->count != 0u && batch->items == NULL) ||
        (batch->soa.group_count != 0u &&
         (batch->soa.groups == NULL || batch->soa.deltas == NULL ||
          batch->soa.average_deltas == NULL)) ||
        (batch->count != 0u && batch->soa.group_count != 0u))
        return -1;
    memset(out, 0, sizeof(*out));
    has_soa = batch->soa.group_count != 0u;
    if (batch->count == 0u && !has_soa)
        return 0;
    group_input_count = has_soa ? batch->soa.group_count : batch->count;
    logical_count = has_soa ? batch->soa.value_count : batch->count;
    if (group_input_count == 0u || logical_count == 0u ||
        pack_index_create(&group_index, group_input_count) != 0 ||
        pack_index_create(&slot_index, logical_count) != 0)
        goto fail;
    storage = config->storage;
    if (config->storage_self == NULL || storage == NULL || storage->shape == NULL ||
        storage->values == NULL ||
        !pe_storage_serves(storage, PE_VALUES_REGRET) ||
        !pe_storage_serves(storage, PE_VALUES_AVERAGE) ||
        pack_alloc(out, group_input_count, logical_count) != 0)
        goto fail;

    if (has_soa)
    {
        for (i = 0u; i < batch->soa.group_count; ++i)
        {
            const pe_update_group_t *source = &batch->soa.groups[i];
            size_t values;
            uint16_t actions;
            uint16_t combos;

            if (source->actions == 0u || source->combos == 0u ||
                source->offset > batch->soa.value_count ||
                (size_t)source->actions > SIZE_MAX / (size_t)source->combos)
                goto fail;
            values = (size_t)source->actions * (size_t)source->combos;
            if (values > batch->soa.value_count - source->offset ||
                source->infoset == PE_INFOSET_ID_INVALID ||
                pack_index_find(&group_index, (uint64_t)source->infoset) !=
                    SIZE_MAX ||
                storage->shape(config->storage_self, source->infoset,
                               &actions, &combos, NULL) != 0 ||
                actions != source->actions || combos != source->combos)
                goto fail;
            out->groups[i].id = source->infoset;
            out->groups[i].actions = source->actions;
            out->groups[i].combos = source->combos;
            out->groups[i].length = values;
            if (values > SIZE_MAX - total_slots ||
                total_slots + values > UINT32_MAX)
                goto fail;
            total_slots += values;
            pack_index_insert(&group_index, (uint64_t)source->infoset,
                              (uint32_t)i);
            ++out->group_count;
        }
    }
    else
    {
        for (i = 0u; i < batch->count; ++i)
        {
            const pe_update_t *update = &batch->items[i];
            size_t group;

            if (!pe_finite_double(update->delta) ||
                !pe_finite_double(update->average_delta))
                goto fail;
            if (pack_index_find(&group_index, (uint64_t)update->infoset) !=
                SIZE_MAX)
                continue;
            group = out->group_count;
            if (update->infoset == PE_INFOSET_ID_INVALID ||
                storage->shape(config->storage_self, update->infoset,
                               &out->groups[group].actions,
                               &out->groups[group].combos, NULL) != 0 ||
                out->groups[group].actions == 0u ||
                out->groups[group].combos == 0u)
                goto fail;
            out->groups[group].id = update->infoset;
            out->groups[group].length =
                (size_t)out->groups[group].actions *
                (size_t)out->groups[group].combos;
            if (out->groups[group].length > SIZE_MAX - total_slots ||
                total_slots + out->groups[group].length > UINT32_MAX)
                goto fail;
            total_slots += out->groups[group].length;
            pack_index_insert(&group_index, (uint64_t)update->infoset,
                              (uint32_t)group);
            ++out->group_count;
        }
    }
    if (out->group_count == 0u || total_slots == 0u ||
        total_slots > SIZE_MAX / sizeof(*out->regrets))
        goto fail;
    out->total_slots = total_slots;

    for (i = 0u; i < out->group_count; ++i)
    {
        size_t expected = (size_t)out->groups[i].actions *
                          (size_t)out->groups[i].combos;
        size_t regret_length = 0u;
        size_t average_length = 0u;
        out->infosets[i] = out->groups[i].id;
        out->action_counts[i] = out->groups[i].actions;
        out->combo_counts[i] = out->groups[i].combos;
        out->offsets[i + 1u] = out->offsets[i] + (uint32_t)expected;
        out->groups[i].regrets = storage->values(
            config->storage_self, out->groups[i].id, PE_VALUES_REGRET,
            &regret_length);
        out->groups[i].averages = storage->values(
            config->storage_self, out->groups[i].id, PE_VALUES_AVERAGE,
            &average_length);
        if (out->groups[i].regrets == NULL || out->groups[i].averages == NULL ||
            regret_length < expected || average_length < expected)
            goto fail;
    }

    out->regrets = (float *)calloc(total_slots, sizeof(*out->regrets));
    out->averages = (float *)calloc(total_slots, sizeof(*out->averages));
    if (total_slots > SIZE_MAX / sizeof(*working_regrets) ||
        out->regrets == NULL || out->averages == NULL)
        goto fail;
    working_regrets = (double *)malloc(
        total_slots * sizeof(*working_regrets));
    working_averages = (double *)malloc(
        total_slots * sizeof(*working_averages));
    if (working_regrets == NULL || working_averages == NULL)
        goto fail;
    for (i = 0u; i < out->group_count; ++i)
    {
        size_t j;
        size_t length = (size_t)out->action_counts[i] *
                        (size_t)out->combo_counts[i];
        for (j = 0u; j < length; ++j)
        {
            out->regrets[out->offsets[i] + j] =
                (float)out->groups[i].regrets[j];
            out->averages[out->offsets[i] + j] =
                (float)out->groups[i].averages[j];
            if (!pe_finite_double(out->regrets[out->offsets[i] + j]) ||
                !pe_finite_double(out->averages[out->offsets[i] + j]))
                goto fail;
            working_regrets[out->offsets[i] + j] =
                (double)out->regrets[out->offsets[i] + j];
            working_averages[out->offsets[i] + j] =
                (double)out->averages[out->offsets[i] + j];
        }
    }

    out->layout.count = out->group_count;
    out->layout.infosets = out->infosets;
    out->layout.offsets = out->offsets;
    out->layout.action_counts = out->action_counts;
    out->layout.combo_counts = out->combo_counts;
    if (has_soa)
    {
        for (i = 0u; i < batch->soa.group_count; ++i)
        {
            const pe_update_group_t *group = &batch->soa.groups[i];
            size_t value_count = (size_t)group->actions *
                                 (size_t)group->combos;
            size_t value_index;

            for (value_index = 0u; value_index < value_count; ++value_index)
            {
                pe_update_t update = {
                    group->infoset,
                    (uint16_t)(value_index / group->combos),
                    (uint16_t)(value_index % group->combos),
                    batch->soa.deltas[group->offset + value_index],
                    batch->soa.average_deltas[group->offset + value_index]
                };
                if (pack_add_update(config, batch->iteration, out,
                                    &group_index, &slot_index,
                                    working_regrets, working_averages,
                                    update) != 0)
                    goto fail;
            }
        }
    }
    else
    {
        for (i = 0u; i < batch->count; ++i)
        {
            if (pack_add_update(config, batch->iteration, out,
                                &group_index, &slot_index,
                                working_regrets, working_averages,
                                batch->items[i]) != 0)
                goto fail;
        }
    }
    free(working_averages);
    free(working_regrets);
    working_averages = NULL;
    working_regrets = NULL;
    pack_index_destroy(&group_index);
    pack_index_destroy(&slot_index);
    return 0;

fail:
    free(working_averages);
    free(working_regrets);
    pack_index_destroy(&group_index);
    pack_index_destroy(&slot_index);
    pe_gpu_update_pack_destroy(out);
    return -1;
}

int pe_gpu_update_pack_commit(pe_gpu_update_pack_t *pack)
{
    size_t i;

    if (pack == NULL || pack->groups == NULL || pack->regrets == NULL ||
        pack->averages == NULL)
        return -1;
    for (i = 0u; i < pack->group_count; ++i)
    {
        size_t j;
        size_t length = (size_t)pack->action_counts[i] *
                        (size_t)pack->combo_counts[i];
        for (j = 0u; j < length; ++j)
        {
            if (!pe_finite_double((double)pack->regrets[pack->offsets[i] + j]) ||
                !pe_finite_double((double)pack->averages[pack->offsets[i] + j]))
                return -1;
            pack->groups[i].regrets[j] =
                (double)pack->regrets[pack->offsets[i] + j];
            pack->groups[i].averages[j] =
                (double)pack->averages[pack->offsets[i] + j];
        }
    }
    return 0;
}

void pe_gpu_update_pack_destroy(pe_gpu_update_pack_t *pack)
{
    if (pack == NULL)
        return;
    free(pack->average_deltas);
    free(pack->regret_deltas);
    free(pack->slots);
    free(pack->averages);
    free(pack->regrets);
    free(pack->combo_counts);
    free(pack->action_counts);
    free(pack->offsets);
    free(pack->infosets);
    free(pack->groups);
    memset(pack, 0, sizeof(*pack));
}
