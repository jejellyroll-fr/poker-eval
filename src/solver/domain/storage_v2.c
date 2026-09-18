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

#include <math.h>
#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int finite_double(double value)
{
    return value <= DBL_MAX && value >= -DBL_MAX;
}

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
    float *float_values[PE_VALUES_COUNT];
    int16_t *fixed_values[PE_VALUES_COUNT];
    float *fixed_scales[PE_VALUES_COUNT]; /* one scale per infoset */
    double **staging_spans[PE_VALUES_COUNT]; /* one decoded span per infoset */
    uint8_t *staging_dirty[PE_VALUES_COUNT];
    /* One byte per infoset per array: set when a solver pass dropped the
       span, consumed when the next access re-materialises it (ISS-235). */
    uint8_t *staging_evicted[PE_VALUES_COUNT];
    pe_precision_mode_t precision;
    pe_storage_policy_t tier;
    size_t fixed16_rescales;
    /* Solver-driven drop accounting (ISS-235, phase 6). */
    uint64_t evict_calls;
    uint64_t evicted_bytes;
    uint64_t remat_calls;
    uint64_t remat_time_ns;
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

pe_storage_t *pe_storage_create_with_precision(size_t expected_infosets,
                                               pe_precision_mode_t precision)
{
    pe_storage_t *s = (pe_storage_t *)calloc(1, sizeof(*s));
    size_t slots;

    if (!s || precision < PE_PREC_F64 || precision >= PE_PREC_COUNT)
    {
        free(s);
        return NULL;
    }
    s->tier = PE_STORAGE_FULL;
    s->precision = precision;
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

pe_storage_t *pe_storage_create(size_t expected_infosets)
{
    return pe_storage_create_with_precision(expected_infosets, PE_PREC_F64);
}

pe_storage_t *pe_storage_create_with_tier(size_t expected_infosets,
                                          pe_precision_mode_t precision,
                                          pe_storage_policy_t tier)
{
    pe_storage_t *s = pe_storage_create_with_precision(expected_infosets,
                                                       precision);
    if (!s)
        return NULL;
    if (tier < PE_STORAGE_FULL || tier >= PE_STORAGE_POLICY_COUNT)
    {
        pe_storage_destroy(s);
        return NULL;
    }
    s->tier = tier;
    return s;
}

pe_storage_t *pe_storage_create_precision(size_t expected_infosets,
                                          pe_precision_mode_t precision)
{
    return pe_storage_create_with_precision(expected_infosets, precision);
}

pe_precision_mode_t pe_storage_precision(const pe_storage_t *s)
{
    return s ? s->precision : PE_PREC_COUNT;
}

pe_storage_policy_t pe_storage_tier(const pe_storage_t *s)
{
    return s ? s->tier : PE_STORAGE_POLICY_COUNT;
}

size_t pe_storage_fixed16_rescales(const pe_storage_t *s)
{
    return s ? s->fixed16_rescales : 0u;
}

/* The public storage port exposes double spans. F32 and Fixed16 use decoded
 * staging spans for that compatibility boundary; the resident arrays remain
 * compact, and every accessed infoset has its own stable span. */
static void pe_low_precision_commit_one(pe_storage_t *s,
                                        pe_value_array_t which,
                                        pe_infoset_id_t id);

static void pe_low_precision_commit_all(pe_storage_t *s)
{
    if (!s || (s->precision != PE_PREC_F32 && s->precision != PE_PREC_FIXED16))
        return;
    for (int i = 0; i < PE_VALUES_COUNT; ++i)
    {
        if (!s->staging_dirty[i])
            continue;
        for (pe_infoset_id_t id = 0u; (size_t)id < s->count; ++id)
            if (s->staging_dirty[i][id] != 0u)
                pe_low_precision_commit_one(s, (pe_value_array_t)i, id);
    }
}

void pe_storage_destroy(pe_storage_t *s)
{
    int i;
    if (!s)
        return;
    pe_low_precision_commit_all(s);
    for (i = 0; i < PE_VALUES_COUNT; ++i)
    {
        if (s->staging_spans[i])
        {
            for (size_t id = 0; id < s->count; ++id)
                free(s->staging_spans[i][id]);
            free(s->staging_spans[i]);
        }
        free(s->values[i]);
        free(s->float_values[i]);
        free(s->fixed_values[i]);
        free(s->fixed_scales[i]);
        free(s->staging_dirty[i]);
        free(s->staging_evicted[i]);
    }
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
    if (s->precision == PE_PREC_F32 || s->precision == PE_PREC_FIXED16)
    {
        for (int i = 0; i < PE_VALUES_COUNT; ++i)
        {
            if (s->staging_spans[i])
            {
                double **grown = (double **)realloc(s->staging_spans[i],
                                                     cap * sizeof(double *));
                if (!grown)
                    return -1;
                memset(grown + s->meta_capacity, 0,
                       (cap - s->meta_capacity) * sizeof(double *));
                s->staging_spans[i] = grown;
            }
            if (s->staging_dirty[i])
            {
                uint8_t *grown = (uint8_t *)realloc(s->staging_dirty[i], cap);
                if (!grown)
                    return -1;
                memset(grown + s->meta_capacity, 0, cap - s->meta_capacity);
                s->staging_dirty[i] = grown;
            }
            if (s->staging_evicted[i])
            {
                uint8_t *grown = (uint8_t *)realloc(s->staging_evicted[i], cap);
                if (!grown)
                    return -1;
                memset(grown + s->meta_capacity, 0, cap - s->meta_capacity);
                s->staging_evicted[i] = grown;
            }
            if (s->precision == PE_PREC_FIXED16 && s->fixed_scales[i])
            {
                float *grown = (float *)realloc(s->fixed_scales[i],
                                                cap * sizeof(float));
                if (!grown)
                    return -1;
                for (size_t j = s->meta_capacity; j < cap; ++j)
                    grown[j] = 1.0f;
                s->fixed_scales[i] = grown;
            }
        }
    }
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
        if (s->precision == PE_PREC_FIXED16)
        {
            int16_t *grown;
            if (!s->fixed_values[i])
                continue;
            grown = (int16_t *)realloc(s->fixed_values[i],
                                       (size_t)cap * sizeof(int16_t));
            if (!grown)
                return -1;
            memset(grown + s->value_capacity, 0,
                   (size_t)(cap - s->value_capacity) * sizeof(int16_t));
            s->fixed_values[i] = grown;
        }
        else if (s->precision == PE_PREC_F32)
        {
            float *grown;
            if (!s->float_values[i])
                continue;
            grown = (float *)realloc(s->float_values[i],
                                     (size_t)cap * sizeof(float));
            if (!grown)
                return -1;
            memset(grown + s->value_capacity, 0,
                   (size_t)(cap - s->value_capacity) * sizeof(float));
            s->float_values[i] = grown;
        }
        else
        {
            double *grown;
            if (!s->values[i])
                continue;           /* not allocated yet: nothing to carry */
            grown = (double *)realloc(s->values[i], (size_t)cap * sizeof(double));
            if (!grown)
                return -1;
            memset(grown + s->value_capacity, 0,
                   (size_t)(cap - s->value_capacity) * sizeof(double));
            s->values[i] = grown;
        }
    }
    s->value_capacity = cap;
    return 0;
}

