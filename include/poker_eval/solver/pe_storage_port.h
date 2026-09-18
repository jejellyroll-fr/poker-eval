/*
 * pe_storage_port.h - Where regrets and averages live (v3, STO-03)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The driven port for storage. The domain resolves infoset keys to ids and
 * reads and writes value spans; whether those spans sit in the heap, in a
 * memory-mapped file, quantised to 16 bits or resident on a device is the
 * adapter's business.
 *
 * This header carries the vocabulary — the id type, the value arrays, the
 * flags — so that code written against the port needs nothing else. Including
 * pe_storage.h to use the port would mean the port is not one.
 *
 * No iterator
 * -----------
 * There is none, deliberately. Ids are dense and monotonic, so iterating is a
 * loop from 0 to count(): an adapter that needed a callback-based iterator
 * would be one whose ids are not dense, and losing that property costs the
 * deterministic merge and the portable checkpoint that depend on it. The
 * absence of an iterate() here is the port asserting the invariant.
 */

#ifndef POKER_EVAL_PE_STORAGE_PORT_H
#define POKER_EVAL_PE_STORAGE_PORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Vocabulary
 * ------------------------------------------------------------------ */

typedef uint32_t pe_infoset_id_t;

/** Returned when an infoset is absent, or when resolving one fails. */
#define PE_INFOSET_ID_INVALID ((pe_infoset_id_t)0xFFFFFFFFu)

/** Street is optional; this says "not known". */
#define PE_STREET_UNKNOWN ((int8_t)-1)

#define PE_INFOSET_LOCKED     ((uint8_t)(1u << 0))
#define PE_INFOSET_ABSTRACTED ((uint8_t)(1u << 1))
#define PE_INFOSET_PRUNED     ((uint8_t)(1u << 2))

/** Value arrays an infoset can carry. */
typedef enum {
    PE_VALUES_REGRET = 0,
    PE_VALUES_AVERAGE,
    /** Optional: the scalar lane recomputes it per node. */
    PE_VALUES_CURRENT,
    /** Optional, and only when something is locked. */
    PE_VALUES_LOCKED,
    PE_VALUES_COUNT
} pe_value_array_t;

/**
 * Index of slot (action, combo) inside a span.
 *
 * An infoset owns action_count * combo_count slots laid out [action][combo].
 * The vector lane carries one value per combo; the others pass combo_count 1
 * and the slab degenerates to one value per action. This is the one place that
 * knows the rule.
 */
static inline size_t pe_storage_slot_at(uint16_t combo_count,
                                        uint16_t action,
                                        uint16_t combo)
{
    return (size_t)action * (size_t)combo_count + (size_t)combo;
}

/* ------------------------------------------------------------------ *
 * Memory accounting (issue #235, phase 1)
 * ------------------------------------------------------------------ */

/**
 * Subsystem memory breakdown of one storage instance.
 *
 * Memory is a first-class performance metric for large sampled solves, so a
 * storage can attribute every byte it holds to the subsystem that owns it.
 * The categories follow issue #235: the infoset hash/index, the per-infoset
 * metadata, the value arrays (regret, average strategy, others), the decoded
 * staging spans a compact representation needs at the double-valued port
 * boundary, and a documented estimate of allocator overhead.
 *
 * storage_bytes is the total and always equals what bytes() reports.
 * bytes_per_infoset and bytes_per_strategy_slot are the derived first-class
 * figures; both are 0 when nothing is stored.
 */
typedef struct pe_storage_memory_report_t
{
    /** Number of infosets the storage holds. */
    size_t total_infosets;

    /** Value slots across every infoset (the bytes_per_strategy_slot
     *  denominator). */
    uint64_t total_slots;

    /** The key -> id map (the open-addressed slot table). */
    size_t hash_index_bytes;

    /** Per-infoset metadata: shape, offsets, flags, and for FIXED16 the
     *  per-infoset scale vectors. */
    size_t metadata_bytes;

    /** Resident regret values, in whatever representation is selected. */
    size_t regret_bytes;

    /** Resident average-strategy values. */
    size_t average_bytes;

    /** Resident CURRENT / LOCKED arrays. */
    size_t other_values_bytes;

    /** Decoded double spans held for a compact representation at the port
     *  boundary, including the per-infoset span index arrays. */
    size_t staging_bytes;

    /** Estimated allocator overhead: one documented per-block constant for
     *  every heap block the storage owns (PE_STORAGE_ALLOC_OVERHEAD_BYTES).
     *  An estimate, not a measurement; rounding inside the allocator is not
     *  accounted for. */
    size_t allocator_overhead_bytes;

    /** The total: the sum of every category above. */
    size_t storage_bytes;

    /** storage_bytes / total_infosets; 0 when the storage is empty. */
    double bytes_per_infoset;

    /** storage_bytes / total_slots; 0 when no slot exists. */
    double bytes_per_strategy_slot;
} pe_storage_memory_report_t;

/**
 * Documented per-heap-block overhead used by allocator_overhead_bytes.
 *
 * Typical 64-bit malloc implementations charge 16 bytes of header plus size
 * rounding per block. The real figure is allocator-dependent; the estimate
 * exists so a report that ignores it cannot flatter the layout.
 */
#define PE_STORAGE_ALLOC_OVERHEAD_BYTES ((size_t)16)

