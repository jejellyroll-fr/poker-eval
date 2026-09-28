/* online_stats.c - allocation-free running statistics (issue #256). */

#include <poker_eval/solver/pe_online_stats.h>

#include <math.h>
#include <stddef.h>

#include "finite_double.h"

void pe_online_stats_reset(pe_online_stats_t *stats)
{
    if (!stats)
        return;
    stats->n = 0u;
    stats->mean = 0.0;
    stats->m2 = 0.0;
}

void pe_online_stats_add(pe_online_stats_t *stats, double value)
{
    double delta;
    if (!stats || !pe_finite_double(value))
        return;
    stats->n++;
    delta = value - stats->mean;
    stats->mean += delta / (double)stats->n;
    /* (value - old mean) * (value - new mean): Welford's update. */
    stats->m2 += delta * (value - stats->mean);
}

void pe_online_stats_merge(pe_online_stats_t *into,
                           const pe_online_stats_t *other)
{
    double n_a, n_b, n, delta;
    if (!into || !other || other->n == 0u)
        return;
    if (into->n == 0u)
    {
        *into = *other;
        return;
    }
    n_a = (double)into->n;
    n_b = (double)other->n;
    n = n_a + n_b;
    delta = other->mean - into->mean;
    into->mean += delta * (n_b / n);
    into->m2 += other->m2 + delta * delta * (n_a * n_b / n);
    into->n += other->n;
}

double pe_online_stats_variance(const pe_online_stats_t *stats)
{
    double v;
    if (!stats || stats->n < 2u)
        return 0.0;
    v = stats->m2 / (double)(stats->n - 1u);
    return v > 0.0 ? v : 0.0; /* rounding cannot make it negative */
}

double pe_online_stats_std_error(const pe_online_stats_t *stats)
{
    if (!stats || stats->n < 2u)
        return 0.0;
    return sqrt(pe_online_stats_variance(stats) / (double)stats->n);
}

double pe_online_stats_half_width(const pe_online_stats_t *stats, double z)
{
    return z * pe_online_stats_std_error(stats);
}

double pe_confidence_z(double level)
{
    double lo = 0.0, hi = 40.0;
    if (!(level > 0.0 && level < 1.0))
        return NAN;
    /* P(|Z| <= z) = erf(z / sqrt 2) is increasing in z: bisect it. Sixty
       halvings of [0, 40] leave an interval far below double resolution. */
    for (int i = 0; i < 60; ++i)
    {
        double mid = 0.5 * (lo + hi);
        if (erf(mid / sqrt(2.0)) < level)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

int pe_online_stats_resolved(const pe_online_stats_t *stats, double z,
                             double absolute_tolerance,
                             double relative_tolerance)
{
    double tolerance;
    if (!stats || stats->n < 2u)
        return 0;
    tolerance = relative_tolerance * fabs(stats->mean);
    if (absolute_tolerance > tolerance)
        tolerance = absolute_tolerance;
    return pe_online_stats_half_width(stats, z) <= tolerance;
}

int pe_online_stats_side(const pe_online_stats_t *stats, double z,
                         double boundary)
{
    double half;
    if (!stats || stats->n < 2u)
        return 0;
    half = pe_online_stats_half_width(stats, z);
    if (stats->mean - half > boundary)
        return 1;
    if (stats->mean + half < boundary)
        return -1;
    return 0;
}

void pe_pooled_variance_add(pe_pooled_variance_t *pooled,
                            const pe_online_stats_t *group)
{
    if (!pooled || !group || group->n < 2u)
        return;
    pooled->dof += group->n - 1u;
    pooled->ssd += group->m2 > 0.0 ? group->m2 : 0.0;
}

double pe_pooled_variance_value(const pe_pooled_variance_t *pooled)
{
    if (!pooled || pooled->dof == 0u)
        return 0.0;
    return pooled->ssd / (double)pooled->dof;
}
