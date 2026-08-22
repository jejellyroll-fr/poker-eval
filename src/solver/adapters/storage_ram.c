/*
 * storage_ram.c - RAM adapter for the storage port (STO-03)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Binds the dense-ID storage to pe_storage_ops_t. Thin on purpose: the port
 * was shaped around what that storage already does, so most of this file is
 * one call per operation.
 *
 * The two places it is not thin are the ones worth reading. values() has to
 * report the span length, which the concrete storage exposes through the
 * metadata rather than the accessor; and find() and the const accessors take a
 * const void *, so the casts back are gathered here instead of being scattered
 * through the domain.
 */

#include <poker_eval/solver/pe_storage.h>
#include <poker_eval/solver/pe_storage_port.h>

#include <stddef.h>

static int ram_create(void **self, size_t expected_infosets)
{
    pe_storage_t *s;

    if (!self)
        return -1;

    s = pe_storage_create(expected_infosets);
    if (!s)
        return -1;

    *self = s;
    return 0;
}

static void ram_destroy(void *self)
{
    pe_storage_destroy((pe_storage_t *)self);
}

static pe_infoset_id_t ram_resolve(void *self, uint64_t key,
                                   uint16_t action_count, uint16_t combo_count,
                                   int8_t street)
{
    return pe_storage_resolve((pe_storage_t *)self, key, action_count,
                              combo_count, street);
}

static pe_infoset_id_t ram_find(const void *self, uint64_t key)
{
    return pe_storage_find((const pe_storage_t *)self, key);
}

static int ram_shape(const void *self, pe_infoset_id_t id,
                     uint16_t *out_actions, uint16_t *out_combos,
                     int8_t *out_street)
{
    const pe_infoset_meta_t *meta =
        pe_storage_meta((const pe_storage_t *)self, id);

    if (!meta)
        return -1;

    if (out_actions)
        *out_actions = meta->action_count;
    if (out_combos)
        *out_combos = meta->combo_count;
    if (out_street)
        *out_street = meta->street;
    return 0;
}

static double *ram_values(void *self, pe_infoset_id_t id,
                          pe_value_array_t which, size_t *out_len)
{
    pe_storage_t *s = (pe_storage_t *)self;
    const pe_infoset_meta_t *meta = pe_storage_meta(s, id);
    double *span;

    if (!meta)
        return NULL;

    span = pe_storage_values(s, id, which);
    if (!span)
        return NULL;

    if (out_len)
        *out_len = pe_storage_slab_size(meta);
    return span;
}

static const double *ram_values_const(const void *self, pe_infoset_id_t id,
                                      pe_value_array_t which, size_t *out_len)
{
    const pe_storage_t *s = (const pe_storage_t *)self;
    const pe_infoset_meta_t *meta = pe_storage_meta(s, id);
    const double *span;

    if (!meta)
        return NULL;

    span = pe_storage_values_const(s, id, which);
    if (!span)
        return NULL;

    if (out_len)
        *out_len = pe_storage_slab_size(meta);
    return span;
}

static size_t ram_count(const void *self)
{
    return pe_storage_count((const pe_storage_t *)self);
}

static uint64_t ram_slot_count(const void *self)
{
    return pe_storage_slot_count((const pe_storage_t *)self);
}

static size_t ram_bytes(const void *self)
{
    return pe_storage_bytes((const pe_storage_t *)self);
}

static int ram_set_flags(void *self, pe_infoset_id_t id, uint8_t set, uint8_t clear)
{
    return pe_storage_set_flags((pe_storage_t *)self, id, set, clear);
}

static int ram_get_flags(const void *self, pe_infoset_id_t id, uint8_t *out)
{
    const pe_infoset_meta_t *meta =
        pe_storage_meta((const pe_storage_t *)self, id);

    if (!meta)
        return -1;
    if (out)
        *out = meta->flags;
    return 0;
}

static const pe_storage_ops_t k_ram_ops = {
    "ram",
    0,                  /* no width limit: this is the vector lane's storage */
    PE_VALUES_ALL,
    ram_create,
    ram_destroy,
    ram_resolve,
    ram_find,
    ram_shape,
    ram_values,
    ram_values_const,
    ram_count,
    ram_slot_count,
    ram_bytes,
    ram_set_flags,
    ram_get_flags
};

const pe_storage_ops_t *pe_storage_ram_ops(void)
{
    return &k_ram_ops;
}