/* ------------------------------------------------------------------ *
 * The port
 * ------------------------------------------------------------------ */

typedef struct pe_storage_ops_t
{
    /** Short name of the adapter, for the execution plan. Never NULL. */
    const char *name;

    /*
     * What this adapter can hold.
     *
     * Not every backend can serve every shape, and pretending otherwise is
     * how a caller ends up writing into a slab that was silently truncated.
     * The legacy hash storage, for one, holds a single value per action and
     * has no place to put a writable locked strategy — its entries only grow a
     * lock array when something is actually locked, so handing one out would
     * change the entry's meaning.
     *
     * max_combo_count  Largest combo_count accepted. 1 means scalar only;
     *                  0 means no limit. resolve() refuses anything wider.
     * value_arrays     Bit set of the pe_value_array_t values served, as
     *                  1u << PE_VALUES_x. An array outside it always reads
     *                  NULL, which pe_storage_serves() is the readable way to
     *                  ask about.
     */
    uint16_t max_combo_count;
    uint8_t value_arrays;

    /**
     * Allocate an instance. `expected_infosets` is a sizing hint; 0 means
     * unknown and must remain valid.
     * @return 0 on success, -1 otherwise. *self is untouched on failure.
     */
    int (*create)(void **self, size_t expected_infosets);

    /** Release an instance. Safe on NULL. */
    void (*destroy)(void *self);

    /**
     * Id of an infoset, creating it if needed. Ids are dense and monotonic:
     * the live set is always exactly [0, count).
     * @return The id, or PE_INFOSET_ID_INVALID on refusal or failure.
     */
    pe_infoset_id_t (*resolve)(void *self, uint64_t key,
                               uint16_t action_count, uint16_t combo_count,
                               int8_t street);

    /** Id of an existing infoset, or PE_INFOSET_ID_INVALID. Creates nothing. */
    pe_infoset_id_t (*find)(const void *self, uint64_t key);

    /**
     * Shape of an infoset. Any out pointer may be NULL.
     * @return 0 on success, -1 when the id is out of range.
     */
    int (*shape)(const void *self, pe_infoset_id_t id,
                 uint16_t *out_actions, uint16_t *out_combos, int8_t *out_street);

    /**
     * Writable span of one value array, and its length in slots.
     *
     * Optional arrays are allocated on first request. The pointer is
     * invalidated by any later resolve() that grows the storage, so read or
     * write it before resolving anything else.
     *
     * @return The span, or NULL. `out_len` may be NULL.
     */
    double *(*values)(void *self, pe_infoset_id_t id, pe_value_array_t which,
                      size_t *out_len);

    /** Read-only form. Never allocates, so an untouched array reads NULL. */
    const double *(*values_const)(const void *self, pe_infoset_id_t id,
                                  pe_value_array_t which, size_t *out_len);

    /** Number of infosets, and the exclusive upper bound of every id. */
    size_t (*count)(const void *self);

    /** Total slots across every infoset — the length of each value array. */
    uint64_t (*slot_count)(const void *self);

    /** Bytes currently held, metadata included. */
    size_t (*bytes)(const void *self);

    /** Set and clear flags. @return 0, or -1 when the id is out of range. */
    int (*set_flags)(void *self, pe_infoset_id_t id, uint8_t set, uint8_t clear);

    /** Read flags. @return 0, or -1 when the id is out of range. */
    int (*get_flags)(const void *self, pe_infoset_id_t id, uint8_t *out);

    /** Read the stable game key of an infoset id without creating anything. */
    int (*key_at)(const void *self, pe_infoset_id_t id, uint64_t *out_key);

    /**
     * Optional subsystem memory breakdown (issue #235).
     *
     * NULL when the adapter cannot attribute its memory; metrics that query
     * through the port then carry zeros beyond what bytes() alone implies.
     * Fills *out and returns 0 on success, -1 otherwise.
     */
    int (*memory_report)(const void *self, pe_storage_memory_report_t *out);
} pe_storage_ops_t;

/** Whether an adapter serves a value array. */
static inline int pe_storage_serves(const pe_storage_ops_t *ops, pe_value_array_t which)
{
    if (ops == NULL || (int)which < 0 || which >= PE_VALUES_COUNT)
        return 0;
    return (ops->value_arrays & (uint8_t)(1u << (unsigned)which)) != 0;
}

/** Whether an adapter accepts an infoset this wide. */
static inline int pe_storage_accepts_width(const pe_storage_ops_t *ops,
                                           uint16_t combo_count)
{
    if (ops == NULL || combo_count == 0)
        return 0;
    return ops->max_combo_count == 0 || combo_count <= ops->max_combo_count;
}

/** Every value array, for an adapter with no restriction. */
#define PE_VALUES_ALL ((uint8_t)((1u << PE_VALUES_REGRET) | (1u << PE_VALUES_AVERAGE) \
                                 | (1u << PE_VALUES_CURRENT) | (1u << PE_VALUES_LOCKED)))

/* ------------------------------------------------------------------ *
 * Adapter: RAM
 * ------------------------------------------------------------------ */

/**
 * The dense-ID storage held in the heap. The default, and the reference the
 * others are checked against.
 *
 * Shared, immutable, always valid.
 */
const pe_storage_ops_t *pe_storage_ram_ops(void);


#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_STORAGE_PORT_H */
