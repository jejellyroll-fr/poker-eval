/*
 * pe_online_stats.h - allocation-free running statistics (issue #256)
 *
 * Welford's online mean and variance, the parallel merge of two
 * accumulators (Chan et al.), a pooled within-group variance, and the
 * confidence helpers a sampling policy needs to decide whether more work is
 * useful: standard error, confidence half-width, a tolerance test and a
 * side-of-boundary test.
 *
 * Welford is used rather than the textbook sum and sum of squares because the
 * latter cancels catastrophically when the mean is large next to the spread:
 * with values near 1e9 and a spread of 1, sum(x^2)/n - mean^2 loses every
 * significant digit, while Welford keeps them.
 */

#ifndef POKER_EVAL_PE_ONLINE_STATS_H
#define POKER_EVAL_PE_ONLINE_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Running count, mean and sum of squared deviations from the mean. */
typedef struct pe_online_stats_t {
    uint64_t n;
    double mean;
    double m2;
} pe_online_stats_t;

void pe_online_stats_reset(pe_online_stats_t *stats);

/** Add one observation. A non-finite value is ignored and not counted. */
void pe_online_stats_add(pe_online_stats_t *stats, double value);

/** Fold `other` into `into`, as if every observation had been added there. */
void pe_online_stats_merge(pe_online_stats_t *into,
                           const pe_online_stats_t *other);

/** Unbiased sample variance (n - 1 denominator); 0 below two observations. */
double pe_online_stats_variance(const pe_online_stats_t *stats);

/** Standard error of the mean, sqrt(variance / n); 0 below two observations. */
double pe_online_stats_std_error(const pe_online_stats_t *stats);

/** z times the standard error. */
double pe_online_stats_half_width(const pe_online_stats_t *stats, double z);

/**
 * Two-sided standard normal quantile for a confidence level in (0, 1): the z
 * with P(|Z| <= z) = level, e.g. 1.959964 for 0.95. NaN outside (0, 1).
 */
double pe_confidence_z(double level);

/**
 * Whether the estimate is resolved: at least two observations and
 *     half_width <= max(absolute_tolerance, relative_tolerance * |mean|).
 */
int pe_online_stats_resolved(const pe_online_stats_t *stats, double z,
                             double absolute_tolerance,
                             double relative_tolerance);

/**
 * Where the confidence interval lies against `boundary`: +1 entirely above,
 * -1 entirely below, 0 when it straddles it or fewer than two observations
 * make it undefined.
 */
int pe_online_stats_side(const pe_online_stats_t *stats, double z,
                         double boundary);

/**
 * Pooled within-group variance: groups of observations that each have their
 * own mean (for example the replicate draws of one chance node visit) share
 * one spread estimate, sum of within-group squared deviations over the sum
 * of (group size - 1). Differences between group means do not enter it.
 */
typedef struct pe_pooled_variance_t {
    uint64_t dof;
    double ssd;
} pe_pooled_variance_t;

/** Add a group; a group of fewer than two observations adds nothing. */
void pe_pooled_variance_add(pe_pooled_variance_t *pooled,
                            const pe_online_stats_t *group);

/** The pooled variance; 0 when no group had two observations. */
double pe_pooled_variance_value(const pe_pooled_variance_t *pooled);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_ONLINE_STATS_H */
