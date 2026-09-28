/*
 * test_work_priority.c - uncertainty-aware work prioritisation (issue #258).
 *
 *  1. Hand-checkable fixtures: the score and the bucket boundaries are
 *     computed on values whose exact answer is known in binary floating
 *     point (gaps and spreads of 2^-5, 2^-6, 2^-7, 2^-8), which pins both
 *     the library and the oracle before anything random runs.
 *  2. The ordering against a brute-force oracle: for small batches the
 *     oracle enumerates all n! permutations and keeps the one consistent
 *     with the documented comparator, so the counting sort and the
 *     round-robin interleave are checked against the specification.
 *  3. The two anti-starvation rules, measured: the coverage floor gives an
 *     unmeasured decision its turn, and aging is what stops a settled
 *     region from being starved once the floor has been served.
 *  4. A synthetic workload: 40 decisions, 10 of them near-indifferent,
 *     serviced 2,000 times, comparing the share of work the near-indifferent
 *     group receives under FIFO and under uncertainty-aware scheduling.
 *  5. The integration point: a pe_work_unit_t array permuted by the order,
 *     which is how the coordinator is meant to consume this.
 */

#include <poker_eval/solver/pe_work_priority.h>
#include <poker_eval/solver/pe_work_unit.h>

#include "support/pe_work_priority_oracle.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

/* ---------------------------------------------------------------- *
 * Helpers
 * ---------------------------------------------------------------- */

static pe_work_priority_config_t make_config(pe_work_scheduler_policy_t policy,
                                             uint32_t min_visits,
                                             uint32_t buckets,
                                             uint64_t aging_interval)
{
    pe_work_priority_config_t in;
    pe_work_priority_config_t out;
    memset(&in, 0, sizeof(in));
    in.policy = policy;
    in.min_visits = min_visits;
    in.buckets = buckets;
    in.aging_interval = aging_interval;
    if (pe_work_priority_resolve(&in, &out) != 0)
    {
        fprintf(stderr, "make_config: resolve refused\n");
        g_failures++;
    }
    return out;
}

static void fill_ctx(oracle_ctx_t *ctx, const pe_work_priority_config_t *config,
                     const pe_work_priority_item_t *items, size_t count,
                     uint64_t epoch)
{
    ctx->config = *config;
    ctx->items = items;
    ctx->count = count;
    ctx->epoch = epoch;
}

static uint64_t rng_next(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static double rng_unit(uint64_t *s)
{
    return ((double)(rng_next(s) >> 11) + 0.5) / 9007199254740992.0;
}

/* ---------------------------------------------------------------- *
 * 1. Fixtures
 * ---------------------------------------------------------------- */

/* Every value here is a power of two, so gap, spread and their ratio are
   exact in double and the bucket boundary is not a coin flip. */
static void test_score_fixtures(void)
{
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    pe_work_priority_item_t item;
    double score;

    /* Nothing to decide. */
    item = oracle_item(5.0, 1.0, 0.5, 0.5, 3u, 0u, 1u);
    CHECK(pe_work_priority_score(&config, &item) >= 0.0 &&
              pe_work_priority_score(&config, &item) <= 0.0,
          "a single action must score zero, got %g",
          pe_work_priority_score(&config, &item));

    /* The issue's first example: a clear winner, no spread recorded. */
    item = oracle_item(12.4, 6.1, 0.0, 0.0, 3u, 0u, 3u);
    score = pe_work_priority_score(&config, &item);
    CHECK(score >= 0.0 && score <= 0.0,
          "a settled decision with no spread must score zero, got %g", score);

    /* The issue's second example: raise 2.102, call 2.097, both +-0.01. */
    item = oracle_item(2.102, 2.097, 0.01, 0.01, 3u, 0u, 3u);
    score = pe_work_priority_score(&config, &item);
    CHECK(fabs(score - sqrt(2.0) * 2.0) < 1e-12,
          "near tie must score sqrt(2)*2 = %.15g, got %.15g",
          sqrt(2.0) * 2.0, score);

    /* Exact ratios: gap 2^-6 against spread 2^-6, 2^-5, 2^-7, 2^-8. */
    item = oracle_item(1.015625, 1.0, 0.015625, 0.0, 3u, 0u, 2u);
    CHECK(fabs(pe_work_priority_score(&config, &item) - 1.0) < 1e-15,
          "gap == spread must score 1, got %.17g",
          pe_work_priority_score(&config, &item));
    item = oracle_item(1.03125, 1.0, 0.015625, 0.0, 3u, 0u, 2u);
    CHECK(fabs(pe_work_priority_score(&config, &item) - 0.5) < 1e-15,
          "spread half the gap must score 0.5, got %.17g",
          pe_work_priority_score(&config, &item));
    item = oracle_item(1.0078125, 1.0, 0.015625, 0.0, 3u, 0u, 2u);
    CHECK(fabs(pe_work_priority_score(&config, &item) - 2.0) < 1e-15,
          "spread twice the gap must score 2, got %.17g",
          pe_work_priority_score(&config, &item));
    item = oracle_item(1.00390625, 1.0, 0.015625, 0.0, 3u, 0u, 2u);
    CHECK(fabs(pe_work_priority_score(&config, &item) - 4.0) < 1e-15,
          "spread four times the gap must score 4, got %.17g",
          pe_work_priority_score(&config, &item));

    /* Two spreads combine in quadrature, not by addition. */
    item = oracle_item(1.015625, 1.0, 0.011048543456039805,
                       0.011048543456039805, 3u, 0u, 2u);
    CHECK(fabs(pe_work_priority_score(&config, &item) - 1.0) < 1e-12,
          "two equal spreads must combine in quadrature, got %.17g",
          pe_work_priority_score(&config, &item));

    /* Unresolved orderings. */
    item = oracle_item(2.0, 2.0, 0.01, 0.01, 3u, 0u, 2u);
    CHECK(isinf(pe_work_priority_score(&config, &item)),
          "an exact tie must score +inf, got %g",
          pe_work_priority_score(&config, &item));
    item = oracle_item(2.0, 2.1, 0.01, 0.01, 3u, 0u, 2u);
    CHECK(isinf(pe_work_priority_score(&config, &item)),
          "a leader behind its runner-up must score +inf, got %g",
          pe_work_priority_score(&config, &item));

    /* The oracle agrees with the library on every fixture above. */
    {
        const pe_work_priority_item_t fixtures[8] = {
            {5.0, 1.0, 0.5, 0.5, 3u, 0u, 1u},
            {12.4, 6.1, 0.0, 0.0, 3u, 0u, 3u},
            {2.102, 2.097, 0.01, 0.01, 3u, 0u, 3u},
            {1.015625, 1.0, 0.015625, 0.0, 3u, 0u, 2u},
            {1.03125, 1.0, 0.015625, 0.0, 3u, 0u, 2u},
            {1.0078125, 1.0, 0.015625, 0.0, 3u, 0u, 2u},
            {2.0, 2.0, 0.01, 0.01, 3u, 0u, 2u},
            {2.0, 2.1, 0.01, 0.01, 3u, 0u, 2u}
        };
        size_t i;
        for (i = 0u; i < 8u; ++i)
        {
            double lib = pe_work_priority_score(&config, &fixtures[i]);
            long double ref = oracle_score(&config, &fixtures[i]);
            int same = (isinf(lib) && isinf((double)ref)) ||
                       (isfinite(lib) && isfinite((double)ref) &&
                        fabs(lib - (double)ref) <= 1e-12 * fabs(lib));
            CHECK(same, "fixture %zu: library %.17g vs oracle %.17Lg", i, lib,
                  ref);
        }
    }
}

static void test_bucket_fixtures(void)
{
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    pe_work_priority_item_t item;
    uint32_t top = config.buckets - 1u;

    /* Bucket 0 is a score below ratio^0 = 1. */
    item = oracle_item(1.03125, 1.0, 0.015625, 0.0, 1u, 0u, 2u); /* score 0.5 */
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == 0u,
          "score 0.5 must land in bucket 0, got %u",
          pe_work_priority_bucket(&config, &item, 0u));
    item = oracle_item(12.4, 6.1, 0.0, 0.0, 1u, 0u, 3u); /* score 0 */
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == 0u,
          "score 0 must land in bucket 0, got %u",
          pe_work_priority_bucket(&config, &item, 0u));

    /* The boundaries themselves: score 1 -> 1, 2 -> 2, 4 -> 3. */
    item = oracle_item(1.015625, 1.0, 0.015625, 0.0, 1u, 0u, 2u);
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == 1u,
          "score 1 must land in bucket 1, got %u",
          pe_work_priority_bucket(&config, &item, 0u));
    item = oracle_item(1.0078125, 1.0, 0.015625, 0.0, 1u, 0u, 2u);
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == 2u,
          "score 2 must land in bucket 2, got %u",
          pe_work_priority_bucket(&config, &item, 0u));
    item = oracle_item(1.00390625, 1.0, 0.015625, 0.0, 1u, 0u, 2u);
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == 3u,
          "score 4 must land in bucket 3, got %u",
          pe_work_priority_bucket(&config, &item, 0u));

    /* An unresolved ordering saturates the top bucket. */
    item = oracle_item(2.0, 2.0, 0.01, 0.01, 1u, 0u, 2u);
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == top,
          "an unresolved ordering must saturate bucket %u, got %u", top,
          pe_work_priority_bucket(&config, &item, 0u));

    /* A bucket count of 2 leaves room for "settled" and "not". */
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 2u, 0u);
    item = oracle_item(1.00390625, 1.0, 0.015625, 0.0, 1u, 0u, 2u);
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == 1u,
          "with 2 buckets, score 4 must saturate bucket 1, got %u",
          pe_work_priority_bucket(&config, &item, 0u));
    item = oracle_item(1.03125, 1.0, 0.015625, 0.0, 1u, 0u, 2u);
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == 0u,
          "with 2 buckets, score 0.5 must land in bucket 0, got %u",
          pe_work_priority_bucket(&config, &item, 0u));
}

