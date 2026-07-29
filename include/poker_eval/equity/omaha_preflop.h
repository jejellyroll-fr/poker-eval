/*
 * omaha_preflop.h - Omaha Preflop Equity Lookup API
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Provides infrastructure for O(1) lookup of Omaha preflop equity.
 * Uses a canonical key abstraction (Ranks + Suit Pattern).
 */

#ifndef POKER_EVAL_OMAHA_PREFLOP_H
#define POKER_EVAL_OMAHA_PREFLOP_H

#include <stdint.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Omaha Hand Key (Canonical)
 *
 * Encodes 4-card hand structure into a 32-bit integer.
 * Layout: [Unused: 8] [Suits: 8] [Ranks: 16]
 *
 * Ranks (16 bits): r1 (4) | r2 (4) | r3 (4) | r4 (4)
 *   - r1..r4 are ranks 0..12 (2..A) sorted descending.
 *   - Fits in 16 bits (4*4).
 *
 * Suits (8 bits): Encodes suit isomorphism.
 *   - First card (r1) is always suit '0'.
 *   - Subsequent cards map to 0, 1, 2, 3 based on appearance order.
 *   - Encoded as: s2 (2) | s3 (2) | s4 (2).
 *   - Fits in 6 bits. Stored in bits 16-23.
 *
 * Total canonical space size < 2^24.
 */
typedef uint32_t omaha_hand_key_t;

/* Lookup table for Hand vs Random equity */
typedef struct {
    float* data;      /* Flat array indexed by key */
    size_t size;      /* Size in elements (usually 2^24) */
    int loaded;       /* Flag */
} omaha_preflop_table_t;

/* Core functions */
POKEREVAL_EXPORT omaha_hand_key_t omaha_cards_to_key(StdDeck_CardMask hand);
POKEREVAL_EXPORT omaha_hand_key_t omaha_key_from_ranks_suits(const int ranks[4], const int suits[4]);

/* Table management */
POKEREVAL_EXPORT omaha_preflop_table_t* omaha_preflop_table_create(void);
POKEREVAL_EXPORT void omaha_preflop_table_free(omaha_preflop_table_t* table);
POKEREVAL_EXPORT int omaha_preflop_table_load(omaha_preflop_table_t* table, const char* filename);
POKEREVAL_EXPORT int omaha_preflop_table_save(const omaha_preflop_table_t* table, const char* filename);
POKEREVAL_EXPORT float omaha_preflop_table_get(const omaha_preflop_table_t* table, omaha_hand_key_t key);
POKEREVAL_EXPORT void omaha_preflop_table_set(omaha_preflop_table_t* table, omaha_hand_key_t key, float equity);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_OMAHA_PREFLOP_H */
