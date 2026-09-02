/* legacy_vector_adapter.h - bridge legacy CFR games into solver v3 */

#ifndef POKER_EVAL_LEGACY_VECTOR_ADAPTER_H
#define POKER_EVAL_LEGACY_VECTOR_ADAPTER_H

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/solver/pe_traversal.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_legacy_vector_adapter_t {
    cfr_game_t *legacy;
    pe_vector_game_t vector;
    uint64_t root_key;
    uint64_t *owned_state_keys;
    size_t owned_count;
    size_t owned_capacity;
} pe_legacy_vector_adapter_t;

/**
 * Wrap a legacy cfr_game_t as a one-combo-or-many-combo v3 vector game.
 *
 * Legacy terminal utilities are copied into every combo lane because the
 * legacy callback has no per-combo argument. States returned by legacy action
 * and chance callbacks are retained and released by destroy(), after the v3
 * solve has stopped traversing them.
 */
int pe_legacy_vector_adapter_init(pe_legacy_vector_adapter_t *adapter,
                                  cfr_game_t *legacy,
                                  uint16_t combo_count);

/** Release tracked child states and clear the adapter. */
void pe_legacy_vector_adapter_destroy(pe_legacy_vector_adapter_t *adapter);

/** Borrow the v3 game view. Valid until destroy(). */
const pe_vector_game_t *pe_legacy_vector_adapter_game(
    const pe_legacy_vector_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_LEGACY_VECTOR_ADAPTER_H */