/* Allocate a value array that was never used, zeroed over the whole capacity
   so an infoset created before it appeared still reads zeros. */
static double *pe_ensure_array(pe_storage_t *s, pe_value_array_t which)
{
    uint64_t cap;

    if (s->precision != PE_PREC_F64)
        return NULL;
    if (s->values[which])
        return s->values[which];

    cap = s->value_capacity ? s->value_capacity : 64u;
    s->values[which] = (double *)calloc((size_t)cap, sizeof(double));
    if (s->values[which])
        s->value_capacity = cap;
    return s->values[which];
}

static int pe_ensure_float_array(pe_storage_t *s, pe_value_array_t which)
{
    uint64_t cap;

    if (!s || s->precision != PE_PREC_F32 ||
        (int)which < 0 || which >= PE_VALUES_COUNT)
        return -1;
    if (s->float_values[which])
        return 0;
    cap = s->value_capacity ? s->value_capacity : 64u;
    s->float_values[which] = (float *)calloc((size_t)cap, sizeof(float));
    if (!s->float_values[which])
        return -1;
    s->value_capacity = cap;
    return 0;
}

static int pe_ensure_fixed_array(pe_storage_t *s, pe_value_array_t which)
{
    uint64_t cap;
    if (!s || s->precision != PE_PREC_FIXED16 ||
        (int)which < 0 || which >= PE_VALUES_COUNT)
        return -1;
    if (s->fixed_values[which])
        return 0;
    cap = s->value_capacity ? s->value_capacity : 64u;
    s->fixed_values[which] = (int16_t *)calloc((size_t)cap, sizeof(int16_t));
    if (!s->fixed_values[which])
        return -1;
    s->fixed_scales[which] = (float *)malloc(s->meta_capacity * sizeof(float));
    if (!s->fixed_scales[which])
    {
        free(s->fixed_values[which]);
        s->fixed_values[which] = NULL;
        return -1;
    }
    for (size_t i = 0; i < s->meta_capacity; ++i)
        s->fixed_scales[which][i] = 1.0f;
    s->value_capacity = cap;
    return 0;
}

