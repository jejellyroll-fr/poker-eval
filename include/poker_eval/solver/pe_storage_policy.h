/*
 * pe_storage_policy.h - explicit storage tiers for large sampled solves
 * (ISS-235, phase 6)
 *
 * Memory becomes the practical solve limit before evaluator speed on full
 * preflop-to-river trees: retaining every deep-street strategy can dominate
 * the retained memory. A storage policy is the configurable answer, kept out
 * of the game adapter exactly like the sampling policy (ISS-232): the solver
 * asks the policy how much derived state each street keeps, and the CFR
 * semantics never change.
 *
 * What the policy actually trades: under a compact representation
 * (F32/F32-MIXED/FIXED16) each access to an infoset decodes a double staging
 * span resident beside the compact arrays. Those spans are derived data —
 * re-decoding is exact — so dropping them trades wall clock for retained
 * bytes in a controlled way. Under F64 there is no derived layer: the policy
 * is then a documented no-op.
 *
 * Every mode retains all convergence-critical data (regrets, strategy sums,
 * metadata) at all times. A mode never touches strategy results: dropping
 * derived state and re-decoding it is byte-exact, which the same-seed
 * regression tests pin.
 */

#ifndef POKER_EVAL_PE_STORAGE_POLICY_H
#define POKER_EVAL_PE_STORAGE_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Streets, indexed like pe_holdem_street_t: 0 = preflop, 1 = flop,
 * 2 = turn, 3 = river. Street tags reach the storage as int8_t with
 * PE_STREET_UNKNOWN (-1) meaning "this adapter does not say". */
#define PE_POLICY_STREET_COUNT 4

typedef enum {
    /* Keep every resident layer, derived included, once materialised. The
       baseline every other mode is benchmarked against. */
    PE_STORAGE_FULL = 0,

    /* Derived state kept for the hot opening streets, dropped for the deep
       ones: flop stored, turn/river re-materialised on demand. Matches the
       issue's candidate behaviour for flop/turn/river materialisation. */
    PE_STORAGE_COMPACT,

    /* Derived state dropped everywhere: every street re-materialises its
       staging spans on demand, including the opening streets. The largest
       reduction, paid in the most decode work. */
    PE_STORAGE_RECOMPUTE_DEEP,

    PE_STORAGE_POLICY_COUNT
} pe_storage_policy_t;

/*
 * First deep street whose derived state a solver-driven solver pass drops at
 * the end of each iteration (infosets with street >= the answer, plus streets
 * tagged unknown, pass).
 *
 *   FULL           answers 4 — nothing is ever deep enough.
 *   COMPACT        answers 2 — preflop and flop stay hot.
 *   RECOMPUTE_DEEP answers 0 — everywhere is deep enough.
 *
 * Kept next to the enum so the solver never hard-codes street pressure.
 */
int pe_storage_policy_pressure_street(pe_storage_policy_t policy);

const char *pe_storage_policy_name(pe_storage_policy_t policy);

/* Parse a policy name case-insensitively; PE_STORAGE_POLICY_COUNT means
 * unknown. */
pe_storage_policy_t pe_storage_policy_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_STORAGE_POLICY_H */