/* The boundaries are ratio^k, and a score sitting exactly on one belongs to
   the upper bucket. Swept over two ratios whose powers are exact in binary,
   with a gap of exactly 1 so the score is the spread itself, so a comparison
   that drifts from >= to > cannot hide behind a single fixture. */
static void test_bucket_boundaries(void)
{
    static const struct
    {
        double ratio;
        double edge;
        uint32_t bucket;
    } cases[] = {
        {2.0, 0.5, 0u},  {2.0, 1.0, 1u}, {2.0, 2.0, 2u},
        {2.0, 4.0, 3u},  {2.0, 8.0, 3u}, {4.0, 0.5, 0u},
        {4.0, 1.0, 1u},  {4.0, 4.0, 2u}, {4.0, 16.0, 3u},
        {4.0, 64.0, 3u}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        pe_work_priority_config_t in, config;
        pe_work_priority_item_t item;

        memset(&in, 0, sizeof(in));
        in.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
        in.min_visits = 1u;
        in.buckets = 4u;
        in.bucket_ratio = cases[i].ratio;
        if (pe_work_priority_resolve(&in, &config) != 0)
        {
            CHECK(0, "case %zu (ratio %g) must resolve", i, cases[i].ratio);
            continue;
        }

        /* best - second is exactly 1, so the score is the spread exactly. */
        item = oracle_item(cases[i].edge + 1.0, cases[i].edge, cases[i].edge,
                           0.0, 1u, 0u, 2u);
        CHECK(fabs(pe_work_priority_score(&config, &item) - cases[i].edge) <
                  1e-15,
              "case %zu: the score must be %g, got %.17g", i, cases[i].edge,
              pe_work_priority_score(&config, &item));
        CHECK(pe_work_priority_bucket(&config, &item, 0u) == cases[i].bucket,
              "case %zu (ratio %g, score %g): the bucket must be %u, got %u", i,
              cases[i].ratio, cases[i].edge, cases[i].bucket,
              pe_work_priority_bucket(&config, &item, 0u));
    }
}

/* ---------------------------------------------------------------- *
 * 2. Configuration
 * ---------------------------------------------------------------- */

static void test_configuration(void)
{
    pe_work_priority_config_t in;
    pe_work_priority_config_t out;

    memset(&in, 0, sizeof(in));
    CHECK(pe_work_priority_resolve(&in, &out) == 0, "zeroed config must resolve");
    CHECK(out.policy == PE_WORK_SCHED_FIFO, "default policy must be FIFO");
    CHECK(out.buckets == PE_WORK_PRIORITY_DEFAULT_BUCKETS,
          "default bucket count is %u, got %u", PE_WORK_PRIORITY_DEFAULT_BUCKETS,
          out.buckets);
    CHECK(fabs(out.bucket_ratio - PE_WORK_PRIORITY_DEFAULT_RATIO) < 1e-15,
          "default ratio is %g, got %g", PE_WORK_PRIORITY_DEFAULT_RATIO,
          out.bucket_ratio);
    CHECK(fabs(out.epsilon - PE_WORK_PRIORITY_DEFAULT_EPSILON) < 1e-30,
          "default epsilon is %g, got %g", PE_WORK_PRIORITY_DEFAULT_EPSILON,
          out.epsilon);
    CHECK(out.min_visits == PE_WORK_PRIORITY_DEFAULT_MIN_VISITS,
          "default min_visits is %u, got %u",
          PE_WORK_PRIORITY_DEFAULT_MIN_VISITS, out.min_visits);
    CHECK(out.aging_interval == 0u, "aging is off by default");

    CHECK(pe_work_priority_resolve(NULL, &out) == 0,
          "a NULL config must resolve to the defaults");
    CHECK(out.policy == PE_WORK_SCHED_FIFO, "NULL must resolve to FIFO");
    CHECK(pe_work_priority_resolve(&in, NULL) == -1, "a NULL out must be refused");

    /* A written-out default must resolve to the same thing as the implicit
       one, so a caller can be explicit without changing behaviour. */
    memset(&in, 0, sizeof(in));
    in.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    in.min_visits = PE_WORK_PRIORITY_DEFAULT_MIN_VISITS;
    in.buckets = PE_WORK_PRIORITY_DEFAULT_BUCKETS;
    in.bucket_ratio = PE_WORK_PRIORITY_DEFAULT_RATIO;
    in.epsilon = PE_WORK_PRIORITY_DEFAULT_EPSILON;
    {
        pe_work_priority_config_t implicit_cfg;
        memset(&implicit_cfg, 0, sizeof(implicit_cfg));
        implicit_cfg.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
        CHECK(pe_work_priority_resolve(&in, &out) == 0, "explicit defaults resolve");
        {
            pe_work_priority_config_t a, b;
            CHECK(pe_work_priority_resolve(&in, &a) == 0, "explicit");
            CHECK(pe_work_priority_resolve(&implicit_cfg, &b) == 0, "implicit");
            CHECK(a.buckets == b.buckets && a.min_visits == b.min_visits &&
                      a.aging_interval == b.aging_interval &&
                      fabs(a.bucket_ratio - b.bucket_ratio) < 1e-15 &&
                      fabs(a.epsilon - b.epsilon) < 1e-30,
                  "explicit defaults must equal implicit defaults");
        }
    }

    /* Refusals. */
    memset(&in, 0, sizeof(in));
    in.policy = (pe_work_scheduler_policy_t)PE_WORK_SCHED_COUNT;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "policy == COUNT refused");
    memset(&in, 0, sizeof(in));
    in.policy = (pe_work_scheduler_policy_t)-1;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "a negative policy refused");
    memset(&in, 0, sizeof(in));
    in.buckets = 1u;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "1 bucket refused");
    memset(&in, 0, sizeof(in));
    in.buckets = PE_WORK_PRIORITY_MAX_BUCKETS + 1u;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "too many buckets refused");
    memset(&in, 0, sizeof(in));
    in.bucket_ratio = 1.0;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "ratio 1.0 refused");
    memset(&in, 0, sizeof(in));
    in.bucket_ratio = 0.5;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "ratio below 1 refused");
    memset(&in, 0, sizeof(in));
    in.epsilon = -1.0;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "a negative epsilon refused");
    memset(&in, 0, sizeof(in));
    in.epsilon = INFINITY;
    CHECK(pe_work_priority_resolve(&in, &out) == -1, "an infinite epsilon refused");
    memset(&in, 0, sizeof(in));
    in.buckets = PE_WORK_PRIORITY_MAX_BUCKETS;
    CHECK(pe_work_priority_resolve(&in, &out) == 0, "32 buckets accepted");

    /* Names round-trip. */
    {
        pe_work_scheduler_policy_t policy;
        CHECK(pe_work_scheduler_policy_parse("fifo", &policy) == 0 &&
                  policy == PE_WORK_SCHED_FIFO, "parse fifo");
        CHECK(pe_work_scheduler_policy_parse("balanced", &policy) == 0 &&
                  policy == PE_WORK_SCHED_BALANCED, "parse balanced");
        CHECK(pe_work_scheduler_policy_parse("uncertainty-aware", &policy) == 0 &&
                  policy == PE_WORK_SCHED_UNCERTAINTY_AWARE,
              "parse uncertainty-aware");
        CHECK(pe_work_scheduler_policy_parse("nonsense", &policy) == -1,
              "an unknown name must be refused");
        CHECK(strcmp(pe_work_scheduler_policy_name(PE_WORK_SCHED_FIFO), "fifo") == 0,
              "fifo name");
        CHECK(strcmp(pe_work_scheduler_policy_name(PE_WORK_SCHED_BALANCED),
                     "balanced") == 0, "balanced name");
        CHECK(strcmp(pe_work_scheduler_policy_name(PE_WORK_SCHED_UNCERTAINTY_AWARE),
                     "uncertainty-aware") == 0, "uncertainty-aware name");
        CHECK(strcmp(pe_work_scheduler_policy_name((pe_work_scheduler_policy_t)99),
                     "unknown") == 0, "an out-of-range policy has no name");
    }
}