static int pe_ensure_staging(pe_storage_t *s, pe_infoset_id_t id,
                             pe_value_array_t which, size_t n)
{
    if (!s->staging_spans[which])
    {
        s->staging_spans[which] = (double **)calloc(s->meta_capacity,
                                                     sizeof(double *));
        if (!s->staging_spans[which])
            return -1;
    }
    if (!s->staging_dirty[which])
    {
        s->staging_dirty[which] = (uint8_t *)calloc(s->meta_capacity, 1u);
        if (!s->staging_dirty[which])
            return -1;
    }
    if (!s->staging_evicted[which])
    {
        s->staging_evicted[which] = (uint8_t *)calloc(s->meta_capacity, 1u);
        if (!s->staging_evicted[which])
            return -1;
    }
    if (!s->staging_spans[which][id])
    {
        s->staging_spans[which][id] = (double *)calloc(n, sizeof(double));
        if (!s->staging_spans[which][id])
            return -1;
    }
    return 0;
}

static void pe_low_precision_commit_one(pe_storage_t *s,
                                        pe_value_array_t which,
                                        pe_infoset_id_t id)
{
    const pe_infoset_meta_t *meta;
    size_t n;

    if (!s || (s->precision != PE_PREC_F32 && s->precision != PE_PREC_FIXED16) ||
        (int)which < 0 || which >= PE_VALUES_COUNT || (size_t)id >= s->count ||
        !s->staging_spans[which] || !s->staging_spans[which][id] ||
        !s->staging_dirty[which] ||
        s->staging_dirty[which][id] == 0u)
        return;
    meta = pe_storage_meta(s, id);
    if (!meta)
        return;
    n = pe_storage_slab_size(meta);
    if (s->precision == PE_PREC_F32)
    {
        if (!s->float_values[which])
            return;
        for (size_t i = 0; i < n; ++i)
            s->float_values[which][meta->value_offset + i] =
                (float)s->staging_spans[which][id][i];
    }
    else
    {
        double max_abs = 0.0;
        double prior;
        double scale;
        if (!s->fixed_values[which] || !s->fixed_scales[which])
            return;
        for (size_t i = 0; i < n; ++i)
        {
            double v = fabs(s->staging_spans[which][id][i]);
            if (finite_double(v) && v > max_abs)
                max_abs = v;
        }
        prior = s->fixed_scales[which][id];
        if (prior <= 0.0 || max_abs > prior * 32767.0 + 1e-12)
            s->fixed16_rescales++;
        scale = max_abs > 0.0 ? max_abs / 32767.0 : 1.0;
        s->fixed_scales[which][id] = (float)scale;
        for (size_t i = 0; i < n; ++i)
        {
            double v = s->staging_spans[which][id][i];
            long q = lround(v / scale);
            if (q > 32767) q = 32767;
            if (q < -32767) q = -32767;
            s->fixed_values[which][meta->value_offset + i] = (int16_t)q;
        }
    }
    /* Keep the span marked after committing it. The caller may still hold
       the decoded pointer and mutate it after a later accessor caused this
       span to be flushed; retaining the mark makes the next flush capture
       that mutation as well. */
}

