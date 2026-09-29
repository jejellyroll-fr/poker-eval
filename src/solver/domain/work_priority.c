/* work_priority.c - uncertainty-aware work prioritisation (issue #258).
   See <poker_eval/solver/pe_work_priority.h>. */

#include <poker_eval/solver/pe_work_priority.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "finite_double.h"

/* "Left at zero" without == on a double; false for NaN. */
static int is_unset(double value) { return value >= 0.0 && value <= 0.0; }

int pe_work_priority_resolve(const pe_work_priority_config_t *in,
                             pe_work_priority_config_t *out)
{
    pe_work_priority_config_t r;
    if (!out)
        return -1;
    if (in)
        r = *in;
    else
        memset(&r, 0, sizeof(r));
    if ((int)r.policy < 0 || r.policy >= PE_WORK_SCHED_COUNT)
        return -1;
    if (r.buckets == 0u)
        r.buckets = PE_WORK_PRIORITY_DEFAULT_BUCKETS;
    if (is_unset(r.bucket_ratio))
        r.bucket_ratio = PE_WORK_PRIORITY_DEFAULT_RATIO;
    if (is_unset(r.epsilon))
        r.epsilon = PE_WORK_PRIORITY_DEFAULT_EPSILON;
    if (r.min_visits == 0u)
        r.min_visits = PE_WORK_PRIORITY_DEFAULT_MIN_VISITS;
    if (is_unset(r.assumed_stderr))
        r.assumed_stderr = PE_WORK_PRIORITY_DEFAULT_ASSUMED_STDERR;
    if (r.buckets < 2u || r.buckets > PE_WORK_PRIORITY_MAX_BUCKETS)
        return -1;
    if (!(r.bucket_ratio > 1.0) || !pe_finite_double(r.bucket_ratio))
        return -1;
    if (!pe_finite_double(r.epsilon) || r.epsilon < 0.0)
        return -1;
    if (!pe_finite_double(r.assumed_stderr) || !(r.assumed_stderr > 0.0))
        return -1;
    *out = r;
    return 0;
}

double pe_work_priority_score(const pe_work_priority_config_t *resolved,
                              const pe_work_priority_item_t *item)
{
    double gap, uncertainty, floor_gap;
    if (!resolved || !item || item->actions < 2u)
        return 0.0; /* nothing to decide */
    gap = item->best - item->second_best;
    /* Not strictly ahead: the ordering is unresolved, or an input is not a
       number. Nothing outranks a decision that could still flip. */
    if (!(gap > 0.0))
        return INFINITY;
    uncertainty = hypot(item->best_stderr, item->second_stderr);
    if (!(uncertainty >= 0.0))
        return INFINITY; /* a NaN spread is not evidence of resolution */
    /* No spread supplied: the gap is the only signal left, so score it
       against the spread assumed for such a decision. A zero here would rank
       every positive gap alike - a near tie as settled as a clear winner. */
    if (is_unset(uncertainty))
        uncertainty = resolved->assumed_stderr;
    floor_gap = resolved->epsilon;
    if (gap < floor_gap)
        gap = floor_gap;
    return uncertainty / gap;
}

/* The number of bucket boundaries ratio^0, ratio^1, ... the score reaches,
   clamped to the last bucket: bucket 0 is a score below ratio^0. A NaN score
   fails every comparison and lands in bucket 0. */
static uint32_t score_bucket(const pe_work_priority_config_t *resolved,
                             double score)
{
    uint32_t bucket = 0u;
    double threshold = 1.0; /* ratio^0 */
    while (bucket + 1u < resolved->buckets && score >= threshold)
    {
        ++bucket;
        threshold *= resolved->bucket_ratio;
    }
    return bucket;
}

int pe_work_priority_below_floor(const pe_work_priority_config_t *resolved,
                                 const pe_work_priority_item_t *item)
{
    if (!resolved || !item || resolved->policy == PE_WORK_SCHED_FIFO)
        return 0;
    return item->visits < resolved->min_visits;
}

uint32_t pe_work_priority_bucket(const pe_work_priority_config_t *resolved,
                                 const pe_work_priority_item_t *item,
                                 uint64_t epoch)
{
    uint32_t top, bucket;
    uint64_t age, promotion;
    if (!resolved || !item)
        return 0u;
    top = resolved->buckets - 1u;
    bucket = score_bucket(resolved, pe_work_priority_score(resolved, item));
    if (resolved->policy == PE_WORK_SCHED_FIFO)
        return bucket; /* FIFO ranks nothing; the bucket is a diagnostic */
    if (pe_work_priority_below_floor(resolved, item))
        return top; /* coverage floor: too little measured to be settled */
    if (resolved->aging_interval == 0u)
        return bucket;
    age = epoch > item->last_served ? epoch - item->last_served : 0u;
    promotion = age / resolved->aging_interval;
    if (promotion == 0u)
        return bucket;
    if (promotion >= (uint64_t)top)
        return top;
    bucket += (uint32_t)promotion;
    return bucket > top ? top : bucket;
}

