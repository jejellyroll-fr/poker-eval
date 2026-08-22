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
 * The port
 * ------------------------------------------------------------------ */

typedef struct pe_storage_ops_t
{
    /** Short name of the adapter, for the execution plan. Never NULL. */
    const char *name;

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
} pe_storage_ops_t;

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
