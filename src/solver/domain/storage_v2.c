/*
 * storage_v2.c - Dense-ID infoset storage (STO-01, STO-02)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * An open-addressed map from infoset key to a dense id, and flat value arrays
 * indexed by that id.
 *
 * The map stores id + 1 rather than the key, with 0 meaning empty. Storing the
 * key would need a sentinel value that no real key may take, and infoset keys
 * are hashes — every 64-bit value is reachable. Reading the key back through
 * meta[id] costs one indirection on a collision and removes the question.
 *
 * Keys arrive already hashed by the game model, but "already hashed" says
 * nothing about the low bits, which are what a power-of-two mask looks at. They
 * go through a finalizer first, so a game whose keys are dense in the low bits
 * does not turn the map into a linked list.
 */

#include <poker_eval/solver/pe_storage.h>

#include <stdlib.h>
#include <string.h>

/* Small enough to be cheap for a toy game, large enough that a real solve is
   not rehashing from four. */
#define PE_STORAGE_MIN_SLOTS 64u

/* Grow at 70%: linear probing degrades sharply past that, and the arrays here
   are large enough that trading a little memory for probe length is right. */
#define PE_STORAGE_MAX_LOAD_NUM 7u
#define PE_STORAGE_MAX_LOAD_DEN 10u

struct pe_storage_t
{
    /* key -> id + 1, 0 meaning empty. Capacity is a power of two. */
    uint32_t *slots;
    size_t slot_capacity;
    size_t slot_mask;

    pe_infoset_meta_t *meta;
    size_t count;
    size_t meta_capacity;

    double *values[PE_VALUES_COUNT];
    uint64_t slot_count;     /* slots in use across every infoset */
    uint64_t value_capacity; /* slots allocated in each live array */

    size_t shape_conflicts;
};

/* ------------------------------------------------------------------ *
 * Hashing
 * ------------------------------------------------------------------ */