static void test_options(void)
{
    pe_work_priority_config_t config;
    memset(&config, 0, sizeof(config));

    CHECK(pe_work_priority_parse_option(&config, "policy",
                                        "uncertainty-aware") == 0 &&
              config.policy == PE_WORK_SCHED_UNCERTAINTY_AWARE, "option policy");
    CHECK(pe_work_priority_parse_option(&config, "min-visits", "7") == 0 &&
              config.min_visits == 7u, "option min-visits");
    CHECK(pe_work_priority_parse_option(&config, "buckets", "12") == 0 &&
              config.buckets == 12u, "option buckets");
    CHECK(pe_work_priority_parse_option(&config, "aging-interval", "5000000000") == 0 &&
              config.aging_interval == 5000000000ull, "option aging-interval");
    CHECK(pe_work_priority_parse_option(&config, "epsilon", "0.25") == 0 &&
              fabs(config.epsilon - 0.25) < 1e-15, "option epsilon");
    CHECK(pe_work_priority_parse_option(&config, "bucket-ratio", "3") == 0 &&
              fabs(config.bucket_ratio - 3.0) < 1e-15, "option bucket-ratio");

    CHECK(pe_work_priority_parse_option(&config, "nonsense", "1") == -1,
          "an unknown key must be refused");
    CHECK(pe_work_priority_parse_option(&config, "min-visits", "-1") == -1,
          "a negative integer must be refused");
    CHECK(pe_work_priority_parse_option(&config, "min-visits", "abc") == -1,
          "a non-numeric integer must be refused");
    CHECK(pe_work_priority_parse_option(&config, "min-visits", "") == -1,
          "an empty value must be refused");
    CHECK(pe_work_priority_parse_option(&config, "epsilon", "nan") == -1,
          "a non-finite number must be refused");
    CHECK(pe_work_priority_parse_option(&config, "epsilon", "1.5x") == -1,
          "a trailing character must be refused");
    CHECK(pe_work_priority_parse_option(&config, "policy", "lifo") == -1,
          "an unknown policy name must be refused");
    CHECK(pe_work_priority_parse_option(&config, "aging-interval", "-3") == -1,
          "a negative aging interval must be refused");
    CHECK(pe_work_priority_parse_option(NULL, "buckets", "4") == -1,
          "a NULL config must be refused");

    /* The parsed values are what resolve accepts. */
    {
        pe_work_priority_config_t resolved;
        CHECK(pe_work_priority_resolve(&config, &resolved) == 0,
              "a fully parsed config must resolve");
        CHECK(resolved.buckets == 12u && resolved.min_visits == 7u,
              "parsed values must survive resolve");
    }
}

/* ---------------------------------------------------------------- *
 * 3. Anti-starvation
 * ---------------------------------------------------------------- */

static void test_coverage_floor(void)
{
    /* A decision that has never been serviced outranks a settled one, even
       though the settled one's score is far lower and its index is lower. */
    pe_work_priority_item_t items[2];
    pe_work_priority_config_t config;
    size_t order[2];

    items[0] = oracle_item(12.4, 6.1, 0.0, 0.0, 9u, 0u, 3u); /* settled, low index */
    items[1] = oracle_item(2.102, 2.097, 0.01, 0.01, 0u, 0u, 3u); /* unmeasured */

    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    CHECK(pe_work_priority_order(&config, items, 2u, 0u, order, 2u, NULL) == 0,
          "order must succeed");
    CHECK(order[0] == 1u,
          "the unmeasured decision must be served first, got index %zu",
          order[0]);

    /* With the floor raised above the settled decision's service count, the
       settled one is promoted too and wins on its lower index. */
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 10u, 8u, 0u);
    CHECK(pe_work_priority_order(&config, items, 2u, 0u, order, 2u, NULL) == 0,
          "order must succeed");
    CHECK(order[0] == 0u,
          "both below the floor: the lower index must win, got %zu", order[0]);

    /* Once the floor is satisfied, the score decides again. */
    items[1].visits = 1u;
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    CHECK(pe_work_priority_order(&config, items, 2u, 0u, order, 2u, NULL) == 0,
          "order must succeed");
    CHECK(order[0] == 1u, "the fragile decision must still lead, got %zu",
          order[0]);

    /* The tier is hard: an item aged to the top bucket must not jump an item
       that has not been measured at all. This is the preemption that would
       starve the unserved decision. */
    items[0] = oracle_item(12.4, 6.1, 0.0, 0.0, 9u, 0u, 3u); /* aged to the top */
    items[1] = oracle_item(12.4, 6.1, 0.0, 0.0, 0u, 0u, 3u); /* unserved */
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 4u);
    CHECK(pe_work_priority_bucket(&config, &items[0], 1000u) ==
              config.buckets - 1u,
          "the aged item must be at the top bucket");
    CHECK(pe_work_priority_order(&config, items, 2u, 1000u, order, 2u, NULL) == 0,
          "order must succeed");
    CHECK(order[0] == 1u,
          "the coverage tier must outrank an aged item, got %zu", order[0]);
}

