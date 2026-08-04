/*
 * pineapple_preflop.h - Pineapple (3-card) Preflop Equity Lookup API
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Provides infrastructure for O(1) lookup of Pineapple preflop equity.
 * Uses a canonical key abstraction (3 Ranks + Suit Pattern).
 */

#ifndef POKER_EVAL_PINEAPPLE_PREFLOP_H
#define POKER_EVAL_PINEAPPLE_PREFLOP_H

#include <stdint.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pineapple Hand Key (Canonical)
 *
 * Encodes 3-card hand structure into a 32-bit integer.
 * Layout: [Unused: 16] [Suits: 4] [Ranks: 12]
 *
 * Ranks (12 bits): r1 (4) | r2 (4) | r3 (4)
 *   - r1..r3 are ranks 0..12 (2..A) sorted descending.
 *   - Fits in 12 bits (3*4).
 *
 * Suits (4 bits): Encodes suit isomorphism.
 *   - First card (r1) is always suit '0'.
 *   - Subsequent cards map to 0, 1, 2 based on first-appearance order.
 *   - Encoded as: s2 (2) | s3 (2).
 *   - Fits in 4 bits. Stored in bits 0..3.
 *
 * Total canonical key = (r_packed << 4) | s_packed.
 */
typedef uint32_t pineapple_hand_key_t;

/* Core functions */
POKEREVAL_EXPORT pineapple_hand_key_t pineapple_cards_to_key(StdDeck_CardMask hand);
POKEREVAL_EXPORT pineapple_hand_key_t pineapple_key_from_ranks_suits(const int ranks[3], const int suits[3]);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PINEAPPLE_PREFLOP_H */