static double *pe_low_precision_values(pe_storage_t *s, pe_infoset_id_t id,
                                       pe_value_array_t which)
{
    const pe_infoset_meta_t *meta = pe_storage_meta(s, id);
    size_t n;
    struct timespec decode_start;
    /* Only a span a solver pass dropped counts as a re-materialisation; the
     * first materialisation of a fresh infoset is not. */
    int was_evicted = (meta && s->staging_spans[which] &&
                       s->staging_evicted[which] &&
                       s->staging_evicted[which][id] != 0u);
    if (was_evicted)
        (void)clock_gettime(CLOCK_MONOTONIC, &decode_start);
    if (!meta || (s->precision == PE_PREC_FIXED16 &&
                  pe_ensure_fixed_array(s, which) != 0) ||
        (s->precision == PE_PREC_F32 && pe_ensure_float_array(s, which) != 0))
        return NULL;
    pe_low_precision_commit_all(s);
    n = pe_storage_slab_size(meta);
    /* Allocate an independent decoded span so another infoset cannot alias
       it and later growth cannot invalidate a pointer already returned. */
    if (pe_ensure_staging(s, id, which, n) != 0)
        return NULL;
    for (size_t i = 0; i < n; ++i)
    {
        if (s->precision == PE_PREC_F32)
            s->staging_spans[which][id][i] =
                (double)s->float_values[which][meta->value_offset + i];
        else
        {
            float scale = s->fixed_scales[which][id];
            if (scale <= 0.0f) scale = 1.0f;
            s->staging_spans[which][id][i] =
                (double)s->fixed_values[which][meta->value_offset + i] * scale;
        }
    }
    s->staging_dirty[which][id] = 1u;
    if (was_evicted)
    {
        struct timespec now;
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        s->remat_calls += 1u;
        s->remat_time_ns += (uint64_t)(now.tv_sec - decode_start.tv_sec) *
                               (uint64_t)1000000000u +
                           (uint64_t)(now.tv_nsec - decode_start.tv_nsec);
        s->staging_evicted[which][id] = 0u;
    }
    return s->staging_spans[which][id];
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

    pe_low_precision_commit_all(s);

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
    for (int i = 0; i < PE_VALUES_COUNT; ++i)
        if (s->fixed_scales[i])
            s->fixed_scales[i][id] = 1.0f;
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
    pe_storage_memory_report_t report;

    pe_storage_memory_report(s, &report);
    return report.storage_bytes;
}

/* Issue #235 (phase 1): attribute every byte the storage holds to the
 * subsystem that owns it. The accounting is deliberately the same walk
 * pe_storage_bytes() has always done, extended with the categories the issue
 * names and with a documented allocator-overhead estimate, so storage_bytes
 * stays consistent with bytes() by construction. */