static void test_aging(void)
{
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 10u);
    pe_work_priority_item_t item =
        oracle_item(12.4, 6.1, 0.0, 0.0, 5u, 100u, 3u); /* settled: bucket 0 */
    uint32_t top = config.buckets - 1u;

    CHECK(pe_work_priority_bucket(&config, &item, 100u) == 0u,
          "no waiting means no promotion");
    CHECK(pe_work_priority_bucket(&config, &item, 105u) == 0u,
          "half an interval means no promotion");
    CHECK(pe_work_priority_bucket(&config, &item, 110u) == 1u,
          "one interval must promote by one bucket, got %u",
          pe_work_priority_bucket(&config, &item, 110u));
    CHECK(pe_work_priority_bucket(&config, &item, 130u) == 3u,
          "three intervals must promote by three buckets, got %u",
          pe_work_priority_bucket(&config, &item, 130u));
    CHECK(pe_work_priority_bucket(&config, &item, 1000u) == top,
          "a long wait must saturate at %u, got %u", top,
          pe_work_priority_bucket(&config, &item, 1000u));

    /* An epoch behind the last service is not a negative age. */
    CHECK(pe_work_priority_bucket(&config, &item, 50u) == 0u,
          "a last_served ahead of the epoch must not promote");

    /* Aging is inert under FIFO, which ranks nothing. */
    config = make_config(PE_WORK_SCHED_FIFO, 1u, 8u, 10u);
    CHECK(pe_work_priority_bucket(&config, &item, 1000u) == 0u,
          "FIFO must ignore aging, got %u",
          pe_work_priority_bucket(&config, &item, 1000u));

    /* Aging is inert when it is off. */
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    CHECK(pe_work_priority_bucket(&config, &item, 1000u) == 0u,
          "aging off must mean no promotion, got %u",
          pe_work_priority_bucket(&config, &item, 1000u));

    /* An item already at the top cannot be promoted past it. */
    {
        pe_work_priority_item_t fragile =
            oracle_item(2.0, 2.0, 0.01, 0.01, 5u, 0u, 2u);
        config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 10u);
        CHECK(pe_work_priority_bucket(&config, &fragile, 10000u) == top,
              "the top bucket is the ceiling, got %u",
              pe_work_priority_bucket(&config, &fragile, 10000u));
    }
}

/* The two rules together, on a queue that cannot be served by score alone.
   One fragile decision sits at the top bucket forever; two settled ones sit
   at the bottom. Serving one item per round, the settled ones can only be
   reached by the floor (once) or by aging. */
static void test_starvation(void)
{
    pe_work_priority_item_t items[3];
    pe_work_priority_config_t config;
    size_t order[3];
    uint64_t served[3];
    uint64_t epoch = 0u;
    size_t round, k;

    /* --- aging off: the floor serves the settled pair once, then they starve */
    items[0] = oracle_item(12.4, 6.1, 0.0, 0.0, 0u, 0u, 3u);
    items[1] = oracle_item(9.9, 4.4, 0.0, 0.0, 0u, 0u, 3u);
    items[2] = oracle_item(2.102, 2.101, 0.05, 0.0, 0u, 0u, 3u);
    memset(served, 0, sizeof(served));
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    epoch = 0u;
    for (round = 0u; round < 200u; ++round)
    {
        CHECK(pe_work_priority_order(&config, items, 3u, epoch, order, 3u, NULL) == 0,
              "order must succeed");
        for (k = 0u; k < 1u; ++k)
        {
            size_t idx = order[k];
            served[idx]++;
            items[idx].visits++;
            items[idx].last_served = epoch;
            epoch++;
        }
    }
    CHECK(served[0] == 1u,
          "the floor must serve the settled decision exactly once, got %llu",
          (unsigned long long)served[0]);
    CHECK(served[1] == 1u,
          "the floor must serve the settled decision exactly once, got %llu",
          (unsigned long long)served[1]);
    CHECK(served[2] == 198u,
          "the fragile decision takes every other round, got %llu",
          (unsigned long long)served[2]);
    printf("starvation, aging off: served = %llu, %llu, %llu\n",
           (unsigned long long)served[0], (unsigned long long)served[1],
           (unsigned long long)served[2]);

    /* --- aging on: the settled pair keeps coming back ------------------- */
    items[0] = oracle_item(12.4, 6.1, 0.0, 0.0, 0u, 0u, 3u);
    items[1] = oracle_item(9.9, 4.4, 0.0, 0.0, 0u, 0u, 3u);
    items[2] = oracle_item(2.102, 2.101, 0.05, 0.0, 0u, 0u, 3u);
    memset(served, 0, sizeof(served));
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 4u);
    epoch = 0u;
    for (round = 0u; round < 200u; ++round)
    {
        CHECK(pe_work_priority_order(&config, items, 3u, epoch, order, 3u, NULL) == 0,
              "order must succeed");
        {
            size_t idx = order[0];
            served[idx]++;
            items[idx].visits++;
            items[idx].last_served = epoch;
            epoch++;
        }
    }
    CHECK(served[0] > 1u,
          "aging must bring the settled decision back, got %llu",
          (unsigned long long)served[0]);
    CHECK(served[1] > 1u,
          "aging must bring the settled decision back, got %llu",
          (unsigned long long)served[1]);
    CHECK(served[0] + served[1] + served[2] == 200u,
          "every round must serve exactly one item");
    printf("starvation, aging on (interval 4): served = %llu, %llu, %llu\n",
           (unsigned long long)served[0], (unsigned long long)served[1],
           (unsigned long long)served[2]);

    /* FIFO is a fixed order, so a caller that always takes the head serves
       the same decision forever. That is not a coverage baseline - it is the
       reason the prioritised run is measured against a *rotating* FIFO queue
       in test_synthetic_workload() below. */
    items[0] = oracle_item(12.4, 6.1, 0.0, 0.0, 0u, 0u, 3u);
    items[1] = oracle_item(9.9, 4.4, 0.0, 0.0, 0u, 0u, 3u);
    items[2] = oracle_item(2.102, 2.101, 0.05, 0.0, 0u, 0u, 3u);
    memset(served, 0, sizeof(served));
    config = make_config(PE_WORK_SCHED_FIFO, 1u, 8u, 0u);
    epoch = 0u;
    for (round = 0u; round < 200u; ++round)
    {
        CHECK(pe_work_priority_order(&config, items, 3u, epoch, order, 3u, NULL) == 0,
              "order must succeed");
        {
            size_t idx = order[0];
            served[idx]++;
            items[idx].visits++;
            items[idx].last_served = epoch;
            epoch++;
        }
    }
    CHECK(served[0] == 200u && served[1] == 0u && served[2] == 0u,
          "FIFO is a fixed order and must take its head every time, got %llu, "
          "%llu, %llu",
          (unsigned long long)served[0], (unsigned long long)served[1],
          (unsigned long long)served[2]);
}

/* ---------------------------------------------------------------- *
 * 4. Determinism
 * ---------------------------------------------------------------- */

