/* compute_layout.c - deterministic infoset-to-ragged-slot mapping (GPU-06). */

#include <poker_eval/solver/pe_compute.h>

#include <stdint.h>

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
        uint32_t begin;
        uint32_t end;
        uint16_t actions;
        uint16_t combos;
        uint64_t relative;
        uint64_t slot;

        if (layout->infosets[index] != update->infoset)
            continue;
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
    return -1;
}