void pe_storage_memory_report(const pe_storage_t *s,
                              pe_storage_memory_report_t *out)
{
    size_t value_bytes[PE_VALUES_COUNT];
    size_t blocks;
    size_t total;
    int i;

    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!s)
        return;

    blocks = 3; /* the storage struct, the map, the metadata array */
    for (i = 0; i < PE_VALUES_COUNT; ++i)
        value_bytes[i] = 0;

    for (i = 0; i < PE_VALUES_COUNT; ++i)
    {
        if (s->precision == PE_PREC_FIXED16)
        {
            if (s->fixed_values[i])
            {
                value_bytes[i] = (size_t)s->value_capacity * sizeof(int16_t);
                blocks++;
            }
            if (s->fixed_scales[i])
                blocks++; /* one scale per infoset, accounted as metadata */
        }
        else if (s->precision == PE_PREC_F32)
        {
            if (s->float_values[i])
            {
                value_bytes[i] = (size_t)s->value_capacity * sizeof(float);
                blocks++;
            }
        }
        else if (s->values[i])
        {
            value_bytes[i] = (size_t)s->value_capacity * sizeof(double);
            blocks++;
        }

        if (s->precision == PE_PREC_FIXED16 || s->precision == PE_PREC_F32)
        {
            if (s->precision == PE_PREC_FIXED16 && s->fixed_scales[i])
                out->metadata_bytes += s->meta_capacity * sizeof(float);
            if (s->staging_spans[i])
            {
                size_t id;
                blocks++; /* the span index array */
                out->staging_bytes += s->meta_capacity * sizeof(double *);
                for (id = 0; id < s->count; ++id)
                    if (s->staging_spans[i][id])
                    {
                        blocks++; /* one decoded span per materialised infoset */
                        out->staging_bytes +=
                            pe_storage_slab_size(&s->meta[id]) * sizeof(double);
                    }
            }
            if (s->staging_dirty[i])
            {
                blocks++;
                out->staging_bytes += s->meta_capacity * sizeof(uint8_t);
            }
            if (s->staging_evicted[i])
            {
                blocks++;
                out->metadata_bytes += s->meta_capacity * sizeof(uint8_t);
            }
        }
    }

    out->total_infosets = s->count;
    out->total_slots = s->slot_count;
    out->hash_index_bytes = s->slot_capacity * sizeof(uint32_t);
    out->metadata_bytes += s->meta_capacity * sizeof(pe_infoset_meta_t);
    out->regret_bytes = value_bytes[PE_VALUES_REGRET];
    out->average_bytes = value_bytes[PE_VALUES_AVERAGE];
    out->other_values_bytes = value_bytes[PE_VALUES_CURRENT] +
                              value_bytes[PE_VALUES_LOCKED];
    out->allocator_overhead_bytes = blocks * PE_STORAGE_ALLOC_OVERHEAD_BYTES;

    /* ISS-235 (phase 6): the retained layer is everything dropped only with
     * the storage itself; the recomputable layer is the derived decoded
     * spans, which a solver pass may drop and re-decode byte-exact. */
    out->retained_strategy_bytes = out->hash_index_bytes +
                                   out->metadata_bytes +
                                   out->regret_bytes + out->average_bytes +
                                   out->other_values_bytes;
    out->recomputable_strategy_bytes = out->staging_bytes;
    out->evict_calls = s->evict_calls;
    out->evicted_bytes = s->evicted_bytes;
    out->remat_calls = s->remat_calls;
    out->remat_time_ms = (double)s->remat_time_ns / 1000000.0;

    total = sizeof(*s) + out->hash_index_bytes + out->metadata_bytes +
            out->staging_bytes + out->allocator_overhead_bytes;
    for (i = 0; i < PE_VALUES_COUNT; ++i)
        total += value_bytes[i];
    out->storage_bytes = total;
    out->bytes_per_infoset = s->count
        ? (double)total / (double)s->count : 0.0;
    out->bytes_per_strategy_slot = s->slot_count
        ? (double)total / (double)s->slot_count : 0.0;
}

/* ------------------------------------------------------------------ *
 * Values
 * ------------------------------------------------------------------ */

