/* compute_gpu_updates.c - common GPU-06 update packing and validation. */

#include "compute_gpu_updates.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int pack_alloc(pe_gpu_update_pack_t *pack, size_t count)
{
    if (count == SIZE_MAX || count > SIZE_MAX / sizeof(*pack->groups))
        return -1;
    pack->groups = (pe_gpu_update_group_t *)calloc(count, sizeof(*pack->groups));
    pack->infosets = (pe_infoset_id_t *)calloc(count, sizeof(*pack->infosets));
    pack->offsets = (uint32_t *)calloc(count + 1u, sizeof(*pack->offsets));
    pack->action_counts = (uint16_t *)calloc(count, sizeof(*pack->action_counts));
    pack->combo_counts = (uint16_t *)calloc(count, sizeof(*pack->combo_counts));
    pack->slots = (uint32_t *)calloc(count, sizeof(*pack->slots));
    pack->regret_deltas = (float *)calloc(count, sizeof(*pack->regret_deltas));
    pack->average_deltas = (float *)calloc(count, sizeof(*pack->average_deltas));
    if (pack->groups == NULL || pack->infosets == NULL || pack->offsets == NULL ||
        pack->action_counts == NULL || pack->combo_counts == NULL ||
        pack->slots == NULL || pack->regret_deltas == NULL ||
        pack->average_deltas == NULL)
        return -1;
    return 0;
}

int pe_gpu_update_pack_build(const pe_compute_config_t *config,
                             const pe_update_batch_t *batch,
                             pe_gpu_update_pack_t *out)
{
    const pe_storage_ops_t *storage;
    size_t total_slots = 0u;
    size_t i;

    if (config == NULL || batch == NULL || out == NULL ||
        (batch->count != 0u && batch->items == NULL))
        return -1;
    memset(out, 0, sizeof(*out));
    if (batch->count == 0u)
        return 0;
    storage = config->storage;
    if (config->storage_self == NULL || storage == NULL || storage->shape == NULL ||
        storage->values == NULL ||
        !pe_storage_serves(storage, PE_VALUES_REGRET) ||
        !pe_storage_serves(storage, PE_VALUES_AVERAGE) ||
        pack_alloc(out, batch->count) != 0)
        goto fail;

    for (i = 0u; i < batch->count; ++i)
    {
        const pe_update_t *update = &batch->items[i];
        size_t group;

        if (!isfinite(update->delta) || !isfinite(update->average_delta))
            goto fail;
        for (group = 0u; group < out->group_count; ++group)
            if (out->groups[group].id == update->infoset)
                break;
        if (group != out->group_count)
            continue;
        if (update->infoset == PE_INFOSET_ID_INVALID ||
            storage->shape(config->storage_self, update->infoset,
                           &out->groups[group].actions,
                           &out->groups[group].combos, NULL) != 0 ||
            out->groups[group].actions == 0u || out->groups[group].combos == 0u)
            goto fail;
        out->groups[group].id = update->infoset;
        out->groups[group].length =
            (size_t)out->groups[group].actions *
            (size_t)out->groups[group].combos;
        if (out->groups[group].length > SIZE_MAX - total_slots ||
            total_slots + out->groups[group].length > UINT32_MAX)
            goto fail;
        total_slots += out->groups[group].length;
        ++out->group_count;
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
    if (out->regrets == NULL || out->averages == NULL)
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
            if (!isfinite(out->regrets[out->offsets[i] + j]) ||
                !isfinite(out->averages[out->offsets[i] + j]))
                goto fail;
        }
    }

    out->layout.count = out->group_count;
    out->layout.infosets = out->infosets;
    out->layout.offsets = out->offsets;
    out->layout.action_counts = out->action_counts;
    out->layout.combo_counts = out->combo_counts;
    for (i = 0u; i < batch->count; ++i)
    {
        uint32_t slot;
        float regret_delta = (float)batch->items[i].delta;
        float average_delta = (float)batch->items[i].average_delta;
        size_t prior;

        if (!isfinite(regret_delta) || !isfinite(average_delta) ||
            pe_infoset_layout_resolve_slot(&out->layout, &batch->items[i],
                                           &slot) != 0)
            goto fail;
        for (prior = 0u; prior < out->count; ++prior)
            if (out->slots[prior] == slot)
                break;
        if (prior != out->count)
        {
            out->regret_deltas[prior] += regret_delta;
            out->average_deltas[prior] += average_delta;
            if (!isfinite(out->regret_deltas[prior]) ||
                !isfinite(out->average_deltas[prior]))
                goto fail;
        }
        else
        {
            out->slots[out->count] = slot;
            out->regret_deltas[out->count] = regret_delta;
            out->average_deltas[out->count] = average_delta;
            ++out->count;
        }
    }
    return 0;

fail:
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
            if (!isfinite((double)pack->regrets[pack->offsets[i] + j]) ||
                !isfinite((double)pack->averages[pack->offsets[i] + j]))
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
