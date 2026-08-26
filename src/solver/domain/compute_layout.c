/* compute_layout.c - deterministic infoset-to-ragged-slot mapping (GPU-06). */

#include <poker_eval/solver/pe_compute.h>

#include <stdint.h>

int pe_infoset_layout_slot_at(const pe_infoset_layout_t *layout, size_t index,
                              const pe_update_t *update, uint32_t *out_slot)
{
    uint32_t begin;
    uint32_t end;
    uint16_t actions;
    uint16_t combos;
    uint64_t relative;
    uint64_t slot;

    if (!layout || !update || !out_slot || index >= layout->count ||
        !layout->infosets || !layout->offsets || !layout->action_counts ||
        !layout->combo_counts)
        return -1;
    if (layout->infosets[index] != update->infoset)
        return -1;
    begin = layout->offsets[index];
    end = layout->offsets[index + 1u];
    actions = layout->action_counts[index];
    combos = layout->combo_counts[index];
    if (end < begin || actions == 0u || combos == 0u ||
        update->action >= actions || update->combo >= combos)
        return -1;
    relative = (uint64_t)update->action * combos + update->combo;
    slot = (uint64_t)begin + relative;
    if (relative >= (uint64_t)(end - begin) || slot > UINT32_MAX)
        return -1;
    *out_slot = (uint32_t)slot;
    return 0;
}

/*
 * The general form: find the entry, then apply the arithmetic above.
 *
 * The search is linear because the layout is not required to be sorted or
 * dense. A caller packing many updates against one layout should keep its own
 * index and call pe_infoset_layout_slot_at directly -- otherwise this scan
 * turns a batch into quadratic work. Both paths share the arithmetic and the
 * bounds checks, so neither can validate differently from the other.
 */
int pe_infoset_layout_resolve_slot(const pe_infoset_layout_t *layout,
                                   const pe_update_t *update,
                                   uint32_t *out_slot)
{
    size_t index;

    if (!layout || !update || !out_slot || layout->count == 0u ||
        !layout->infosets || !layout->offsets || !layout->action_counts ||
        !layout->combo_counts)
        return -1;
    for (index = 0u; index < layout->count; ++index)
    {
        if (layout->infosets[index] != update->infoset)
            continue;
        return pe_infoset_layout_slot_at(layout, index, update, out_slot);
    }
    return -1;
}