double *pe_storage_values(pe_storage_t *s, pe_infoset_id_t id, pe_value_array_t which)
{
    double *base;

    if (!s || (size_t)id >= s->count || (int)which < 0 || which >= PE_VALUES_COUNT)
        return NULL;

    if (s->precision == PE_PREC_F32 || s->precision == PE_PREC_FIXED16)
        return pe_low_precision_values(s, id, which);

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
    if (s->precision == PE_PREC_F32 || s->precision == PE_PREC_FIXED16)
    {
        /* A const lookup must not create compact storage for an array that
         * has never been accessed. Mutable access materializes the resident
         * representation and decoded span. */
        if ((s->precision == PE_PREC_F32 && !s->float_values[which]) ||
            (s->precision == PE_PREC_FIXED16 &&
             (!s->fixed_values[which] || !s->fixed_scales[which])))
            return NULL;
        /* The port's const accessor may materialize a decoded read span; the
           representation remains logically const even though its cache is
           populated. Convert through uintptr_t to keep strict cast-qual
           builds honest about the API boundary. */
        pe_storage_t *mut = (pe_storage_t *)(uintptr_t)s;
        return pe_low_precision_values(mut, id, which);
    }
    if (!s->values[which])
        return NULL;
    return s->values[which] + s->meta[id].value_offset;
}

/* ISS-235 (phase 6): drop the recomputable decoded spans of infosets acting
 * at or beyond `min_street`, flushing them byte-exact into the resident
 * compact arrays first. A previous solver pass guarantees no pointer is held
 * anywhere, so freeing here cannot invalidate a live span. Convergence-
 * critical arrays are never touched. Under F64 there is no derived layer, so
 * the call answers success and does nothing — the documented no-op. */
int pe_storage_drop_recomputable(pe_storage_t *s, int min_street)
{
    uint64_t evicted_now = 0u;
    int i;

    if (!s)
        return -1;
    if (s->precision != PE_PREC_F32 && s->precision != PE_PREC_FIXED16)
        return 0; /* nothing to drop */
    for (i = 0; i < PE_VALUES_COUNT; ++i)
    {
        pe_infoset_id_t id;
        if (!s->staging_spans[i])
            continue;
        for (id = 0u; (size_t)id < s->count; ++id)
        {
            const pe_infoset_meta_t *meta = &s->meta[id];
            size_t n;
            if (meta->street != PE_STREET_UNKNOWN &&
                (int)meta->street < min_street)
                continue;
            if (!s->staging_spans[i][id])
                continue;
            n = pe_storage_slab_size(meta);
            if (s->staging_dirty[i] && s->staging_dirty[i][id] != 0u)
            {
                /* Byte-exact flush into the resident compact arrays; the
                   span is dropped right after, so the retained-mark flush
                   behaviour below matters no further. */
                pe_low_precision_commit_one(s, (pe_value_array_t)i, id);
                s->staging_dirty[i][id] = 0u;
            }
            free(s->staging_spans[i][id]);
            s->staging_spans[i][id] = NULL;
            if (s->staging_evicted[i])
                s->staging_evicted[i][id] = 1u;
            evicted_now += (uint64_t)n * sizeof(double);
        }
    }
    s->evict_calls += 1u;
    s->evicted_bytes += evicted_now;
    return 0;
}

size_t pe_storage_staging_span_count(const pe_storage_t *s,
                                     pe_value_array_t which)
{
    size_t resident;
    pe_infoset_id_t id;

    if (!s || which < 0 || which >= PE_VALUES_COUNT || !s->staging_spans[which])
        return 0u;
    resident = 0u;
    for (id = 0u; (size_t)id < s->count; ++id)
        if (s->staging_spans[which][id])
            resident++;
    return resident;
}

int pe_storage_set_flags(pe_storage_t *s, pe_infoset_id_t id, uint8_t set, uint8_t clear)
{
    if (!s || (size_t)id >= s->count)
        return -1;
    s->meta[id].flags = (uint8_t)((s->meta[id].flags & (uint8_t)~clear) | set);
    return 0;
}
