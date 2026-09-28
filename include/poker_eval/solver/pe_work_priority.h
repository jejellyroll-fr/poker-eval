/*
 * pe_work_priority.h - uncertainty-aware work prioritisation (issue #258)
 *
 * A solve spends the same amount of work on every decision it has to make,
 * whether the preferred action wins by a mile or by a hair. The confidence-
 * guided best response (issue #257) already measures, per decision, how far
 * the leader is ahead of the runner-up (`gap`) and how uncertain that
 * difference is (`gap_half_width`). This module turns those two numbers into
 * a single priority, and turns a batch of them into an ordering.
 *
 * The score is the uncertainty of the leader-versus-runner-up difference
 * divided by its size:
 *
 *     gap         = best - second_best
 *     uncertainty = hypot(best_stderr, second_stderr)
 *     score       = uncertainty / max(gap, epsilon)
 *
 * so a large gap with a tight estimate scores low (the ordering is settled,
 * more work will not move it) and a small gap with a wide estimate scores
 * high. A decision whose ordering is not even resolved - the runner-up is
 * not behind, or the gap is not a number - scores +infinity and sorts first.
 *
 * The score is unbounded, and a scheduler must not be. It is therefore
 * quantised into `buckets` geometric buckets, and the ordering only looks at
 * the bucket: within a bucket the input order is preserved. That is what
 * keeps the output batchable - a fine-grained priority queue would destroy
 * the vector and accelerator throughput the work units exist to feed.
 *
 * Two rules stop a low-scoring region from being starved:
 *
 *   - the coverage floor (`min_visits`): a decision serviced fewer times than
 *     that ranks **ahead of every decision that has been serviced enough**,
 *     whatever either scored. It is a tier, not a bucket promotion: a
 *     decision that has barely been measured cannot be called settled, and
 *     letting an aged decision outrank it would let the floor be preempted,
 *     which is the starvation it exists to prevent. Within the tier the input
 *     order decides, so prioritisation proper begins only once every decision
 *     has had its minimum;
 *   - aging (`aging_interval`): a decision that has waited `age` epochs since
 *     its last service is promoted by `age / aging_interval` buckets, so its
 *     rank rises the longer it waits. Aging applies below the floor tier, so
 *     it can never jump an unserved decision.
 *
 * Both are off under the FIFO policy, which returns the input order
 * unchanged and is the default. Nothing here is game-specific, and nothing
 * here touches a backend: the module reads metadata the caller already has
 * and returns a permutation.
 *
 * Consuming the adaptive-variance sampler (issue #256) is a convenience, not
 * a requirement: pe_work_priority_item_from_stats() fills an item from two
 * pe_online_stats_t accumulators, and a caller that has no uncertainty
 * information passes a zero stderr, which scores the decision on its gap
 * alone.
 */

#ifndef POKER_EVAL_PE_WORK_PRIORITY_H
#define POKER_EVAL_PE_WORK_PRIORITY_H

#include <poker_eval/solver/pe_online_stats.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_WORK_PRIORITY_MAX_BUCKETS 32u
#define PE_WORK_PRIORITY_DEFAULT_BUCKETS 8u
#define PE_WORK_PRIORITY_DEFAULT_RATIO 2.0
#define PE_WORK_PRIORITY_DEFAULT_EPSILON 1e-9
#define PE_WORK_PRIORITY_DEFAULT_MIN_VISITS 1u

/** How a batch of work is ordered. */
typedef enum {
    /* Input order, unchanged. The historical behaviour, and the default. */
    PE_WORK_SCHED_FIFO = 0,

    /* One item per bucket per round, walking down from the round's starting
       bucket and wrapping, so the service ratio between the top and the
       bottom bucket is bounded by the number of non-empty buckets, at the
       cost of not draining the top bucket first.

       The round starts at the top bucket on epoch 0 and one bucket lower on
       each later epoch, wrapping at the bottom. That rotation is what makes
       the bound hold for a caller that services a prefix of the order and
       recomputes: without it every call would put the same top bucket at
       position zero, and the policy would do nothing the strict one does not.
       A caller that consumes the whole permutation sees the rotation only as
       a change of order within a round. */
    PE_WORK_SCHED_BALANCED,

    /* Highest bucket first, drained before the next bucket starts. */
    PE_WORK_SCHED_UNCERTAINTY_AWARE,

    PE_WORK_SCHED_COUNT
} pe_work_scheduler_policy_t;

