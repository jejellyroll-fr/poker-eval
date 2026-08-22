/*
 * storage_legacy.c - The v2 hash storage behind the port (STO-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The bridge of the migration: new code reads and writes through the port
 * while the game model still fills a cfr_storage_t. It lives in poker_engine
 * rather than poker_solver because it wraps engine symbols, and poker_engine
 * already links poker_solver — the other direction would close a cycle.
 *
 * The v2 storage has no dense ids — it is a hash keyed by the infoset key, and
 * nothing in it counts or orders entries. The adapter therefore keeps the one
 * thing the port requires and the backend cannot supply: an append-only array
 * mapping id to key. Ids come from the order keys were first resolved, which
 * is exactly the invariant the port states, and lookups go through the hash as
 * before.
 *
 * Two capabilities it does not have, declared rather than faked:
 *
 *   - it holds one value per action, so combo_count above 1 is refused. A
 *     silent truncation here would let the vector lane write into a slab a
 *     quarter of the size it thinks it has.
 *   - it serves regret and average only. An entry grows its locked array only
 *     when something is actually locked, so handing out a writable one would
 *     turn an ordinary infoset into a locked one; and there is no current
 *     strategy at all, the scalar traversal recomputes it per node.
 */

#include <poker_eval/engine/solvers/cfr/cfr_storage_legacy_port.h>

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <stdlib.h>
#include <string.h>

typedef struct
{
    cfr_storage_t *storage;

    /* id -> key, appended in resolution order. The port needs dense ids and
       the hash cannot produce them. */
    uint64_t *keys;
    uint16_t *actions;
    int8_t *streets;
    uint8_t *flags;
    size_t count;
    size_t capacity;
} legacy_t;

static int legacy_grow(legacy_t *l)
{
    size_t cap = l->capacity ? l->capacity * 2u : 64u;
    uint64_t *k = (uint64_t *)realloc(l->keys, cap * sizeof(uint64_t));
    uint16_t *a;
    int8_t *st;
    uint8_t *f;

    if (!k)
        return -1;
    l->keys = k;

    a = (uint16_t *)realloc(l->actions, cap * sizeof(uint16_t));
    if (!a)
        return -1;
    l->actions = a;

    st = (int8_t *)realloc(l->streets, cap * sizeof(int8_t));
    if (!st)
        return -1;
    l->streets = st;

    f = (uint8_t *)realloc(l->flags, cap * sizeof(uint8_t));
    if (!f)
        return -1;
    l->flags = f;

    memset(l->flags + l->capacity, 0, (cap - l->capacity) * sizeof(uint8_t));
    l->capacity = cap;
    return 0;
}

static int legacy_create(void **self, size_t expected_infosets)
{
    legacy_t *l;

    (void)expected_infosets;   /* the v2 storage sizes itself */
    if (!self)
        return -1;

    l = (legacy_t *)calloc(1, sizeof(legacy_t));
    if (!l)
        return -1;

    l->storage = cfr_storage_create();
    if (!l->storage)
    {
        free(l);
        return -1;
    }

    *self = l;
    return 0;
}

static void legacy_destroy(void *self)
{
    legacy_t *l = (legacy_t *)self;
    if (!l)
        return;
    cfr_storage_destroy(l->storage);
    free(l->keys);
    free(l->actions);
    free(l->streets);
    free(l->flags);
    free(l);
}

static pe_infoset_id_t legacy_find(const void *self, uint64_t key)
{
    const legacy_t *l = (const legacy_t *)self;
    size_t i;

    if (!l)
        return PE_INFOSET_ID_INVALID;

    /* Linear over the id table rather than the hash: the hash answers "does
       this key exist", not "which id is it", and the id is what the port
       promises. The migration bridge is not a hot path. */
    for (i = 0; i < l->count; ++i)
        if (l->keys[i] == key)
            return (pe_infoset_id_t)i;
    return PE_INFOSET_ID_INVALID;
}

static pe_infoset_id_t legacy_resolve(void *self, uint64_t key,
                                      uint16_t action_count, uint16_t combo_count,
                                      int8_t street)
{
    legacy_t *l = (legacy_t *)self;
    pe_infoset_id_t id;

    if (!l || action_count == 0 || combo_count == 0)
        return PE_INFOSET_ID_INVALID;

    /* Declared as scalar-only; refusing is the point of declaring it. */
    if (combo_count > 1)
        return PE_INFOSET_ID_INVALID;

    id = legacy_find(l, key);
    if (id != PE_INFOSET_ID_INVALID)
        return id;

    if (l->count == l->capacity && legacy_grow(l) != 0)
        return PE_INFOSET_ID_INVALID;

    /* Create the entry in the underlying storage so its span exists. */
    if (!cfr_storage_regret_span(l->storage, key, (int)action_count))
        return PE_INFOSET_ID_INVALID;

    id = (pe_infoset_id_t)l->count;
    l->keys[id] = key;
    l->actions[id] = action_count;
    l->streets[id] = street;
    l->flags[id] = 0;
    l->count++;
    return id;
}