/* Where a BALANCED emission puts the item that is `round`-th within its own
   bucket: every earlier round emits one item per non-empty bucket, and every
   bucket visited before this one emits an extra item in this round. `start`
   is the bucket the round begins at; the visit order walks down from it and
   wraps, so `rank` is how far `bucket` sits along that walk. */
static size_t balanced_position(const size_t *depth, uint32_t buckets,
                                uint32_t bucket, size_t round, uint32_t start)
{
    size_t position = 0u;
    uint32_t rank = (start + buckets - bucket) % buckets;
    uint32_t b;
    for (b = 0u; b < buckets; ++b)
    {
        size_t d = depth[b];
        position += d < round ? d : round;
        if (d > round && (start + buckets - b) % buckets < rank)
            position++;
    }
    return position;
}

static void accumulate(const pe_work_priority_config_t *resolved,
                       const pe_work_priority_item_t *item, uint64_t epoch,
                       uint32_t bucket, pe_work_priority_stats_t *stats)
{
    double score;
    if (!stats)
        return;
    stats->items++;
    stats->bucket_depth[bucket]++;
    stats->delay_sum +=
        epoch > item->last_served ? epoch - item->last_served : 0u;
    score = pe_work_priority_score(resolved, item);
    if (pe_finite_double(score))
    {
        stats->score_sum += score;
        stats->score_count++;
    }
    else
        stats->unresolved++;
    /* The score distribution, before any policy moves an item. Counted for
       every policy, so a baseline run and a prioritised one are comparable,
       and an unresolved score lands in the top bucket exactly where it ranks. */
    stats->score_depth[score_bucket(resolved, score)]++;
    if (resolved->policy == PE_WORK_SCHED_FIFO)
        return;
    if (pe_work_priority_below_floor(resolved, item))
        stats->coverage_promotions++;
    else if (score_bucket(resolved, score) < bucket)
        stats->aging_promotions++;
}

int pe_work_priority_order(const pe_work_priority_config_t *resolved,
                           const pe_work_priority_item_t *items, size_t count,
                           uint64_t epoch, size_t *out_order, size_t capacity,
                           pe_work_priority_stats_t *stats)
{
    size_t depth[PE_WORK_PRIORITY_MAX_BUCKETS];
    size_t cursor[PE_WORK_PRIORITY_MAX_BUCKETS];
    size_t offset[PE_WORK_PRIORITY_MAX_BUCKETS];
    size_t floored = 0u, written = 0u;
    size_t i, running;
    uint32_t b;

    if (!resolved || (!items && count != 0u) || (!out_order && count != 0u) ||
        capacity < count)
        return -1;
    if (stats)
        memset(stats, 0, sizeof(*stats));
    for (b = 0u; b < PE_WORK_PRIORITY_MAX_BUCKETS; ++b)
    {
        depth[b] = 0u;
        cursor[b] = 0u;
        offset[b] = 0u;
    }

    /* FIFO returns the input order untouched. The buckets are still counted,
       so a run can compare its workload against a prioritised one. */
    if (resolved->policy == PE_WORK_SCHED_FIFO)
    {
        for (i = 0u; i < count; ++i)
        {
            uint32_t bucket = pe_work_priority_bucket(resolved, &items[i], epoch);
            out_order[i] = i;
            accumulate(resolved, &items[i], epoch, bucket, stats);
        }
        return 0;
    }

    /* The coverage floor is a tier of its own, ahead of every bucket: it is
       the promise that prioritisation does not begin before a decision has
       been measured, and an aged item must not be able to jump it. */
    for (i = 0u; i < count; ++i)
    {
        uint32_t bucket = pe_work_priority_bucket(resolved, &items[i], epoch);
        accumulate(resolved, &items[i], epoch, bucket, stats);
        if (pe_work_priority_below_floor(resolved, &items[i]))
            floored++;
        else
            depth[bucket]++;
    }
    for (i = 0u; i < count; ++i)
        if (pe_work_priority_below_floor(resolved, &items[i]))
            out_order[written++] = i;

    if (resolved->policy == PE_WORK_SCHED_BALANCED)
    {
        /* A round-robin only balances if the round starts somewhere else next
           time. A caller that services the whole permutation does not care,
           but one that takes a prefix and recomputes - the usual shape - would
           otherwise see the same top bucket at position zero for ever. The
           epoch rotates the starting bucket through the *occupied* buckets,
           highest first. The period stays `buckets` epochs and is shared out
           in proportion: position epoch % buckets maps to the
           (position * occupied / buckets)-th occupied bucket, so each of n
           occupied buckets leads buckets/n epochs of every period, rounded,
           and epoch 0 is the plain highest-first order.

           Rotating through every bucket number instead let the empty ones
           hand their turns to the next occupied bucket down - with buckets 7
           and 0 alone, bucket 0 led seven epochs in eight. Indexing the
           occupied buckets by epoch % n instead is fair only while n holds
           still; a caller that services a prefix changes n from epoch to
           epoch, and the index then jumps about (the 40-decision workload's
           worst delay went from 65 rounds to 362). A fixed period sweeps the
           occupied buckets in order whatever n does. */
        uint32_t occupied = 0u, pick, start = resolved->buckets - 1u;
        for (b = resolved->buckets; b-- > 0u;)
            if (depth[b] != 0u)
                occupied++;
        if (occupied != 0u)
        {
            uint64_t position = epoch % (uint64_t)resolved->buckets;
            pick = (uint32_t)(position * (uint64_t)occupied /
                              (uint64_t)resolved->buckets);
            for (b = resolved->buckets; b-- > 0u;)
                if (depth[b] != 0u && pick-- == 0u)
                {
                    start = b;
                    break;
                }
        }
        for (i = 0u; i < count; ++i)
        {
            uint32_t bucket;
            size_t round;
            if (pe_work_priority_below_floor(resolved, &items[i]))
                continue;
            bucket = pe_work_priority_bucket(resolved, &items[i], epoch);
            round = cursor[bucket]++;
            out_order[floored +
                      balanced_position(depth, resolved->buckets, bucket, round,
                                        start)] = i;
        }
        return 0;
    }

    /* UNCERTAINTY_AWARE: highest bucket first, input order within a bucket. */
    running = floored;
    for (b = resolved->buckets; b-- > 0u;)
    {
        offset[b] = running;
        running += depth[b];
    }
    for (i = 0u; i < count; ++i)
    {
        uint32_t bucket;
        if (pe_work_priority_below_floor(resolved, &items[i]))
            continue;
        bucket = pe_work_priority_bucket(resolved, &items[i], epoch);
        out_order[offset[bucket] + cursor[bucket]++] = i;
    }
    return 0;
}

