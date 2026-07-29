/*
 * canonicalize.h - Canonicalization utilities for board equivalence classes
 */

#ifndef POKER_EVAL_EQUITY_CANONICALIZE_H
#define POKER_EVAL_EQUITY_CANONICALIZE_H

#include <poker_eval/deck/deck_std.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generate a canonical key for a 5-card board (suit relabeling + rank order).
 * Returns 0 on success, -1 if buffer too small or board not 5 cards.
 * Key format: 10 chars (r1 s1 r2 s2 r3 s3 r4 s4 r5 s5), rank in "23456789TJQKA",
 * suit labels 'a'..'d' assigned by first occurrence order.
 */
int pe_board5_canonical_key(StdDeck_CardMask board, char* out_key, size_t out_size);

/* 64-bit hash of canonical key (FNV-1a). Returns 0 on error. */
uint64_t pe_board5_canonical_hash64(StdDeck_CardMask board);

/* Generate canonical key for a 7-card hand (board+hole) under suit relabeling and rank order.
 * Expects exactly 7 cards in mask. Key length: 14 chars + NUL. */
int pe_hand7_canonical_key(StdDeck_CardMask cards7, char* out_key, size_t out_size);

/* 64-bit hash for 7-card canonical key. Returns 0 on error. */
uint64_t pe_hand7_canonical_hash64(StdDeck_CardMask cards7);

/* Generic canonical key for N cards (suit relabeling + rank order).
 * Returns 0 on success, -1 on error.
 * Key format: 2*N chars + NUL. */
int pe_canonical_key(StdDeck_CardMask cards, int n, char* out_key, size_t out_size);

/* 64-bit hash for generic N-card canonical key. */
uint64_t pe_canonical_hash64(StdDeck_CardMask cards, int n);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_EQUITY_CANONICALIZE_H */