static void test_determinism(void)
{
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    pe_work_priority_item_t items[5];
    size_t first[5], second[5];
    size_t i;

    /* Four of the five share a bucket, so the input order has to decide. */
    for (i = 0u; i < 5u; ++i)
        items[i] = oracle_item(12.4, 6.1, 0.0, 0.0, 1u, 0u, 3u);
    items[3] = oracle_item(2.102, 2.101, 0.05, 0.0, 1u, 0u, 3u);

    CHECK(pe_work_priority_order(&config, items, 5u, 0u, first, 5u, NULL) == 0,
          "order must succeed");
    CHECK(pe_work_priority_order(&config, items, 5u, 0u, second, 5u, NULL) == 0,
          "order must succeed");
    CHECK(memcmp(first, second, sizeof(first)) == 0,
          "the same metadata must give the same order");
    CHECK(first[0] == 3u, "the fragile decision leads, got %zu", first[0]);
    CHECK(first[1] == 0u && first[2] == 1u && first[3] == 2u && first[4] == 4u,
          "equal buckets must keep the input order, got %zu %zu %zu %zu", first[1],
          first[2], first[3], first[4]);

    /* FIFO is the identity, whatever the scores are. */
    config = make_config(PE_WORK_SCHED_FIFO, 1u, 8u, 0u);
    CHECK(pe_work_priority_order(&config, items, 5u, 0u, first, 5u, NULL) == 0,
          "order must succeed");
    for (i = 0u; i < 5u; ++i)
        CHECK(first[i] == i, "FIFO must be the identity, got %zu at %zu", first[i],
              i);

    /* The output is always a permutation. */
    config = make_config(PE_WORK_SCHED_BALANCED, 2u, 6u, 3u);
    CHECK(pe_work_priority_order(&config, items, 5u, 40u, first, 5u, NULL) == 0,
          "order must succeed");
    {
        int seen[5];
        memset(seen, 0, sizeof(seen));
        for (i = 0u; i < 5u; ++i)
        {
            CHECK(first[i] < 5u, "index %zu out of range", first[i]);
            CHECK(!seen[first[i]], "index %zu repeated", first[i]);
            seen[first[i]] = 1;
        }
    }

    /* A capacity below the count, or a NULL buffer, is refused. */
    CHECK(pe_work_priority_order(&config, items, 5u, 0u, first, 4u, NULL) == -1,
          "a short capacity must be refused");
    CHECK(pe_work_priority_order(&config, items, 5u, 0u, NULL, 5u, NULL) == -1,
          "a NULL output must be refused");
    CHECK(pe_work_priority_order(NULL, items, 5u, 0u, first, 5u, NULL) == -1,
          "a NULL config must be refused");
    CHECK(pe_work_priority_order(&config, items, 0u, 0u, first, 0u, NULL) == 0,
          "an empty batch must succeed");
}

/* Codex P2 on PR #270: a one-observation accumulator has an *undefined*
   variance, not a zero one. Reading it as zero would drop a decision nobody
   has measured twice into the lowest bucket, and with aging off it could be
   starved there. The bridge must say "not enough observations", not "no
   spread". */
static void test_undersampled_bridge(void)
{
    pe_online_stats_t best, second;
    pe_work_priority_item_t item;
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    uint32_t top = config.buckets - 1u;

    pe_online_stats_reset(&best);
    pe_online_stats_reset(&second);
    pe_online_stats_add(&best, 2.102);
    pe_online_stats_add(&second, 2.097);
    CHECK(pe_online_stats_std_error(&best) >= 0.0 &&
              pe_online_stats_std_error(&best) <= 0.0,
          "the accumulator itself reports a zero spread below two samples");

    pe_work_priority_item_from_stats(&item, &best, &second, 3u, 1u, 0u);
    CHECK(isnan(item.best_stderr) && isnan(item.second_stderr),
          "the bridge must report an undefined spread as NaN, got %g and %g",
          item.best_stderr, item.second_stderr);
    CHECK(isinf(pe_work_priority_score(&config, &item)),
          "an undefined spread must score as unresolved, got %g",
          pe_work_priority_score(&config, &item));
    CHECK(pe_work_priority_bucket(&config, &item, 0u) == top,
          "an undefined spread must rank first, got bucket %u",
          pe_work_priority_bucket(&config, &item, 0u));

    /* A single action has nothing to decide, however thin the measurement. */
    pe_work_priority_item_from_stats(&item, &best, NULL, 1u, 0u, 0u);
    CHECK(pe_work_priority_score(&config, &item) >= 0.0 &&
              pe_work_priority_score(&config, &item) <= 0.0,
          "a single action must still score zero, got %g",
          pe_work_priority_score(&config, &item));

    /* Two observations are enough, and the score is a real number again. */
    pe_online_stats_add(&best, 2.202);
    pe_online_stats_add(&second, 2.000);
    pe_work_priority_item_from_stats(&item, &best, &second, 3u, 2u, 0u);
    CHECK(!isnan(item.best_stderr) && item.best_stderr > 0.0,
          "two observations must give a real spread, got %g", item.best_stderr);
    {
        double score = pe_work_priority_score(&config, &item);
        CHECK(!isnan(score) && !isinf(score) && score > 0.0,
              "a measured decision must score finitely, got %g", score);
        CHECK(pe_work_priority_bucket(&config, &item, 0u) < top,
              "a measured decision must not rank first, got bucket %u",
              pe_work_priority_bucket(&config, &item, 0u));
    }
}

/* Codex P2 on PR #270: a stateless interleave that always starts at the top
   bucket does nothing for a caller that services a prefix of the order and
   recomputes - the usual shape. The starting bucket must advance with the
   epoch, or BALANCED is the strict policy with a different within-round
   order. */