/* The standard error of an accumulator, or NaN when it cannot give one. A
   single observation has an undefined variance; reporting it as a zero spread
   would rank a decision nobody has measured twice as settled, which is the
   opposite of what it is. pe_online_stats_std_error() returns 0 there, so the
   distinction has to be made here, where the sample count is known. */
static double spread_of(const pe_online_stats_t *stats)
{
    if (!stats || stats->n < 2u)
        return NAN;
    return pe_online_stats_std_error(stats);
}

void pe_work_priority_item_from_stats(pe_work_priority_item_t *item,
                                      const pe_online_stats_t *best,
                                      const pe_online_stats_t *second_best,
                                      uint32_t actions, uint64_t visits,
                                      uint64_t last_served)
{
    if (!item)
        return;
    item->best = best ? best->mean : 0.0;
    item->best_stderr = spread_of(best);
    if (second_best)
    {
        item->second_best = second_best->mean;
        item->second_stderr = spread_of(second_best);
    }
    else
    {
        item->second_best = item->best;
        item->second_stderr = 0.0;
    }
    item->visits = visits;
    item->last_served = last_served;
    item->actions = actions;
}

/* Append literal text. The format string is always a literal here, so the
   build's -Wformat=2 stays satisfied. */
static void append_text(char *out, size_t capacity, size_t *used,
                        const char *text)
{
    int n = snprintf(out && *used < capacity ? out + *used : NULL,
                     out && *used < capacity ? capacity - *used : 0u, "%s",
                     text);
    if (n > 0)
        *used += (size_t)n;
}

uint32_t pe_work_priority_percentile_bucket(
    const pe_work_priority_stats_t *stats, uint32_t buckets, double percentile)
{
    uint64_t total = 0u;
    uint64_t target;
    uint64_t running = 0u;
    double rank;
    uint32_t b;

    if (!stats || buckets < 2u)
        return 0u;
    if (buckets > PE_WORK_PRIORITY_MAX_BUCKETS)
        buckets = PE_WORK_PRIORITY_MAX_BUCKETS;
    if (!(percentile > 0.0))
        percentile = 0.0;
    else if (percentile > 1.0)
        percentile = 1.0;
    for (b = 0u; b < buckets; ++b)
        total += stats->score_depth[b];
    if (total == 0u)
        return 0u;
    /* Nearest rank: the smallest bucket whose cumulative count reaches the
       target, with the rank taken as ceil(p * total) and never below 1. The
       ceil lands in a double first: the strict build refuses a direct cast
       from a function call (-Wbad-function-cast). */
    rank = ceil(percentile * (double)total);
    target = rank > 0.0 ? (uint64_t)rank : 1u;
    for (b = 0u; b < buckets; ++b)
    {
        running += stats->score_depth[b];
        if (running >= target)
            return b;
    }
    return buckets - 1u;
}