static int legacy_shape(const void *self, pe_infoset_id_t id,
                        uint16_t *out_actions, uint16_t *out_combos,
                        int8_t *out_street)
{
    const legacy_t *l = (const legacy_t *)self;

    if (!l || (size_t)id >= l->count)
        return -1;
    if (out_actions)
        *out_actions = l->actions[id];
    if (out_combos)
        *out_combos = 1;
    if (out_street)
        *out_street = l->streets[id];
    return 0;
}

static double *legacy_values(void *self, pe_infoset_id_t id,
                             pe_value_array_t which, size_t *out_len)
{
    legacy_t *l = (legacy_t *)self;
    double *span = NULL;

    if (!l || (size_t)id >= l->count)
        return NULL;

    if (which == PE_VALUES_REGRET)
        span = cfr_storage_regret_span(l->storage, l->keys[id], (int)l->actions[id]);
    else if (which == PE_VALUES_AVERAGE)
        span = cfr_storage_avg_span(l->storage, l->keys[id], (int)l->actions[id]);
    /* CURRENT and LOCKED are not served; see the file header. */

    if (span && out_len)
        *out_len = l->actions[id];
    return span;
}

static const double *legacy_values_const(const void *self, pe_infoset_id_t id,
                                         pe_value_array_t which, size_t *out_len)
{
    /* No cast back to a mutable legacy_t: through a const legacy_t *, the
       member `storage` is a const *pointer* to a mutable storage, which is
       exactly what the v2 accessors want. Casting the const away would compile
       nowhere useful anyway — -Wcast-qual is an error here.
     *
     * The v2 storage has no read-only path, so this goes through the same
     * accessor as the writable one. It cannot create anything: resolve() has
     * already created every id this can be called with. */
    const legacy_t *l = (const legacy_t *)self;
    double *span = NULL;

    if (!l || (size_t)id >= l->count)
        return NULL;

    if (which == PE_VALUES_REGRET)
        span = cfr_storage_regret_span(l->storage, l->keys[id], (int)l->actions[id]);
    else if (which == PE_VALUES_AVERAGE)
        span = cfr_storage_avg_span(l->storage, l->keys[id], (int)l->actions[id]);

    if (span && out_len)
        *out_len = l->actions[id];
    return span;
}

static size_t legacy_count(const void *self)
{
    const legacy_t *l = (const legacy_t *)self;
    return l ? l->count : 0u;
}

static uint64_t legacy_slot_count(const void *self)
{
    const legacy_t *l = (const legacy_t *)self;
    uint64_t n = 0;
    size_t i;

    if (!l)
        return 0u;
    for (i = 0; i < l->count; ++i)
        n += l->actions[i];
    return n;
}

static size_t legacy_bytes(const void *self)
{
    const legacy_t *l = (const legacy_t *)self;
    size_t bytes;

    if (!l)
        return 0u;

    /* The v2 storage does not report its own footprint, so this covers what
       the adapter owns plus the two double arrays per entry it caused to be
       allocated. It is an underestimate of the hash table itself, and saying
       so is better than a confident wrong number. */
    bytes = sizeof(legacy_t);
    bytes += l->capacity * (sizeof(uint64_t) + sizeof(uint16_t) + sizeof(int8_t)
                            + sizeof(uint8_t));
    bytes += (size_t)legacy_slot_count(l) * 2u * sizeof(double);
    return bytes;
}

static int legacy_set_flags(void *self, pe_infoset_id_t id, uint8_t set, uint8_t clear)
{
    legacy_t *l = (legacy_t *)self;

    if (!l || (size_t)id >= l->count)
        return -1;
    l->flags[id] = (uint8_t)((l->flags[id] & (uint8_t)~clear) | set);
    return 0;
}

static int legacy_get_flags(const void *self, pe_infoset_id_t id, uint8_t *out)
{
    const legacy_t *l = (const legacy_t *)self;

    if (!l || (size_t)id >= l->count)
        return -1;
    if (out)
        *out = l->flags[id];
    return 0;
}

static const pe_storage_ops_t k_legacy_ops = {
    "legacy",
    1,                                                    /* scalar only */
    (uint8_t)((1u << PE_VALUES_REGRET) | (1u << PE_VALUES_AVERAGE)),
    legacy_create,
    legacy_destroy,
    legacy_resolve,
    legacy_find,
    legacy_shape,
    legacy_values,
    legacy_values_const,
    legacy_count,
    legacy_slot_count,
    legacy_bytes,
    legacy_set_flags,
    legacy_get_flags
};

const pe_storage_ops_t *pe_storage_legacy_ops(void)
{
    return &k_legacy_ops;
}
