/*
 * pe_sampling_policy.h - how sampled traversals distribute work (ISS-232)
 *
 * A sampled solve can starve deep streets: every river visit has to survive
 * every earlier chance draw, so a full preflop-to-river tree accumulates
 * iterations while late-street infosets stay near their uniform start.  A
 * sampling policy is a configurable answer to that problem, kept out of the
 * game adapter: the traversal asks the policy how much work a chance draw
 * deserves, and the game semantics never change.
 *
 * Every policy must keep the CFR estimator unbiased.  STREET_BALANCED does it
 * with replicate draws (see the enum comment); whatever a future policy does,
 * its correction has to be explicit and testable the same way.
 */

#ifndef POKER_EVAL_PE_SAMPLING_POLICY_H
#define POKER_EVAL_PE_SAMPLING_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Streets a sampling policy can reason about, indexed like pe_holdem_street_t.
 * The traversal is game-agnostic, so it carries street tags as int8_t with
 * PE_STREET_UNKNOWN meaning "this adapter does not say"; a draw tagged unknown
 * always gets the standard single-sample treatment. */
#define PE_SAMPLING_STREET_COUNT 4

typedef enum {
    /* One draw per chance visit. The reference behaviour; also the baseline
       every other policy is benchmarked against. */
    PE_SAMPLING_STANDARD = 0,

    /* Street quotas by replication. A chance draw into street s is sampled
       street_replicates[s] times per visit; each replicate is an independent,
       unbiased trajectory, the node's value is their mean, and every
       replicate's CFR updates enter the batch.  The mean of unbiased
       replicates is unbiased, so no importance correction is needed — the
       correction is the division.  Deeper streets receive that factor more
       useful updates per iteration; the caller pays for it in wall clock and
       re-balances through the iteration budget. */
    PE_SAMPLING_STREET_BALANCED,

    PE_SAMPLING_COUNT
} pe_sampling_policy_t;

/* Replicates a policy asks for street `street` (0..3, already validated).
 * STANDARD always answers 1; STREET_BALANCED answers its per-street table.
 * Kept next to the enum so the traversal never hard-codes street work. */
uint16_t pe_sampling_replicates_for(pe_sampling_policy_t policy,
                                    const uint16_t *street_replicates,
                                    int street);

const char *pe_sampling_policy_name(pe_sampling_policy_t policy);

/* Parse "standard" / "street-balanced". @return 0 on success, -1 otherwise. */
int pe_sampling_policy_parse(const char *name, pe_sampling_policy_t *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_SAMPLING_POLICY_H */