static void test_balanced_rotates(void)
{
    pe_work_priority_item_t items[8];
    pe_work_priority_config_t balanced =
        make_config(PE_WORK_SCHED_BALANCED, 1u, 8u, 0u); /* aging off */
    pe_work_priority_config_t strict =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    size_t order[8], strict_order[8];
    /* One decision per bucket: item i scores edges[i] and lands in bucket i.
       A gap of exactly 1 makes the score the spread itself. */
    static const double edges[8] = {0.0, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
    size_t i;
    uint64_t epoch;

    for (i = 0u; i < 8u; ++i)
    {
        items[i] =
            oracle_item(edges[i] + 1.0, edges[i], edges[i], 0.0, 5u, 0u, 2u);
        CHECK(pe_work_priority_bucket(&balanced, &items[i], 0u) == (uint32_t)i,
              "item %zu must land in bucket %zu, got %u", i, i,
              pe_work_priority_bucket(&balanced, &items[i], 0u));
    }

    printf("balanced rotation: heads");
    for (epoch = 0u; epoch < 8u; ++epoch)
    {
        CHECK(pe_work_priority_order(&balanced, items, 8u, epoch, order, 8u,
                                     NULL) == 0,
              "order must succeed");
        CHECK(pe_work_priority_order(&strict, items, 8u, epoch, strict_order, 8u,
                                     NULL) == 0,
              "order must succeed");
        printf(" %zu", order[0]);
        /* The round starts at the top bucket and walks down, so with every
           bucket occupied the head is exactly one bucket lower each epoch. */
        CHECK(order[0] == (size_t)(7u - (uint32_t)epoch),
              "epoch %llu: the head must be the decision in bucket %u, got %zu",
              (unsigned long long)epoch, 7u - (uint32_t)epoch, order[0]);
        CHECK(strict_order[0] == 7u,
              "the strict policy must keep the top bucket in front, got %zu",
              strict_order[0]);
        {
            int seen[8];
            memset(seen, 0, sizeof(seen));
            for (i = 0u; i < 8u; ++i)
            {
                CHECK(order[i] < 8u && !seen[order[i]],
                      "epoch %llu: position %zu is %zu, not a permutation",
                      (unsigned long long)epoch, i, order[i]);
                seen[order[i]] = 1;
            }
        }
    }
    printf("\n");
}

/* ---------------------------------------------------------------- *
 * 5. Against the brute-force oracle
 * ---------------------------------------------------------------- */

static void test_against_oracle(void)
{
    static const pe_work_scheduler_policy_t policies[3] = {
        PE_WORK_SCHED_FIFO, PE_WORK_SCHED_BALANCED,
        PE_WORK_SCHED_UNCERTAINTY_AWARE
    };
    static const double ratios[3] = {1.5, 2.0, 3.0};
    uint64_t rng = 20250816u;
    unsigned int trial;

    for (trial = 0u; trial < 4000u; ++trial)
    {
        pe_work_priority_item_t items[6];
        pe_work_priority_config_t config;
        oracle_ctx_t ctx;
        size_t order[6], expected[6];
        size_t count = 1u + (size_t)(rng_next(&rng) % 6u);
        size_t i;
        uint64_t epoch = rng_next(&rng) % 64u;
        int rc;

        for (i = 0u; i < count; ++i)
        {
            uint64_t r = rng_next(&rng);
            double best = (double)(r % 1000u) / 100.0;
            double gap = ((double)((r >> 16) % 40u) - 20.0) / 1000.0;
            double se_b = (double)((r >> 32) % 5u) / 100.0;
            double se_s = (double)((r >> 40) % 5u) / 100.0;
            items[i] = oracle_item(best, best - gap, se_b, se_s,
                                   r % 4u, rng_next(&rng) % 64u,
                                   2u + (uint32_t)((r >> 48) % 3u));
        }

        {
            pe_work_priority_config_t in;
            memset(&in, 0, sizeof(in));
            in.policy = policies[rng_next(&rng) % 3u];
            in.min_visits = 1u + (uint32_t)(rng_next(&rng) % 3u);
            in.buckets = 2u + (uint32_t)(rng_next(&rng) % 7u);
            in.bucket_ratio = ratios[rng_next(&rng) % 3u];
            in.aging_interval = rng_next(&rng) % 2u == 0u ? 0u : 1u + rng_next(&rng) % 8u;
            if (pe_work_priority_resolve(&in, &config) != 0)
            {
                CHECK(0, "trial %u: a generated config must resolve", trial);
                continue;
            }
        }

        fill_ctx(&ctx, &config, items, count, epoch);
        rc = pe_work_priority_order(&config, items, count, epoch, order, count,
                                    NULL);
        CHECK(rc == 0, "trial %u: order must succeed", trial);
        if (rc != 0)
            continue;

        if (config.policy == PE_WORK_SCHED_FIFO)
        {
            for (i = 0u; i < count; ++i)
                CHECK(order[i] == i, "trial %u: FIFO must be the identity", trial);
            continue;
        }

        if (config.policy == PE_WORK_SCHED_BALANCED)
        {
            CHECK(oracle_balanced_order(&ctx, expected) == 0,
                  "trial %u: the oracle must produce an order", trial);
        }
        else
        {
            CHECK(oracle_order_exhaustive(&ctx, expected) == 0,
                  "trial %u: the oracle must find a permutation", trial);
        }
        for (i = 0u; i < count; ++i)
            CHECK(order[i] == expected[i],
                  "trial %u (%s, %u buckets, ratio %g): position %zu is %zu, "
                  "the oracle says %zu",
                  trial, pe_work_scheduler_policy_name(config.policy),
                  config.buckets, config.bucket_ratio, i, order[i], expected[i]);
    }
}

/* ---------------------------------------------------------------- *
 * 6. Telemetry
 * ---------------------------------------------------------------- */

static void test_stats(void)
{
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 2u, 8u, 10u);
    pe_work_priority_item_t items[4];
    pe_work_priority_stats_t stats;
    size_t order[4];
    uint64_t depth_sum = 0u;
    uint32_t b;

    /* Epoch 100, aging interval 10. Item 0 has waited 25 epochs, which is
       two promotions; items 1 and 2 have waited 5, which is none. */
    items[0] = oracle_item(12.4, 6.1, 0.0, 0.0, 9u, 75u, 3u);    /* bucket 0 -> 2 */
    items[1] = oracle_item(2.102, 2.101, 0.05, 0.0, 9u, 95u, 3u); /* bucket 6 */
    items[2] = oracle_item(2.0, 2.0, 0.01, 0.01, 9u, 95u, 2u);    /* unresolved */
    items[3] = oracle_item(12.4, 6.1, 0.0, 0.0, 0u, 0u, 3u);      /* floor */

    CHECK(pe_work_priority_order(&config, items, 4u, 100u, order, 4u, &stats) == 0,
          "order must succeed");
    CHECK(stats.items == 4u, "items counted");
    for (b = 0u; b < config.buckets; ++b)
        depth_sum += stats.bucket_depth[b];
    CHECK(depth_sum == 4u, "the bucket histogram must cover every item, got %llu",
          (unsigned long long)depth_sum);
    CHECK(stats.bucket_depth[2] == 1u && stats.bucket_depth[6] == 1u &&
              stats.bucket_depth[7] == 2u,
          "the histogram is 1 in bucket 2, 1 in 6, 2 in 7, got %llu/%llu/%llu",
          (unsigned long long)stats.bucket_depth[2],
          (unsigned long long)stats.bucket_depth[6],
          (unsigned long long)stats.bucket_depth[7]);
    CHECK(stats.coverage_promotions == 1u,
          "one item is below the floor, got %llu",
          (unsigned long long)stats.coverage_promotions);
    CHECK(stats.aging_promotions == 1u,
          "one item aged into a higher bucket, got %llu",
          (unsigned long long)stats.aging_promotions);
    CHECK(stats.unresolved == 1u, "one ordering is unresolved, got %llu",
          (unsigned long long)stats.unresolved);
    CHECK(stats.score_count == 3u, "three scores are finite, got %llu",
          (unsigned long long)stats.score_count);
    CHECK(stats.delay_sum == 135u,
          "the delay is 25 + 5 + 5 + 100, got %llu",
          (unsigned long long)stats.delay_sum);
    CHECK(order[0] == 3u && order[1] == 2u && order[2] == 1u && order[3] == 0u,
          "the order is 3 (below the floor), then 2, 1, 0, got %zu %zu %zu %zu",
          order[0], order[1], order[2], order[3]);
    CHECK(pe_work_priority_below_floor(&config, &items[3]) == 1 &&
              pe_work_priority_below_floor(&config, &items[0]) == 0,
          "only the unserviced item is below the floor");

    /* The score histogram is the workload, not the policy: it counts each
       item once at its own score's bucket, before the floor and aging move
       anything. Item 0 scores 0 and is aged from bucket 0 to 2, so the two
       histograms must disagree exactly there - that is the difference the
       percentile is meant to describe. */
    {
        uint64_t score_total = 0u;
        for (b = 0u; b < config.buckets; ++b)
            score_total += stats.score_depth[b];
        CHECK(score_total == 4u,
              "the score histogram must cover every item, got %llu",
              (unsigned long long)score_total);
        CHECK(stats.score_depth[0] == 2u && stats.score_depth[6] == 1u &&
                  stats.score_depth[7] == 1u,
              "the score histogram is 2 in bucket 0, 1 in 6, 1 in 7, got "
              "%llu/%llu/%llu",
              (unsigned long long)stats.score_depth[0],
              (unsigned long long)stats.score_depth[6],
              (unsigned long long)stats.score_depth[7]);
        CHECK(stats.bucket_depth[0] == 0u && stats.score_depth[0] == 2u,
              "aging must move an item in bucket_depth and not in score_depth, "
              "got %llu and %llu",
              (unsigned long long)stats.bucket_depth[0],
              (unsigned long long)stats.score_depth[0]);
    }
    CHECK(pe_work_priority_percentile_bucket(&stats, config.buckets, 0.5) == 0u,
          "the median score bucket must be 0, got %u",
          pe_work_priority_percentile_bucket(&stats, config.buckets, 0.5));
    CHECK(pe_work_priority_percentile_bucket(&stats, config.buckets, 0.9) == 7u,
          "the ninetieth percentile must be the top bucket, got %u",
          pe_work_priority_percentile_bucket(&stats, config.buckets, 0.9));

    /* A NULL stats pointer is allowed. */
    CHECK(pe_work_priority_order(&config, items, 4u, 100u, order, 4u, NULL) == 0,
          "a NULL stats pointer must be allowed");

    /* The telemetry line is complete and comparable. */
    {
        char line[512];
        size_t needed = pe_work_priority_format_stats(&stats, config.buckets,
                                                      line, sizeof(line));
        CHECK(needed > 0u && needed < sizeof(line), "the line must fit, needs %zu",
              needed);
        CHECK(strstr(line, "work_priority items=4") != NULL,
              "the line must name the counters, got: %s", line);
        CHECK(strstr(line, "buckets=8") != NULL, "the line must carry buckets: %s",
              line);
        CHECK(strstr(line, "coverage_promotions=1") != NULL,
              "the line must carry the floor activations, got: %s", line);
        CHECK(strstr(line, "aging_promotions=1") != NULL,
              "the line must carry the aging activations, got: %s", line);
        CHECK(strstr(line, "unresolved=1") != NULL,
              "the line must carry the unresolved count, got: %s", line);
        CHECK(strstr(line, "depth=0,0,1,0,0,0,1,2") != NULL,
              "the line must carry the per-bucket depth, got: %s", line);
        CHECK(strstr(line, "p50_bucket=0") != NULL &&
                  strstr(line, "p90_bucket=7") != NULL,
              "the line must carry the score percentiles, got: %s", line);
        printf("%s\n", line);

        {
            char small[16];
            size_t want = pe_work_priority_format_stats(&stats, config.buckets,
                                                        small, sizeof(small));
            CHECK(want == needed,
                  "the needed length must not depend on the buffer, %zu vs %zu",
                  want, needed);
            CHECK(strlen(small) < sizeof(small),
                  "a short buffer must stay terminated");
        }
        CHECK(pe_work_priority_format_stats(NULL, 8u, line, sizeof(line)) == 0u,
              "a NULL stats must render nothing");
        {
            size_t empty = pe_work_priority_format_stats(&stats, 0u, line,
                                                         sizeof(line));
            CHECK(empty == strlen(line) && empty > 6u &&
                      strcmp(line + empty - 6u, "depth=") == 0,
                  "zero buckets must render an empty depth, got: %s", line);
        }
    }
}

