/*
 * pe_storage.h - Dense-ID infoset storage (architecture v3, STO-01/STO-02)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The v2 storage is a hash of entries, each owning its own malloc'd regret and
 * average arrays. Convenient for a dynamic CPU solver and wrong for everything
 * that comes next: a pointer per infoset cannot be uploaded to a device, cannot
 * be walked by SIMD, and cannot be checkpointed without chasing every pointer.
 *
 * This replaces it with two levels. A hash maps an infoset key to a dense id,
 * and the values live in flat arrays indexed by that id. Ids are handed out in
 * order and never reused, so they are also a stable iteration order — which is
 * what makes a deterministic parallel merge and a portable checkpoint possible.
 *
 * One layout for both lanes
 * -------------------------
 * An infoset owns a contiguous slab of action_count * combo_count slots,
 * indexed [action][combo]. The vector lane (A) carries one value per combo; the
 * scalar and abstracted lanes (B) set combo_count to 1 and the slab degenerates
 * to one value per action. Nothing in the layout, the accessors or the growth
 * path distinguishes them, which is the point: the two lanes share a storage
 * rather than each getting one.
 *
 * Two deviations from architecture v3 §6, both deliberate:
 *
 *   - the sketch carries an action_offset and a combo_offset. With a
 *     contiguous slab a single base is enough, and two bases invite them to
 *     disagree. There is one, value_offset.
 *   - it types the offset as uint32_t. A turn solve with 1326 combos per
 *     infoset passes four billion slots well before it runs out of anything
 *     else, so the offset is 64-bit. Ids stay 32-bit: they count infosets, not
 *     slots.
 */

#ifndef POKER_EVAL_PE_STORAGE_H
#define POKER_EVAL_PE_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Identity
 * ------------------------------------------------------------------ */

typedef uint32_t pe_infoset_id_t;

/** Returned when an infoset is absent, or when resolving one fails. */
#define PE_INFOSET_ID_INVALID ((pe_infoset_id_t)0xFFFFFFFFu)

/** Street is optional; this says "not known", matching the v2 convention. */
#define PE_STREET_UNKNOWN ((int8_t)-1)

/* Per-infoset flags. */
#define PE_INFOSET_LOCKED     ((uint8_t)(1u << 0))
#define PE_INFOSET_ABSTRACTED ((uint8_t)(1u << 1))
#define PE_INFOSET_PRUNED     ((uint8_t)(1u << 2))

typedef struct
{
    /** The hashed infoset key the game model produced. */
    uint64_t key;

    /** Base of this infoset's slab in every value array. */
    uint64_t value_offset;

    uint16_t action_count;

    /** 1 outside the vector lane. Never 0: an infoset with no value is not
        stored at all. */
    uint16_t combo_count;

    int8_t street;
    uint8_t flags;
    uint8_t reserved[2];
} pe_infoset_meta_t;

/* ------------------------------------------------------------------ *
 * The storage
 * ------------------------------------------------------------------ */

typedef struct pe_storage_t pe_storage_t;

/** Value arrays an infoset can carry. */
typedef enum {
    PE_VALUES_REGRET = 0,
    PE_VALUES_AVERAGE,
    /** Allocated on first use: the scalar lane recomputes it per node. */
    PE_VALUES_CURRENT,
    /** Allocated on first use, and only when something is locked. */
    PE_VALUES_LOCKED,
    PE_VALUES_COUNT
} pe_value_array_t;

/**
 * Create an empty storage.
 *
 * @param expected_infosets  Hint used to size the map and the metadata up
 *                           front. 0 picks a small default; a wrong hint costs
 *                           a few rehashes, never correctness.
 * @return The storage, or NULL on allocation failure.
 */
pe_storage_t *pe_storage_create(size_t expected_infosets);

void pe_storage_destroy(pe_storage_t *storage);

/**
 * Id of an existing infoset.
 *
 * @return Its id, or PE_INFOSET_ID_INVALID when the key is unknown.
 */