/* SplitMix64's finalizer: a bijection that spreads the high bits down. */
static uint64_t pe_mix(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

static size_t pe_round_up_pow2(size_t x)
{
    size_t p = PE_STORAGE_MIN_SLOTS;
    while (p < x)
        p <<= 1;
    return p;
}

/* ------------------------------------------------------------------ *
 * Lifetime
 * ------------------------------------------------------------------ */

pe_storage_t *pe_storage_create(size_t expected_infosets)
{
    pe_storage_t *s = (pe_storage_t *)calloc(1, sizeof(*s));
    size_t slots;

    if (!s)
        return NULL;

    /* Size for the hint at the target load factor, so a caller who knows the
       count never rehashes. */
    slots = pe_round_up_pow2(expected_infosets * PE_STORAGE_MAX_LOAD_DEN
                                 / PE_STORAGE_MAX_LOAD_NUM + 1u);
    s->slots = (uint32_t *)calloc(slots, sizeof(uint32_t));
    if (!s->slots)
    {
        free(s);
        return NULL;
    }
    s->slot_capacity = slots;
    s->slot_mask = slots - 1u;

    s->meta_capacity = (expected_infosets > 0) ? expected_infosets : 16u;
    s->meta = (pe_infoset_meta_t *)calloc(s->meta_capacity, sizeof(pe_infoset_meta_t));
    if (!s->meta)
    {
        free(s->slots);
        free(s);
        return NULL;
    }

    return s;
}

void pe_storage_destroy(pe_storage_t *s)
{
    int i;
    if (!s)
        return;
    for (i = 0; i < PE_VALUES_COUNT; ++i)
        free(s->values[i]);
    free(s->meta);
    free(s->slots);
    free(s);
}

/* ------------------------------------------------------------------ *
 * Lookup
 * ------------------------------------------------------------------ */

/* Index of the slot holding `key`, or of the first empty slot on its probe
   path. The table is never full — it grows at 70% — so this terminates. */
static size_t pe_probe(const pe_storage_t *s, uint64_t key)
{
    size_t i = (size_t)pe_mix(key) & s->slot_mask;

    for (;;)
    {
        uint32_t slot = s->slots[i];
        if (slot == 0)
            return i;
        if (s->meta[slot - 1u].key == key)
            return i;
        i = (i + 1u) & s->slot_mask;
    }
}

pe_infoset_id_t pe_storage_find(const pe_storage_t *s, uint64_t key)
{
    size_t i;
    uint32_t slot;

    if (!s)
        return PE_INFOSET_ID_INVALID;

    i = pe_probe(s, key);
    slot = s->slots[i];
    return (slot == 0) ? PE_INFOSET_ID_INVALID : (pe_infoset_id_t)(slot - 1u);
}

static int pe_rehash(pe_storage_t *s)
{
    size_t new_capacity = s->slot_capacity * 2u;
    uint32_t *slots = (uint32_t *)calloc(new_capacity, sizeof(uint32_t));
    size_t i;

    if (!slots)
        return -1;

    free(s->slots);
    s->slots = slots;
    s->slot_capacity = new_capacity;
    s->slot_mask = new_capacity - 1u;

    /* Reinsert in id order, so the table is a pure function of the id sequence
       and two storages built the same way probe the same way. */
    for (i = 0; i < s->count; ++i)
    {
        size_t j = pe_probe(s, s->meta[i].key);
        s->slots[j] = (uint32_t)(i + 1u);
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * Growth
 * ------------------------------------------------------------------ */

static int pe_grow_meta(pe_storage_t *s)
{
    size_t cap = s->meta_capacity * 2u;
    pe_infoset_meta_t *m = (pe_infoset_meta_t *)realloc(s->meta,
                                                        cap * sizeof(pe_infoset_meta_t));
    if (!m)
        return -1;
    memset(m + s->meta_capacity, 0, (cap - s->meta_capacity) * sizeof(pe_infoset_meta_t));
    s->meta = m;
    s->meta_capacity = cap;
    return 0;
}

/* Grow every live value array to hold `needed` slots, zeroing the new tail.
   Arrays are grown together so one offset addresses all of them. */
static int pe_grow_values(pe_storage_t *s, uint64_t needed)
{
    uint64_t cap = s->value_capacity ? s->value_capacity : 64u;
    int i;

    if (needed <= s->value_capacity)
        return 0;

    while (cap < needed)
        cap *= 2u;

    for (i = 0; i < PE_VALUES_COUNT; ++i)
    {
        double *grown;
        if (!s->values[i])
            continue;               /* not allocated yet: nothing to carry */
        grown = (double *)realloc(s->values[i], (size_t)cap * sizeof(double));
        if (!grown)
            return -1;
        memset(grown + s->value_capacity, 0,
               (size_t)(cap - s->value_capacity) * sizeof(double));
        s->values[i] = grown;
    }
    s->value_capacity = cap;
    return 0;
}

/* Allocate a value array that was never used, zeroed over the whole capacity
   so an infoset created before it appeared still reads zeros. */
static double *pe_ensure_array(pe_storage_t *s, pe_value_array_t which)
{
    uint64_t cap;

    if (s->values[which])
        return s->values[which];

    cap = s->value_capacity ? s->value_capacity : 64u;
    s->values[which] = (double *)calloc((size_t)cap, sizeof(double));
    if (s->values[which])
        s->value_capacity = cap;
    return s->values[which];
}

/* ------------------------------------------------------------------ *
 * Resolution
 * ------------------------------------------------------------------ */

pe_infoset_id_t pe_storage_resolve(pe_storage_t *s,
                                   uint64_t key,
                                   uint16_t action_count,
                                   uint16_t combo_count,
                                   int8_t street)
{
    size_t i;
    uint32_t slot;
    pe_infoset_id_t id;
    uint64_t slab;

    if (!s || action_count == 0 || combo_count == 0)
        return PE_INFOSET_ID_INVALID;

    i = pe_probe(s, key);
    slot = s->slots[i];
    if (slot != 0)
    {
        id = (pe_infoset_id_t)(slot - 1u);
        /* The slab is already sized and placed. Re-shaping it would move every
           later infoset, invalidating spans the caller may still hold, so the
           mismatch is counted and the original shape returned. */
        if (s->meta[id].action_count != action_count ||
            s->meta[id].combo_count != combo_count)
            s->shape_conflicts++;
        return id;
    }

    if (s->count + 1u > (SIZE_MAX / 2u) || s->count >= 0xFFFFFFFEu)
        return PE_INFOSET_ID_INVALID;   /* ids are 32-bit */

    if (s->count == s->meta_capacity && pe_grow_meta(s) != 0)
        return PE_INFOSET_ID_INVALID;

    slab = (uint64_t)action_count * (uint64_t)combo_count;
    if (pe_grow_values(s, s->slot_count + slab) != 0)
        return PE_INFOSET_ID_INVALID;

    id = (pe_infoset_id_t)s->count;
    s->meta[id].key = key;
    s->meta[id].value_offset = s->slot_count;
    s->meta[id].action_count = action_count;
    s->meta[id].combo_count = combo_count;
    s->meta[id].street = street;
    s->meta[id].flags = 0;
    s->meta[id].reserved[0] = 0;
    s->meta[id].reserved[1] = 0;
    s->slot_count += slab;
    s->count++;

    /* Insert before any rehash, then rehash if the table is now too full: the
       probe position computed above is only valid for the current table. */
    s->slots[i] = (uint32_t)(id + 1u);
    if (s->count * PE_STORAGE_MAX_LOAD_DEN >
        s->slot_capacity * PE_STORAGE_MAX_LOAD_NUM)
    {
        if (pe_rehash(s) != 0)
        {
            /* The table is still correct, just denser than intended. Probing
               stays terminating because the count never reaches capacity. */
        }
    }

    return id;
}

size_t pe_storage_shape_conflicts(const pe_storage_t *s)
{
    return s ? s->shape_conflicts : 0u;
}

size_t pe_storage_count(const pe_storage_t *s)
{
    return s ? s->count : 0u;
}

const pe_infoset_meta_t *pe_storage_meta(const pe_storage_t *s, pe_infoset_id_t id)
{
    if (!s || (size_t)id >= s->count)
        return NULL;
    return &s->meta[id];
}

uint64_t pe_storage_slot_count(const pe_storage_t *s)
{
    return s ? s->slot_count : 0u;
}

size_t pe_storage_bytes(const pe_storage_t *s)
{
    size_t bytes;
    int i;

    if (!s)
        return 0u;

    bytes = sizeof(*s);
    bytes += s->slot_capacity * sizeof(uint32_t);
    bytes += s->meta_capacity * sizeof(pe_infoset_meta_t);
    for (i = 0; i < PE_VALUES_COUNT; ++i)
        if (s->values[i])
            bytes += (size_t)s->value_capacity * sizeof(double);
    return bytes;
}

/* ------------------------------------------------------------------ *
 * Values
 * ------------------------------------------------------------------ */

double *pe_storage_values(pe_storage_t *s, pe_infoset_id_t id, pe_value_array_t which)
{
    double *base;

    if (!s || (size_t)id >= s->count || (int)which < 0 || which >= PE_VALUES_COUNT)
        return NULL;

    base = pe_ensure_array(s, which);
    if (!base)
        return NULL;
    return base + s->meta[id].value_offset;
}

const double *pe_storage_values_const(const pe_storage_t *s, pe_infoset_id_t id,
                                      pe_value_array_t which)
{
    if (!s || (size_t)id >= s->count || (int)which < 0 || which >= PE_VALUES_COUNT)
        return NULL;
    if (!s->values[which])
        return NULL;
    return s->values[which] + s->meta[id].value_offset;
}

int pe_storage_set_flags(pe_storage_t *s, pe_infoset_id_t id, uint8_t set, uint8_t clear)
{
    if (!s || (size_t)id >= s->count)
        return -1;
    s->meta[id].flags = (uint8_t)((s->meta[id].flags & (uint8_t)~clear) | set);
    return 0;
}