/* ---------------------------------------------------------------- *
 * 7. Synthetic workload and the integration point
 * ---------------------------------------------------------------- */

#define WORKLOAD_ITEMS 40u
#define WORKLOAD_ROUNDS 2000u

static void build_workload(pe_work_priority_item_t *items)
{
    size_t i;
    for (i = 0u; i < WORKLOAD_ITEMS; ++i)
    {
        if (i % 4u == 3u)
        {
            /* Near-indifferent: a hair of a gap, a wide spread. */
            items[i] = oracle_item(2.102, 2.101, 0.05, 0.0, 0u, 0u, 3u);
        }
        else
        {
            /* Settled: a wide gap, a tight spread. */
            items[i] = oracle_item(10.0 + (double)i, 1.0, 0.01, 0.0, 0u, 0u, 3u);
        }
    }
}

/* Service one item per round and return the worst number of rounds between
   two consecutive services of the same item.

   The two disciplines are the two queues the layer is meant to feed, and
   they differ in exactly the way a FIFO queue differs from a priority queue:

     - `rotate` (FIFO): the order is the input order, and servicing the head
       puts it back at the tail, so a cursor walks the order and wraps;
     - otherwise: every round takes the head of a freshly computed order, so
       a decision stays in front until it stops scoring high or aging lifts
       the others over it. */
static int serve(const pe_work_priority_config_t *config,
                 pe_work_priority_item_t *items, size_t rounds, int rotate,
                 uint64_t *served, size_t *max_delay)
{
    size_t order[WORKLOAD_ITEMS];
    uint64_t epoch = 0u;
    uint64_t next_allowed[WORKLOAD_ITEMS];
    size_t cursor = 0u;
    size_t i, round;
    uint64_t worst = 0u;

    memset(served, 0, WORKLOAD_ITEMS * sizeof(uint64_t));
    for (i = 0u; i < WORKLOAD_ITEMS; ++i)
        next_allowed[i] = 0u;

    for (round = 0u; round < rounds; ++round)
    {
        size_t idx;
        if (pe_work_priority_order(config, items, WORKLOAD_ITEMS, epoch, order,
                                   WORKLOAD_ITEMS, NULL) != 0)
            return -1;
        if (rotate)
        {
            idx = order[cursor];
            cursor = (cursor + 1u) % WORKLOAD_ITEMS;
        }
        else
        {
            idx = order[0];
        }
        if (served[idx] > 0u)
        {
            uint64_t skipped = epoch - next_allowed[idx];
            if (skipped > worst)
                worst = skipped;
        }
        served[idx]++;
        next_allowed[idx] = epoch + 1u;
        items[idx].visits++;
        items[idx].last_served = epoch;
        epoch++;
    }
    for (i = 0u; i < WORKLOAD_ITEMS; ++i)
        if (served[i] == 0u)
            return -1;
    *max_delay = (size_t)worst;
    return 0;
}

/* The percentile is nearest rank over the score histogram: the smallest
   bucket whose cumulative count reaches ceil(p * total). That convention is
   the whole content of the function, so it is pinned on a distribution whose
   boundaries are exact - ten items, 3 in bucket 0, 4 in bucket 1, 3 in
   bucket 2 - rather than on a random one where an off-by-one would hide. */
static void test_percentile(void)
{
    pe_work_priority_stats_t stats;
    uint32_t buckets = 8u;

    memset(&stats, 0, sizeof(stats));
    CHECK(pe_work_priority_percentile_bucket(NULL, buckets, 0.5) == 0u,
          "a NULL stats must give bucket 0");
    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, 0.5) == 0u,
          "an empty histogram must give bucket 0");
    CHECK(pe_work_priority_percentile_bucket(&stats, 1u, 0.5) == 0u,
          "fewer than two buckets must give bucket 0");

    stats.score_depth[0] = 3u;
    stats.score_depth[1] = 4u;
    stats.score_depth[2] = 3u;
    stats.items = 10u;

    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, 0.0) == 0u,
          "the zeroth percentile is the lowest bucket, got %u",
          pe_work_priority_percentile_bucket(&stats, buckets, 0.0));
    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, 0.3) == 0u,
          "rank 3 is still the last of bucket 0, got %u",
          pe_work_priority_percentile_bucket(&stats, buckets, 0.3));
    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, 0.31) == 1u,
          "rank 4 crosses into bucket 1, got %u",
          pe_work_priority_percentile_bucket(&stats, buckets, 0.31));
    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, 0.5) == 1u,
          "the median is in bucket 1, got %u",
          pe_work_priority_percentile_bucket(&stats, buckets, 0.5));
    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, 0.7) == 1u,
          "rank 7 is the last of bucket 1, got %u",
          pe_work_priority_percentile_bucket(&stats, buckets, 0.7));
    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, 1.0) == 2u,
          "the hundredth percentile is the highest occupied bucket, got %u",
          pe_work_priority_percentile_bucket(&stats, buckets, 1.0));
    CHECK(pe_work_priority_percentile_bucket(&stats, buckets, -1.0) == 0u &&
              pe_work_priority_percentile_bucket(&stats, buckets, 9.0) == 2u,
          "out-of-range percentiles are clamped, got %u and %u",
          pe_work_priority_percentile_bucket(&stats, buckets, -1.0),
          pe_work_priority_percentile_bucket(&stats, buckets, 9.0));
    CHECK(pe_work_priority_percentile_bucket(&stats, 999u, 1.0) == 2u,
          "a bucket count above the maximum is clamped, got %u",
          pe_work_priority_percentile_bucket(&stats, 999u, 1.0));
}

