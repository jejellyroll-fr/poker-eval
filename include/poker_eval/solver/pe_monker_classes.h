/*
 * pe_monker_classes.h - MonkerSolver's hand-class index for four-card games
 *
 * A stored strategy is indexed by hand class, and this is the numbering it
 * uses. Read off c.bJ in monkersolver.jar 2.1.9 and confirmed against the
 * table that class builds at run time — all 270725 entries, not a sample.
 *
 * Three things define it, and getting any of them wrong yields a numbering
 * that is also a bijection onto 16432 classes and is not this one:
 *
 *   - The deck is suit-major. Card i has suit i / 13 and rank i % 13, with
 *     suits ordered s, h, c, d and ranks 2 through ace. The rank-major
 *     alternative produces 16432 classes too, and disagrees.
 *   - Hands are enumerated lexicographically over sorted four-tuples of card
 *     indices, the plain nested c0 < c1 < c2 < c3 loop.
 *   - A class index is minted the first time a canonical form appears in that
 *     enumeration. It is not a sort of anything.
 *
 * There is no arithmetic shortcut: the numbering is defined by the order of
 * first appearance, so the enumeration has to be run. It is run once, into a
 * table the caller owns.
 */

#ifndef POKER_EVAL_PE_MONKER_CLASSES_H
#define POKER_EVAL_PE_MONKER_CLASSES_H

#include <poker_eval/solver/pe_monker.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Four-card hands up to suit isomorphism. */
#define PE_MONKER_CLASS_COUNT 16432u

/** Four-card hands. */
#define PE_MONKER_COMBO_COUNT 270725u

typedef struct pe_monker_classes_t pe_monker_classes_t;

/**
 * Build the table. Roughly half a megabyte, and a few milliseconds of
 * enumeration; there is no global state and no lazy initialisation, so a
 * caller that wants one shared table owns it and its lifetime.
 */
pe_monker_status_t pe_monker_classes_create(pe_monker_classes_t **out);

void pe_monker_classes_destroy(pe_monker_classes_t *classes);

/**
 * The class index of a four-card hand. `cards` holds four distinct card
 * indices in [0, 52) in any order.
 */
pe_monker_status_t pe_monker_class_of(const pe_monker_classes_t *classes,
                                      const int *cards,
                                      uint32_t *out_class);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_MONKER_CLASSES_H */
