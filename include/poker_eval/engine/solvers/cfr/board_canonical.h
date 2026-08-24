/*
 * board_canonical.h - Suit-permutation canonicalization (board isomorphism)
 */

#ifndef POKER_EVAL_BOARD_CANONICAL_H
#define POKER_EVAL_BOARD_CANONICAL_H

#include "poker_eval/core/modern_cardmask.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Suit-permutation canonicalization for poker cards.
 *
 * Boards (and hands) that differ only by a permutation of the suits, e.g.
 * "AhKh7c" vs "AdKd7s", are strategically identical. These functions map any
 * set of cards to a canonical representative so that isomorphic sets share a
 * single key / subtree.
 *
 * Canonical form: cards are sorted by descending rank; suits are then
 * relabeled 0,1,2,3 in order of first appearance. The returned permutation
 * maps each canonical suit label back to the original suit (suit_perm[label]
 * = original suit index, -1 when the label is unused), so results can be
 * translated back to the concrete suits at export time.
 *
 * Returns 0 on success, -1 on invalid input (bad mask size / buffers).
 */

/* Canonical string key: rank letters + suit labels in sorted order, e.g. "AaKa7a". */
int pe_board_canonical_key(mask_t cards, int n, char *out_key, size_t out_size);

/* Canonical representative mask + suit mapping table. */
int pe_board_canonicalize(mask_t cards, int n, mask_t *out_canon, int suit_perm[4]);

/* Canonicalize under a variant-declared set of rank automorphisms in addition
 * to the existing suit orbit. Standard poker must pass only the identity:
 * arbitrary rank relabeling changes straight and high-card order. The
 * validator is mandatory and is the variant's proof boundary: it must return
 * non-zero only for permutations that preserve the complete game/evaluator
 * semantics. Each permutation is still checked to be a bijection here. */
typedef int (*pe_rank_automorphism_validator_fn)(const int rank_perm[13],
                                                void *user_data);

int pe_board_canonicalize_rank_orbit(
    mask_t cards,
    int n,
    const int (*rank_permutations)[13],
    size_t permutation_count,
    pe_rank_automorphism_validator_fn validator,
    void *user_data,
    mask_t *out_canon,
    int out_rank_perm[13],
    int out_suit_perm[4]);

/* Number of cards set in a mask (0..52). */
int pe_board_count_cards(mask_t cards);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_BOARD_CANONICAL_H */
