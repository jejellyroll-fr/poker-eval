/*
 * pe_vector.h - Per-combo vectors (architecture v3, RNG-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * The representation the vector lane is built on. Where the scalar traversal
 * carries one number per state, lane A carries one number per combo and walks
 * the tree once for the whole range instead of once per hand. A dense array
 * indexed by combo is what makes that possible, and what makes the terminal
 * evaluation an O(n log n) sort rather than an O(n^2) pairing.
 *
 * Two roles, one representation
 * -----------------------------
 * A reach vector holds probabilities — non-negative, multiplied along a path.
 * A value vector holds counterfactual values — any sign, summed. They are the
 * same dense array of doubles, and the names below say which is which.
 *
 * They are aliases rather than distinct structs. In C, separating them would
 * mean either duplicating every operation or casting between them at each call
 * site, and the safety it buys is already carried by the operation names: a
 * reach is advanced with pe_vec_mul, a value is accumulated with pe_vec_axpy.
 * Nothing here can tell them apart, and nothing here needs to.
 *
 * Summation
 * ---------
 * pe_vec_sum and pe_vec_dot compensate. Adding 270 725 doubles naively loses
 * roughly the square root of that many ulps, which is comfortably outside the
 * tolerance a solver's convergence checks are written to; the reduction over a
 * full PLO range is exactly where that would first show, and it would show as
 * a solver that stops converging rather than as an obvious defect.
 */

#ifndef POKER_EVAL_PE_VECTOR_H
#define POKER_EVAL_PE_VECTOR_H

#include <poker_eval/solver/pe_solver.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A dense vector indexed by combo.
 *
 * `v` is owned when the vector came from pe_vec_alloc, borrowed when it came
 * from pe_vec_wrap. Only the former may be passed to pe_vec_free.
 */
typedef struct {
    double *v;
    size_t n;
} pe_vec_t;

/** Reach probabilities: non-negative, multiplied along a path. */
typedef pe_vec_t pe_reach_vec_t;

/** Counterfactual values: any sign, summed. */
typedef pe_vec_t pe_value_vec_t;

/* ------------------------------------------------------------------ *
 * Lifetime
 * ------------------------------------------------------------------ */

/**
 * Allocate a zeroed vector of `n` components.
 * @return PE_SOLVER_OK, PE_SOLVER_ERR_NULL_ARGUMENT,
 *         PE_SOLVER_ERR_INVALID_CONFIG when n is 0, or
 *         PE_SOLVER_ERR_OUT_OF_MEMORY.
 */
pe_solver_status_t pe_vec_alloc(pe_vec_t *out, size_t n);

/** Release a vector from pe_vec_alloc. Safe on NULL and on a zeroed vector. */
void pe_vec_free(pe_vec_t *v);

/** A borrowed view over memory the caller owns. Never freed by this module. */
pe_vec_t pe_vec_wrap(double *data, size_t n);

/* ------------------------------------------------------------------ *
 * Element-wise
 * ------------------------------------------------------------------ */

/** Set every component to `x`. */
void pe_vec_fill(pe_vec_t *v, double x);

/** dst <- src. Requires equal lengths; does nothing otherwise. */
void pe_vec_copy(pe_vec_t *dst, const pe_vec_t *src);

/** v <- v * s. */
void pe_vec_scale(pe_vec_t *v, double s);

/**
 * dst <- dst * src, component by component.
 *
 * This is how a reach vector is advanced through an action: the strategy at an
 * infoset is one probability per combo, not one for the whole range, which is
 * exactly the difference between the vector lane and the scalar one.
 */
void pe_vec_mul(pe_vec_t *dst, const pe_vec_t *src);

/** dst <- dst + a * src. The accumulation a value vector is built from. */
void pe_vec_axpy(pe_vec_t *dst, double a, const pe_vec_t *src);

/* ------------------------------------------------------------------ *
 * Reductions
 * ------------------------------------------------------------------ */

/**
 * Sum of the components, compensated.
 *
 * Neumaier's variant: it also recovers the lost part when the running total is
 * smaller than the term being added, which the plain Kahan form drops. Reach
 * vectors routinely mix a few large weights with a long tail of small ones,
 * and that is the case Kahan alone gets wrong.
 */
double pe_vec_sum(const pe_vec_t *v);

/** Compensated inner product. 0.0 when the lengths differ. */
double pe_vec_dot(const pe_vec_t *a, const pe_vec_t *b);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_VECTOR_H */