/** Priority settings. A zero-initialised struct resolves to the defaults
    below, which under the default FIFO policy change no ordering at all. */
typedef struct pe_work_priority_config_t {
    pe_work_scheduler_policy_t policy; /* 0 = FIFO */
    /* Coverage floor: fewer services than this ranks in the top bucket.
       0 selects 1. Ignored under FIFO. */
    uint32_t min_visits;
    /* Floor on the gap, so the score stays finite. 0 selects 1e-9. */
    double epsilon;
    /* Number of geometric buckets; 0 selects 8, and 1 or more than 32 is
       refused. At least 2 are needed to separate "settled" from "not". */
    uint32_t buckets;
    /* Bucket boundaries are ratio^0, ratio^1, ... A score below ratio^0
       lands in bucket 0. 0 selects 2.0; 1.0 or less is refused. */
    double bucket_ratio;
    /* Epochs of waiting per promoted bucket; 0 disables aging. Ignored
       under FIFO. */
    uint64_t aging_interval;
} pe_work_priority_config_t;

/**
 * One queued decision's observable metadata. Every field is a number the
 * caller already holds; none of it is game-specific.
 */
typedef struct pe_work_priority_item_t {
    /* The leader's and the runner-up's estimated values. */
    double best;
    double second_best;
    /* Their standard errors. Zero means "no uncertainty information": the
       decision is then scored on its gap alone. NaN means "not enough
       observations to estimate one", which scores as unresolved - the two are
       not the same thing, and pe_work_priority_item_from_stats() produces the
       second rather than a zero it cannot justify. */
    double best_stderr;
    double second_stderr;
    /* How often this decision has been serviced, and at which epoch of the
       queue it was serviced last. */
    uint64_t visits;
    uint64_t last_served;
    /* Legal actions. Fewer than two means there is nothing to decide, and
       the item scores zero. */
    uint32_t actions;
} pe_work_priority_item_t;

/** Totals over one ordering, for telemetry. */
typedef struct pe_work_priority_stats_t {
    uint64_t items;
    /* Items the coverage floor pulled into the top bucket. */
    uint64_t coverage_promotions;
    /* Items aging moved up at least one bucket. */
    uint64_t aging_promotions;
    /* Items whose ordering is unresolved (a non-finite score). */
    uint64_t unresolved;
    /* Queue depth per effective bucket; the histogram of the scores the
       ordering was actually built from. */
    uint64_t bucket_depth[PE_WORK_PRIORITY_MAX_BUCKETS];
    /* Sum and count of the finite scores, for their mean. */
    double score_sum;
    uint64_t score_count;
    /* Sum over items of the epochs each has waited, for the mean delay. */
    uint64_t delay_sum;
} pe_work_priority_stats_t;

/** Fill the defaults into a copy. A NULL input resolves to all defaults.
    @return 0, or -1 for a policy outside the enum, a bucket count of 1 or
    above 32, a bucket ratio of 1.0 or less or non-finite, or a negative or
    non-finite epsilon. */
int pe_work_priority_resolve(const pe_work_priority_config_t *in,
                             pe_work_priority_config_t *out);

/**
 * The priority of one decision under a resolved config: larger means more
 * useful additional work. +infinity when the ordering is unresolved (the
 * runner-up is not behind, or a value is not a number), 0 when there is
 * nothing to decide (fewer than two actions) or when the estimates are
 * equally certain and the leader is ahead.
 */
double pe_work_priority_score(const pe_work_priority_config_t *resolved,
                              const pe_work_priority_item_t *item);

