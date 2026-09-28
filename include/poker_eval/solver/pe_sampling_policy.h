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

    /* Issue #256: variance-driven replication. A chance draw is replicated R
       times per visit, R chosen from the measured spread of earlier visits
       of the same kind (see pe_adaptive_sampling_budget), so resolved
       estimates stop early and noisy ones get more draws.

       Two things keep the estimator unbiased. R is fixed before the visit
       draws anything, from statistics of earlier, independent draws; given
       R, the mean of R unbiased replicates is unbiased. And each replicate's
       updates enter the batch at weight 1/R, so a visit contributes, in
       expectation, exactly what one standard draw contributes: R lowers the
       variance of the update, never its expectation or its weight. That last
       point is where it differs from STREET_BALANCED, which weights every
       replicate fully on purpose. */
    PE_SAMPLING_ADAPTIVE_VARIANCE,

    PE_SAMPLING_COUNT
} pe_sampling_policy_t;

/* Issue #256: adaptive-variance settings. Every field may be left at zero,
   which selects its default; a zero-initialised struct is valid and only
   read when the policy is PE_SAMPLING_ADAPTIVE_VARIANCE. */
typedef struct {
    /* Draws per visit, at least 2 so every visit measures its own spread.
       0 selects 2. */
    uint32_t min_samples;
    /* Draws per visit, at most, and also the most draws one trajectory may
       multiply to across nested chance visits: a visit below others that
       already drew R1, R2... draws at most max_samples / (R1 * R2 ...), and
       may then fall below min_samples. 0 selects 32; the ceiling is 4096. */
    uint32_t max_samples;
    /* Visits of one kind between two budget updates. 0 selects 64. */
    uint32_t check_interval;
    /* Two-sided confidence of the half-width, in (0, 1). 0 selects 0.95. */
    double confidence_level;
    /* The target half-width is max(absolute_tolerance,
       relative_tolerance * |mean|), in the units of the values sampled.
       Both 0 selects a relative tolerance of 0.05. */
    double absolute_tolerance;
    double relative_tolerance;
} pe_adaptive_sampling_t;

#define PE_ADAPTIVE_DEFAULT_MIN_SAMPLES 2u
#define PE_ADAPTIVE_DEFAULT_MAX_SAMPLES 32u
#define PE_ADAPTIVE_DEFAULT_CHECK_INTERVAL 64u
#define PE_ADAPTIVE_DEFAULT_CONFIDENCE 0.95
#define PE_ADAPTIVE_DEFAULT_RELATIVE_TOLERANCE 0.05
#define PE_ADAPTIVE_MAX_SAMPLES_CEILING 4096u

/* Fill the defaults into a copy of `in` (NULL means all defaults) and return
   the confidence quantile z. @return 0, or -1 when a set value is invalid:
   min above max, max above the ceiling, a confidence outside (0, 1), or a
   negative or non-finite tolerance. */
int pe_adaptive_sampling_resolve(const pe_adaptive_sampling_t *in,
                                 pe_adaptive_sampling_t *out, double *out_z);

/* Draws per visit for the next visits of one kind, from a resolved config:
       R = ceil(z^2 * variance / tolerance^2),
       tolerance = max(absolute_tolerance, relative_tolerance * |mean|),
   clamped to [min_samples, max_samples]. That is the smallest R whose
   confidence half-width z * sqrt(variance / R) meets the tolerance. A zero
   variance needs the minimum; a zero tolerance with some variance needs the
   maximum. `variance` is the per-draw variance, `mean` the typical value. */
uint32_t pe_adaptive_sampling_budget(const pe_adaptive_sampling_t *resolved,
                                     double z, double variance, double mean);

/* Set one adaptive field from text, for command lines and config files:
   "min-samples", "max-samples", "check-interval" (unsigned integers),
   "confidence", "absolute-tolerance", "relative-tolerance" (numbers).
   Ranges are checked later, by pe_adaptive_sampling_resolve. @return 0, or
   -1 for an unknown key or a value that is not a number of that kind. */
int pe_adaptive_sampling_parse_option(pe_adaptive_sampling_t *settings,
                                      const char *key, const char *value);

/* Replicates a policy asks for street `street` (0..3, already validated).
 * STANDARD always answers 1; STREET_BALANCED answers its per-street table.
 * Kept next to the enum so the traversal never hard-codes street work. */
uint16_t pe_sampling_replicates_for(pe_sampling_policy_t policy,
                                    const uint16_t *street_replicates,
                                    int street);

const char *pe_sampling_policy_name(pe_sampling_policy_t policy);

/* Parse "standard" / "street-balanced" / "adaptive-variance".
   @return 0 on success, -1 otherwise. */
int pe_sampling_policy_parse(const char *name, pe_sampling_policy_t *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_SAMPLING_POLICY_H */