size_t pe_work_priority_format_stats(const pe_work_priority_stats_t *stats,
                                     uint32_t buckets, char *out,
                                     size_t capacity)
{
    char head[256];
    char piece[32];
    size_t used = 0u;
    uint32_t b;

    if (!stats)
        return 0u;
    if (buckets > PE_WORK_PRIORITY_MAX_BUCKETS)
        buckets = PE_WORK_PRIORITY_MAX_BUCKETS;
    if (out && capacity > 0u)
        out[0] = '\0';

    snprintf(head, sizeof(head),
             "work_priority items=%llu buckets=%u coverage_promotions=%llu "
             "aging_promotions=%llu unresolved=%llu mean_score=%.6g "
             "mean_delay=%.3f p50_bucket=%u p90_bucket=%u depth=",
             (unsigned long long)stats->items, buckets,
             (unsigned long long)stats->coverage_promotions,
             (unsigned long long)stats->aging_promotions,
             (unsigned long long)stats->unresolved,
             stats->score_count != 0u
                 ? stats->score_sum / (double)stats->score_count
                 : 0.0,
             stats->items != 0u ? (double)stats->delay_sum / (double)stats->items
                                : 0.0,
             pe_work_priority_percentile_bucket(stats, buckets, 0.5),
             pe_work_priority_percentile_bucket(stats, buckets, 0.9));
    append_text(out, capacity, &used, head);
    for (b = 0u; b < buckets; ++b)
    {
        if (b == 0u)
            snprintf(piece, sizeof(piece), "%llu",
                     (unsigned long long)stats->bucket_depth[b]);
        else
            snprintf(piece, sizeof(piece), ",%llu",
                     (unsigned long long)stats->bucket_depth[b]);
        append_text(out, capacity, &used, piece);
    }
    return used;
}

const char *pe_work_scheduler_policy_name(pe_work_scheduler_policy_t policy)
{
    switch (policy)
    {
    case PE_WORK_SCHED_FIFO:
        return "fifo";
    case PE_WORK_SCHED_BALANCED:
        return "balanced";
    case PE_WORK_SCHED_UNCERTAINTY_AWARE:
        return "uncertainty-aware";
    case PE_WORK_SCHED_COUNT:
        break;
    default:
        break;
    }
    return "unknown";
}

int pe_work_scheduler_policy_parse(const char *name,
                                   pe_work_scheduler_policy_t *out)
{
    if (!name || !out)
        return -1;
    if (strcmp(name, "fifo") == 0)
        *out = PE_WORK_SCHED_FIFO;
    else if (strcmp(name, "balanced") == 0)
        *out = PE_WORK_SCHED_BALANCED;
    else if (strcmp(name, "uncertainty-aware") == 0)
        *out = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    else
        return -1;
    return 0;
}

/* strtoul and strtoull skip leading whitespace and accept a sign, and they
   negate a "-1" into the type's maximum rather than refusing it: " -1" would
   become UINT64_MAX, an aging interval that silently disables aging. An
   unsigned option must therefore start with a digit, nothing else. */
static int starts_with_digit(const char *text)
{
    return text && isdigit((unsigned char)text[0]);
}

static int parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!starts_with_digit(text))
        return -1;
    errno = 0;
    v = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || v > UINT32_MAX)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_u64(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v;
    if (!starts_with_digit(text))
        return -1;
    errno = 0;
    v = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return -1;
    *out = (uint64_t)v;
    return 0;
}

static int parse_real(const char *text, double *out)
{
    char *end = NULL;
    double v;
    if (!text || !*text)
        return -1;
    errno = 0;
    v = strtod(text, &end);
    if (errno != 0 || !end || *end != '\0' || !pe_finite_double(v))
        return -1;
    *out = v;
    return 0;
}

int pe_work_priority_parse_option(pe_work_priority_config_t *config,
                                  const char *key, const char *value)
{
    if (!config || !key)
        return -1;
    if (strcmp(key, "policy") == 0)
        return pe_work_scheduler_policy_parse(value, &config->policy);
    if (strcmp(key, "min-visits") == 0)
        return parse_u32(value, &config->min_visits);
    if (strcmp(key, "buckets") == 0)
        return parse_u32(value, &config->buckets);
    if (strcmp(key, "aging-interval") == 0)
        return parse_u64(value, &config->aging_interval);
    if (strcmp(key, "epsilon") == 0)
        return parse_real(value, &config->epsilon);
    if (strcmp(key, "bucket-ratio") == 0)
        return parse_real(value, &config->bucket_ratio);
    if (strcmp(key, "assumed-stderr") == 0)
        return parse_real(value, &config->assumed_stderr);
    return -1;
}