/**
 * The bucket the item is ranked in: its score quantised, then raised by the
 * coverage floor and by aging, unless the policy is FIFO (which ranks
 * nothing and returns the score's bucket). Always below `buckets`.
 *
 * A bucket alone does not order two items: an item below the coverage floor
 * outranks every item at or above it whatever their buckets, so the tier
 * from pe_work_priority_below_floor() is the first key and this is the
 * second.
 */
uint32_t pe_work_priority_bucket(const pe_work_priority_config_t *resolved,
                                 const pe_work_priority_item_t *item,
                                 uint64_t epoch);

/**
 * Whether the item is below the coverage floor, and therefore ranked ahead
 * of every item that is not. Always false under FIFO, which ranks nothing.
 */
int pe_work_priority_below_floor(const pe_work_priority_config_t *resolved,
                                 const pe_work_priority_item_t *item);

/**
 * Order `count` items. `out_order` receives the indices of `items` in service
 * order, highest priority first; it needs room for `count` entries.
 * `epoch` is the caller's current service counter, used by aging. `stats`
 * may be NULL.
 *
 * The key, highest priority first, is: the coverage-floor tier (below the
 * floor before at or above it), then the bucket, then the input position.
 * Under BALANCED the bucket pass is round-robin rather than drained, but the
 * floor tier still comes first.
 *
 * The result is a permutation, and it is fully determined by the metadata:
 * equal buckets keep the input order, so identical input gives identical
 * output. @return 0, or -1 for bad arguments or a capacity below `count`.
 */
int pe_work_priority_order(const pe_work_priority_config_t *resolved,
                           const pe_work_priority_item_t *items, size_t count,
                           uint64_t epoch, size_t *out_order, size_t capacity,
                           pe_work_priority_stats_t *stats);

/**
 * Fill an item from the running statistics of two actions, so a caller that
 * already keeps pe_online_stats_t accumulators (the adaptive-variance
 * sampler, issue #256) does not have to know the layout. A NULL
 * `second_best` mirrors `best`, which with `actions` < 2 scores zero.
 *
 * An accumulator with fewer than two observations yields a NaN standard
 * error, not a zero one: pe_online_stats_std_error() cannot estimate a spread
 * from a single sample, and reading that as certainty would drop a decision
 * that has barely been measured into the lowest bucket. The item then scores
 * as unresolved and is ranked first.
 */
void pe_work_priority_item_from_stats(pe_work_priority_item_t *item,
                                      const pe_online_stats_t *best,
                                      const pe_online_stats_t *second_best,
                                      uint32_t actions, uint64_t visits,
                                      uint64_t last_served);

/**
 * Render one stats snapshot as a single telemetry line, in the style of the
 * solver's other counters, so a baseline run and an uncertainty-aware run can
 * be compared field by field:
 *
 *   work_priority items=40 buckets=8 coverage_promotions=40
 *     aging_promotions=0 unresolved=10 mean_score=0.0016 mean_delay=0.000
 *     depth=30,0,0,0,0,0,10,0
 *
 * `depth` is the queue depth per bucket, lowest bucket first. @return the
 * length the line needs, excluding the terminator, like snprintf; a NULL
 * `out` or a zero capacity measures without writing.
 */
size_t pe_work_priority_format_stats(const pe_work_priority_stats_t *stats,
                                     uint32_t buckets, char *out,
                                     size_t capacity);

const char *pe_work_scheduler_policy_name(pe_work_scheduler_policy_t policy);

/** Parse "fifo" / "balanced" / "uncertainty-aware". @return 0, or -1. */
int pe_work_scheduler_policy_parse(const char *name,
                                   pe_work_scheduler_policy_t *out);

/**
 * Set one field from text: "policy" (a name), "min-visits" and "buckets"
 * (unsigned integers), "aging-interval" (an unsigned 64-bit integer),
 * "epsilon" and "bucket-ratio" (numbers). Ranges are checked later, by
 * pe_work_priority_resolve. @return 0, or -1 for an unknown key or a value
 * that is not of that kind.
 */
int pe_work_priority_parse_option(pe_work_priority_config_t *config,
                                  const char *key, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_PRIORITY_H */