pe_infoset_id_t pe_storage_find(const pe_storage_t *storage, uint64_t key);

/**
 * Id of an infoset, creating it if needed.
 *
 * Ids are dense and monotonic: the first infoset gets 0, the next 1, and so on
 * with no gaps, so the set of live ids is always exactly [0, count).
 *
 * Resolving an existing key ignores `action_count`, `combo_count` and `street`
 * — the slab is already sized and moving it would invalidate every span handed
 * out so far. A caller that resolves the same key with a different shape gets
 * the original shape back, which pe_storage_shape_conflicts() reports.
 *
 * @return The id, or PE_INFOSET_ID_INVALID when either count is 0 or an
 *         allocation fails.
 */
pe_infoset_id_t pe_storage_resolve(pe_storage_t *storage,
                                   uint64_t key,
                                   uint16_t action_count,
                                   uint16_t combo_count,
                                   int8_t street);

/**
 * Whether a key was ever resolved with a shape other than the one stored.
 *
 * A silently ignored shape change means a caller is writing into a slab it
 * mis-measured, so the storage counts those rather than failing the call: the
 * traversal cannot usefully react mid-descent, but a solve that ends with a
 * non-zero count here has a bug worth failing on.
 */
size_t pe_storage_shape_conflicts(const pe_storage_t *storage);

/** Number of infosets, and therefore the exclusive upper bound of every id. */
size_t pe_storage_count(const pe_storage_t *storage);

/** Metadata of an infoset, or NULL when `id` is out of range. */
const pe_infoset_meta_t *pe_storage_meta(const pe_storage_t *storage,
                                         pe_infoset_id_t id);

/** Total slots across every infoset — the length of each value array. */
uint64_t pe_storage_slot_count(const pe_storage_t *storage);

/** Bytes currently held, metadata and map included. */
size_t pe_storage_bytes(const pe_storage_t *storage);

/* ------------------------------------------------------------------ *
 * Values
 * ------------------------------------------------------------------ */

/**
 * The slab of an infoset in one value array.
 *
 * The span is action_count * combo_count long and laid out [action][combo], so
 * slot (a, c) is at index a * combo_count + c. Use pe_storage_slot_index() and
 * let it be the one place that knows.
 *
 * PE_VALUES_CURRENT and PE_VALUES_LOCKED are allocated on first request, so
 * the arrays a solve never touches cost nothing.
 *
 * The pointer is invalidated by any later pe_storage_resolve() that grows the
 * arrays. Read or write it before resolving anything else.
 *
 * @return The span, or NULL when `id` is out of range, `which` is invalid, or
 *         a lazy allocation fails.
 */
double *pe_storage_values(pe_storage_t *storage,
                          pe_infoset_id_t id,
                          pe_value_array_t which);

/** Const form; never allocates, so it returns NULL for an absent array. */
const double *pe_storage_values_const(const pe_storage_t *storage,
                                      pe_infoset_id_t id,
                                      pe_value_array_t which);

/**
 * Index of slot (action, combo) inside a span.
 *
 * Deliberately not bounds-checked: it is called once per slot in the hot path.
 * pe_storage_values() already validated the infoset, and the counts come from
 * its metadata.
 */
static inline size_t pe_storage_slot_index(const pe_infoset_meta_t *meta,
                                           uint16_t action,
                                           uint16_t combo)
{
    return (size_t)action * (size_t)meta->combo_count + (size_t)combo;
}

/** Slots one infoset occupies in each value array. */
static inline size_t pe_storage_slab_size(const pe_infoset_meta_t *meta)
{
    return (size_t)meta->action_count * (size_t)meta->combo_count;
}

/* ------------------------------------------------------------------ *
 * Flags
 * ------------------------------------------------------------------ */

/** Set or clear flags on an infoset. Returns 0, or -1 when `id` is invalid. */
int pe_storage_set_flags(pe_storage_t *storage, pe_infoset_id_t id,
                         uint8_t set, uint8_t clear);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_STORAGE_H */