static void test_synthetic_workload(void)
{
    pe_work_priority_item_t items[WORKLOAD_ITEMS];
    pe_work_priority_config_t config;
    uint64_t served[WORKLOAD_ITEMS];
    uint64_t near_fifo, near_ua;
    size_t i, max_delay_fifo = 0u, max_delay_ua = 0u;

    /* Baseline: the historical order, which spreads work evenly. */
    build_workload(items);
    config = make_config(PE_WORK_SCHED_FIFO, 1u, 8u, 0u);
    CHECK(serve(&config, items, WORKLOAD_ROUNDS, 1, served, &max_delay_fifo) == 0,
          "the FIFO run must service every decision");
    near_fifo = 0u;
    for (i = 0u; i < WORKLOAD_ITEMS; ++i)
        if (i % 4u == 3u)
            near_fifo += served[i];

    /* Uncertainty-aware, with aging so the settled majority still gets in. */
    build_workload(items);
    config = make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 8u);
    CHECK(serve(&config, items, WORKLOAD_ROUNDS, 0, served, &max_delay_ua) == 0,
          "the uncertainty-aware run must service every decision");
    near_ua = 0u;
    for (i = 0u; i < WORKLOAD_ITEMS; ++i)
        if (i % 4u == 3u)
            near_ua += served[i];

    printf("synthetic workload: near-indifferent share "
           "fifo %.1f%% -> uncertainty-aware %.1f%%\n",
           100.0 * (double)near_fifo / (double)WORKLOAD_ROUNDS,
           100.0 * (double)near_ua / (double)WORKLOAD_ROUNDS);
    printf("synthetic workload: worst delay between services "
           "fifo %zu -> uncertainty-aware %zu rounds\n",
           max_delay_fifo, max_delay_ua);

    CHECK(near_fifo > 0u, "FIFO must serve the near-indifferent group");
    CHECK(near_ua > 2u * near_fifo,
          "uncertainty-aware must at least double the near-indifferent share "
          "(%llu vs %llu)",
          (unsigned long long)near_ua, (unsigned long long)near_fifo);
    CHECK(max_delay_ua < WORKLOAD_ROUNDS,
          "aging must keep the worst delay finite, got %zu", max_delay_ua);

    /* BALANCED bounds the ratio between the busiest and the quietest bucket
       more tightly than the strict policy, at the cost of draining the top
       bucket less often. Both must still cover everything. */
    build_workload(items);
    config = make_config(PE_WORK_SCHED_BALANCED, 1u, 8u, 8u);
    CHECK(serve(&config, items, WORKLOAD_ROUNDS, 0, served, &max_delay_ua) == 0,
          "the balanced run must service every decision");
    printf("synthetic workload: balanced worst delay %zu rounds\n", max_delay_ua);
}

/* The order is meant to permute a pe_work_unit_t array before the
   coordinator dispatches it: this is the composition the guide documents. */
static void test_unit_permutation(void)
{
    enum { N = 4 };
    pe_work_unit_t units[N];
    const pe_work_unit_t *pointers[N];
    pe_work_priority_item_t items[N];
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    size_t order[N];
    size_t i;

    for (i = 0u; i < N; ++i)
    {
        pe_work_unit_init(&units[i]);
        units[i].public_state = (uint64_t)i * 1000u + 7u;
        units[i].player = (uint8_t)(i % 2u);
        units[i].iteration_begin = 10u;
        units[i].iteration_end = 11u;
        CHECK(pe_work_unit_validate(&units[i]) == 0,
              "unit %zu must validate once initialised", i);
        pointers[i] = &units[i];
    }

    /* Unit 2 is the near-indifferent decision. */
    items[0] = oracle_item(12.4, 6.1, 0.0, 0.0, 1u, 0u, 3u);
    items[1] = oracle_item(9.9, 4.4, 0.0, 0.0, 1u, 0u, 3u);
    items[2] = oracle_item(2.102, 2.101, 0.05, 0.0, 1u, 0u, 3u);
    items[3] = oracle_item(7.7, 3.3, 0.0, 0.0, 1u, 0u, 3u);

    CHECK(pe_work_priority_order(&config, items, N, 0u, order, N, NULL) == 0,
          "order must succeed");
    CHECK(order[0] == 2u, "the near-indifferent unit must be dispatched first");

    /* Applying the permutation to the pointers keeps every unit intact. */
    {
        const pe_work_unit_t *dispatched[N];
        for (i = 0u; i < N; ++i)
            dispatched[i] = pointers[order[i]];
        for (i = 0u; i < N; ++i)
        {
            CHECK(dispatched[i]->public_state ==
                      (uint64_t)order[i] * 1000u + 7u,
                  "the permutation must move whole units, got %llu",
                  (unsigned long long)dispatched[i]->public_state);
            CHECK(pe_work_unit_validate(dispatched[i]) == 0,
                  "a permuted unit must still validate");
        }
    }

    for (i = 0u; i < N; ++i)
        pe_work_unit_destroy(&units[i]);
}

/* ---------------------------------------------------------------- *
 * 8. The adaptive-variance bridge
 * ---------------------------------------------------------------- */

static void test_from_stats(void)
{
    pe_online_stats_t best, second;
    pe_work_priority_item_t item;
    pe_work_priority_config_t config =
        make_config(PE_WORK_SCHED_UNCERTAINTY_AWARE, 1u, 8u, 0u);
    uint32_t i;

    pe_online_stats_reset(&best);
    pe_online_stats_reset(&second);
    for (i = 0u; i < 16u; ++i)
    {
        pe_online_stats_add(&best, 2.102 + ((i % 2u) ? 0.05 : -0.05));
        pe_online_stats_add(&second, 2.101 + ((i % 2u) ? 0.05 : -0.05));
    }
    pe_work_priority_item_from_stats(&item, &best, &second, 3u, 4u, 9u);
    CHECK(item.actions == 3u && item.visits == 4u && item.last_served == 9u,
          "the bridge must carry the counters through");
    CHECK(fabs(item.best - best.mean) < 1e-15, "the bridge must copy the means");
    CHECK(item.best_stderr > 0.0 && item.second_stderr > 0.0,
          "the bridge must carry the standard errors");
    CHECK(pe_work_priority_score(&config, &item) > 1.0,
          "a near tie with a real spread must score above 1, got %g",
          pe_work_priority_score(&config, &item));

    /* A single action, and a missing runner-up: both are handled. */
    pe_work_priority_item_from_stats(&item, &best, NULL, 1u, 0u, 0u);
    CHECK(pe_work_priority_score(&config, &item) >= 0.0 &&
              pe_work_priority_score(&config, &item) <= 0.0,
          "a single action must score zero");
    pe_work_priority_item_from_stats(&item, &best, NULL, 3u, 0u, 0u);
    CHECK(isinf(pe_work_priority_score(&config, &item)),
          "no runner-up statistics must read as unresolved, got %g",
          pe_work_priority_score(&config, &item));
    pe_work_priority_item_from_stats(NULL, &best, &second, 3u, 0u, 0u);
}

/* ---------------------------------------------------------------- */

int main(void)
{
    test_score_fixtures();
    test_bucket_fixtures();
    test_bucket_boundaries();
    test_configuration();
    test_options();
    test_coverage_floor();
    test_aging();
    test_starvation();
    test_determinism();
    test_against_oracle();
    test_stats();
    test_percentile();
    test_synthetic_workload();
    test_unit_permutation();
    test_from_stats();
    test_undersampled_bridge();
    test_balanced_rotates();

    if (g_failures != 0)
    {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("test_work_priority: all checks passed\n");
    return 0;
}
