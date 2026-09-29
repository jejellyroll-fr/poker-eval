/*
 * test_br_priority.c - uncertainty-aware allocation of a sampled BR's sample
 * budget (issue #271).
 *
 * The priority layer orders a batch; this call site has no batch. What it has
 * is one decision at a time, and a confidence-guided resolver whose sample
 * cap already varies. So the layer is consumed as a *bucket*: a decision's
 * own previous measurement gives it a gap and a spread, the layer turns those
 * into a bucket, and the bucket interpolates the decision's cap between the
 * sampling config's minimum and its maximum.
 *
 * What that buys, and what it does not, is asserted here rather than assumed:
 *
 *  1. The default is inert. FIFO, and a zeroed config, leave the measurement
 *     byte for byte as it was, and report nothing.
 *  2. The cap never rises above the configured maximum: a prioritised run
 *     never samples more than the same run under FIFO. That is the invariant
 *     the whole design rests on - the policy reallocates downward.
 *  3. It is inert where it should be. A decision that resolves at its first
 *     look is untouched whatever its bucket, because the cap is only a cap.
 *  4. It bites where the decision does not resolve: a noisy decision spends
 *     strictly fewer draws, and the estimate stays on the exact value.
 *  5. The classification is what the layer promises. A decision with a wide
 *     gap and little noise lands in the lowest bucket; a near tie ranks
 *     higher on average over seeds (one snapshot of a tie is noisy).
 *     The comparison is across two games because an external game has a single
 *     root, so a run carries a single decision infoset.
 *  6. It is reproducible: a seeded run repeats exactly.
 *  7. Both actions' spreads are read: a decision whose only noise is on the
 *     leader, or only on the runner-up, is not mistaken for one with none.
 *  8. A game that cannot name its infosets is refused under the policy -
 *     there is no per-infoset record to allocate by - and still measured
 *     under FIFO, exactly as before.
 *
 * The measured numbers in the comments come from this machine, Debug, and are
 * what the assertions were written against.
 */

#include <poker_eval/solver/pe_external_best_response.h>

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
 * A one-decision toy game
 *
 * root: player 0 picks one of three actions (values base[0..2], each plus a
 * uniform noise of spread `noise[action]`), then a chance node draws the
 * noise.
 * Player 0's policy is uniform, so its best response is exactly base[0] when
 * that is the largest. One infoset (key 5), which is all the allocation
 * needs: the bucket is a per-decision property.
 * ---------------------------------------------------------------- */

#define T_ROOT 1u
#define T_CHANCE 10u /* 10 + action */
#define T_TERM 100u   /* 100 + action * 16 + outcome */

/* Per-game parameters, reached through the game's own `user` pointer so two
   games never share state. */
typedef struct {
    double base[3];
    double noise[3];
} toy_params_t;

static int t_terminal(const void *s, void *u)
{
    (void)u;
    return (uintptr_t)s >= T_TERM;
}
static int t_actor(const void *s, void *u)
{
    (void)u;
    return (uintptr_t)s == T_ROOT ? 0 : -1;
}
static uint16_t t_actions(const void *s, void *u)
{
    (void)u;
    return (uintptr_t)s == T_ROOT ? 3u : 0u;
}
static uint64_t t_key(const void *s, void *u)
{
    (void)s; (void)u;
    return 5u;
}
static const void *t_apply(const void *s, uint16_t a, void *u)
{
    (void)u;
    return (uintptr_t)s == T_ROOT && a < 3u
               ? (const void *)(uintptr_t)(T_CHANCE + a) : NULL;
}
static double t_prob(const void *s, uint64_t k, uint16_t a, void *u)
{
    (void)s; (void)k; (void)a; (void)u;
    return 1.0 / 3.0;
}
static double t_value(const void *s, int player, void *u)
{
    const toy_params_t *p = (const toy_params_t *)u;
    uintptr_t v = (uintptr_t)s - T_TERM;
    double value = p->base[v / 16u] + p->noise[v / 16u] * (((double)(v % 16u) - 7.5) / 7.5);
    return player == 0 ? value : -value;
}
static int t_sample_chance(const void *s, pe_rng_t *rng, pe_chance_sample_t *out)
{
    (void)s;
    out->outcome = (int)pe_rng_below(rng, 16u);
    out->importance_ratio = 1.0;
    return 0;
}
static const void *t_apply_chance(const void *s, int outcome, void *u)
{
    uintptr_t v = (uintptr_t)s;
    (void)u;
    if (v < T_CHANCE || v >= T_CHANCE + 3u || outcome < 0 || outcome >= 16)
        return NULL;
    return (const void *)(T_TERM + (v - T_CHANCE) * 16u + (uintptr_t)outcome);
}

static pe_external_game_t toy_game(toy_params_t *params, double b0, double b1,
                                   double noise)
{
    pe_external_game_t g;
    memset(&g, 0, sizeof(g));
    params->base[0] = b0;
    params->base[1] = b1;
    params->base[2] = 0.0;
    params->noise[0] = params->noise[1] = params->noise[2] = noise;
    g.root = (const void *)(uintptr_t)T_ROOT;
    g.user = params;
    g.player_count = 2u;
    g.is_terminal = t_terminal;
    g.acting_player = t_actor;
    g.action_count = t_actions;
    g.infoset_key = t_key;
    g.apply_action = t_apply;
    g.action_probability = t_prob;
    g.terminal_value = t_value;
    g.sample_chance = t_sample_chance;
    g.apply_chance = t_apply_chance;
    return g;
}

/* ---------------------------------------------------------------- *
 * Configurations
 * ---------------------------------------------------------------- */

#define TRAJECTORIES 400u
#define SEED 9u
#define MIN_SAMPLES 4u
#define MAX_SAMPLES 64u
#define CLASSIFY_SEEDS 16u

static pe_external_br_config_t make_config(void)
{
    pe_external_br_config_t c = pe_external_br_config_default();
    c.mode = PE_BR_SAMPLED;
    c.samples = TRAJECTORIES;
    c.seed = SEED;
    c.sampling.min_samples = MIN_SAMPLES;
    c.sampling.max_samples = MAX_SAMPLES;
    return c;
}

/* The bucket the single tracked infoset ended in, or -1 when none is
   occupied. */
static int occupied_bucket(const pe_work_priority_stats_t *stats,
                           uint32_t buckets)
{
    uint32_t b;
    int found = -1;
    for (b = 0u; b < buckets; ++b)
    {
        if (stats->bucket_depth[b] == 0u)
            continue;
        if (found >= 0)
            return -2; /* more than one: the caller asserts one infoset */
        found = (int)b;
    }
    return found;
}

/* ---------------------------------------------------------------- *
 * 1. The default is inert
 * ---------------------------------------------------------------- */

static void test_default_is_inert(void)
{
    toy_params_t params;
    pe_external_game_t game = toy_game(&params, 3.0, 0.0, 2.25);
    pe_external_br_config_t zeroed = make_config();
    pe_external_br_config_t fifo = make_config();
    pe_external_br_result_t r_zeroed, r_fifo;

    printf("  the default is inert\n");
    fifo.priority.policy = PE_WORK_SCHED_FIFO;

    CHECK(pe_external_best_response_sampled(&game, 0u, &zeroed, &r_zeroed) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &fifo, &r_fifo) == 0,
          "both measurements run");
    CHECK(r_zeroed.priority.items == 0u,
          "an untouched config tracks nothing (%llu)",
          (unsigned long long)r_zeroed.priority.items);
    CHECK(r_zeroed.sampling.samples == r_fifo.sampling.samples &&
              r_zeroed.sampling.decisions == r_fifo.sampling.decisions,
          "a zeroed config and an explicit FIFO measure the same "
          "(%llu vs %llu draws)",
          (unsigned long long)r_zeroed.sampling.samples,
          (unsigned long long)r_fifo.sampling.samples);
    /* The exact figure the historical path produces, so that a change to the
       default path cannot pass this test by moving both sides together. */
    CHECK(r_zeroed.sampling.samples == 15332u,
          "the historical path draws 15332 samples here (%llu)",
          (unsigned long long)r_zeroed.sampling.samples);
}

/* ---------------------------------------------------------------- *
 * 2-5. The cap: an invariant, an inert case, and a case that bites
 * ---------------------------------------------------------------- */

static void test_cap_is_a_cap(void)
{
    toy_params_t params;
    pe_external_game_t game = toy_game(&params, 10.0, 0.0, 0.1);
    pe_external_br_config_t fifo = make_config();
    pe_external_br_config_t prio = make_config();
    pe_external_br_result_t r_fifo, r_prio;

    printf("  a decision that resolves at its first look is untouched\n");
    prio.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    prio.priority.buckets = 8u;
    prio.priority.bucket_ratio = 2.0;

    CHECK(pe_external_best_response_sampled(&game, 0u, &fifo, &r_fifo) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &prio, &r_prio) == 0,
          "both measurements run");
    /* A gap of 10 against noise of 0.1: the leader clears the runner-up at
       the first look, so every decision stops there whatever its cap. The
       policy changes the cap and nothing else, so the totals match. */
    CHECK(r_prio.sampling.samples == r_fifo.sampling.samples,
          "the cap does not bind when the decision resolves at once "
          "(%llu vs %llu draws)",
          (unsigned long long)r_prio.sampling.samples,
          (unsigned long long)r_fifo.sampling.samples);
    CHECK(r_prio.priority.items == 1u,
          "the policy tracked the one infoset (%llu)",
          (unsigned long long)r_prio.priority.items);
    CHECK(occupied_bucket(&r_prio.priority, 8u) == 0,
          "a wide gap with little noise ranks in the lowest bucket (%d)",
          occupied_bucket(&r_prio.priority, 8u));
    printf("    draws: fifo=%llu prioritised=%llu (equal: the cap is inert "
           "here)\n",
           (unsigned long long)r_fifo.sampling.samples,
           (unsigned long long)r_prio.sampling.samples);
}

static void test_cap_bites(void)
{
    toy_params_t params;
    pe_external_game_t game = toy_game(&params, 3.0, 0.0, 2.25);
    pe_external_br_config_t fifo = make_config();
    pe_external_br_config_t prio = make_config();
    pe_external_br_result_t r_fifo, r_prio;

    printf("  a decision that does not resolve is capped down\n");
    prio.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    prio.priority.buckets = 8u;
    prio.priority.bucket_ratio = 2.0;

    CHECK(pe_external_best_response_sampled(&game, 0u, &fifo, &r_fifo) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &prio, &r_prio) == 0,
          "both measurements run");
    CHECK(r_prio.sampling.samples <= r_fifo.sampling.samples,
          "the cap is a cap: a prioritised run never outspends FIFO "
          "(%llu vs %llu draws)",
          (unsigned long long)r_prio.sampling.samples,
          (unsigned long long)r_fifo.sampling.samples);
    CHECK(r_prio.sampling.samples < r_fifo.sampling.samples,
          "here it bites: %.1f%% fewer draws (%llu vs %llu)",
          100.0 * (double)(r_fifo.sampling.samples - r_prio.sampling.samples) /
              (double)r_fifo.sampling.samples,
          (unsigned long long)r_prio.sampling.samples,
          (unsigned long long)r_fifo.sampling.samples);
    /* A cap is a budget, not a licence to stop estimating: the fresh-draw
       re-estimate still happens, so the value stays on the exact 3.0. */
    CHECK(fabs(r_prio.br_value - 3.0) < 0.1,
          "the estimate stays on the exact value under the policy (%.4f)",
          r_prio.br_value);
    /* The gap (3.0) is several standard errors of the difference wide even
       at min_samples, so the layer calls it settled - bucket 0 - although the
       resolver's own interval, z standard errors wide with z the sequential
       union bound, does not clear it at the first look. The two thresholds differ by z, and it is the layer's that
       allocates. */
    CHECK(occupied_bucket(&r_prio.priority, 8u) == 0,
          "a decision the layer calls settled is in bucket 0 (%d)",
          occupied_bucket(&r_prio.priority, 8u));
    printf("    draws: fifo=%llu prioritised=%llu\n",
           (unsigned long long)r_fifo.sampling.samples,
           (unsigned long long)r_prio.sampling.samples);
}

/* ---------------------------------------------------------------- *
 * 5. The classification is what the layer promises
 * ---------------------------------------------------------------- */

static void test_classification(void)
{
    pe_external_br_config_t prio = make_config();
    pe_external_br_result_t r_clear, r_tie, r_exact;
    toy_params_t p_clear, p_tie, p_exact;
    pe_external_game_t clear = toy_game(&p_clear, 10.0, 0.0, 0.1);
    pe_external_game_t tie = toy_game(&p_tie, 1.0, 0.999, 1.0);
    pe_external_game_t exact = toy_game(&p_exact, 1.0, 1.0, 1.0);
    int b_clear, b_tie, b_exact;
    int sum_clear = 0, sum_tie = 0, sum_exact = 0, runs_ok = 1;
    uint64_t seed;

    printf("  a near tie outranks a clearly separated decision\n");
    prio.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    prio.priority.buckets = 8u;
    prio.priority.bucket_ratio = 2.0;

    /* The bucket is read from the end-of-run snapshot, which is one draw of
       a noisy statistic: bucket 0 is a gap wider than one standard error of
       the difference, and a tie's measured gap exceeds that about a third of
       the time. So the ranking is asserted over several seeds, on the mean
       bucket, rather than on whichever snapshot one seed happens to give. */
    printf("    buckets (clear/near-tie/exact-tie):");
    for (seed = 1u; seed <= CLASSIFY_SEEDS; ++seed)
    {
        prio.seed = seed;
        if (pe_external_best_response_sampled(&clear, 0u, &prio, &r_clear) != 0 ||
            pe_external_best_response_sampled(&tie, 0u, &prio, &r_tie) != 0 ||
            pe_external_best_response_sampled(&exact, 0u, &prio, &r_exact) != 0)
        {
            runs_ok = 0;
            break;
        }
        b_clear = occupied_bucket(&r_clear.priority, 8u);
        b_tie = occupied_bucket(&r_tie.priority, 8u);
        b_exact = occupied_bucket(&r_exact.priority, 8u);
        CHECK(b_clear == 0, "the settled decision is in bucket 0 (seed %llu: %d)",
              (unsigned long long)seed, b_clear);
        CHECK(b_tie >= 0 && b_exact >= 0,
              "one tracked infoset per run (seed %llu)",
              (unsigned long long)seed);
        sum_clear += b_clear;
        sum_tie += b_tie;
        sum_exact += b_exact;
        printf(" %d/%d/%d", b_clear, b_tie, b_exact);
    }
    printf("\n");
    CHECK(runs_ok, "the measurements run");
    CHECK(sum_tie > sum_clear,
          "the near tie outranks it on average (%d against %d over %u seeds)",
          sum_tie, sum_clear, CLASSIFY_SEEDS);
    CHECK(sum_exact > sum_clear,
          "so does a decision with no gap at all (%d against %d over %u seeds)",
          sum_exact, sum_clear, CLASSIFY_SEEDS);
}

/* ---------------------------------------------------------------- *
 * The coverage floor and aging
 * ---------------------------------------------------------------- */

static void test_aging_promotes(void)
{
    toy_params_t params;
    pe_external_game_t game = toy_game(&params, 3.0, 0.0, 2.25);
    pe_external_br_config_t fifo = make_config();
    pe_external_br_config_t off = make_config();
    pe_external_br_config_t on = make_config();
    pe_external_br_result_t r_fifo, r_off, r_on;

    printf("  aging raises the cap of a decision that has waited\n");
    off.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    on.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    off.priority.buckets = on.priority.buckets = 8u;
    off.priority.bucket_ratio = on.priority.bucket_ratio = 2.0;
    /* One epoch of waiting is one bucket, and a decision infoset is served at
       most once per epoch, so its wait is one epoch. That is what makes the
       recorded `last_served` observable: without it the age would be the
       whole epoch counter and the promotion would saturate the bucket. */
    on.priority.aging_interval = 1u;

    CHECK(pe_external_best_response_sampled(&game, 0u, &fifo, &r_fifo) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &off, &r_off) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &on, &r_on) == 0,
          "the three measurements run");
    CHECK(r_on.sampling.samples > r_off.sampling.samples,
          "a promoted bucket buys a larger cap (%llu against %llu draws)",
          (unsigned long long)r_on.sampling.samples,
          (unsigned long long)r_off.sampling.samples);
    CHECK(r_on.sampling.samples <= r_fifo.sampling.samples,
          "and it is still a cap (%llu against FIFO's %llu)",
          (unsigned long long)r_on.sampling.samples,
          (unsigned long long)r_fifo.sampling.samples);
    /* The snapshot is taken right after the last service, so every record's
       age is zero and aging promotes nothing *there*. Aging acts on the cap
       during the traversal, which is the assertion above; the layer's
       counter is pinned here so that a reader does not mistake the snapshot
       for a record of the promotions. */
    CHECK(r_on.priority.aging_promotions == 0u,
          "the end-of-run snapshot shows no promotion to make (%llu)",
          (unsigned long long)r_on.priority.aging_promotions);
    printf("    draws: fifo=%llu aging-off=%llu aging-on=%llu\n",
           (unsigned long long)r_fifo.sampling.samples,
           (unsigned long long)r_off.sampling.samples,
           (unsigned long long)r_on.sampling.samples);
}

/* ---------------------------------------------------------------- *
 * 6. Reproducibility
 * ---------------------------------------------------------------- */

static void test_reproducible(void)
{
    toy_params_t params;
    pe_external_game_t game = toy_game(&params, 3.0, 0.0, 2.25);
    pe_external_br_config_t prio = make_config();
    pe_external_br_result_t first, again;

    printf("  a seeded prioritised run repeats exactly\n");
    prio.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    prio.priority.buckets = 8u;
    prio.priority.bucket_ratio = 2.0;

    CHECK(pe_external_best_response_sampled(&game, 0u, &prio, &first) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &prio, &again) == 0,
          "both measurements run");
    CHECK(first.sampling.samples == again.sampling.samples &&
              first.sampling.decisions == again.sampling.decisions &&
              first.sampling.terminal_evaluations ==
                  again.sampling.terminal_evaluations,
          "the draw counts repeat (%llu and %llu)",
          (unsigned long long)first.sampling.samples,
          (unsigned long long)again.sampling.samples);
    /* Bit for bit, not to a tolerance: this is a determinism claim, so a
       difference of one ulp is a failure. */
    CHECK(memcmp(&first.br_value, &again.br_value, sizeof(double)) == 0 &&
              memcmp(&first.br_gap, &again.br_gap, sizeof(double)) == 0,
          "the values repeat exactly (%.6f and %.6f)", first.br_value,
          again.br_value);
    CHECK(memcmp(first.priority.bucket_depth, again.priority.bucket_depth,
                 sizeof(first.priority.bucket_depth)) == 0 &&
              memcmp(&first.priority.score_sum, &again.priority.score_sum,
                     sizeof(double)) == 0 &&
              first.priority.score_count == again.priority.score_count,
          "the priority snapshot repeats");
}

/* ---------------------------------------------------------------- *
 * 7. Both standard errors reach the layer
 * ---------------------------------------------------------------- */

static void test_both_spreads(void)
{
    toy_params_t p_leader, p_runner;
    pe_external_game_t leader = toy_game(&p_leader, 10.0, 0.0, 0.0);
    pe_external_game_t runner = toy_game(&p_runner, 10.0, 0.0, 0.0);
    pe_external_br_config_t prio = make_config();
    pe_external_br_result_t r_leader, r_runner;
    int b_leader, b_runner;

    printf("  the leader's and the runner-up's spreads both reach the layer\n");
    /* Noise on one action only, and the third action far below so the
       runner-up is always action 1. A decision that reports no spread at all
       is scored against assumed_stderr; set absurdly wide, it sends such a
       decision to the top bucket. So each fixture lands in bucket 0 only if
       the one spread it has - the leader's, then the runner-up's - is the
       one the record kept. */
    p_leader.base[2] = p_runner.base[2] = -10.0;
    p_leader.noise[0] = 1.0;
    p_runner.noise[1] = 1.0;
    prio.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    prio.priority.buckets = 8u;
    prio.priority.bucket_ratio = 2.0;
    prio.priority.assumed_stderr = 1.0e6;

    CHECK(pe_external_best_response_sampled(&leader, 0u, &prio, &r_leader) == 0 &&
              pe_external_best_response_sampled(&runner, 0u, &prio, &r_runner) == 0,
          "both measurements run");
    b_leader = occupied_bucket(&r_leader.priority, 8u);
    b_runner = occupied_bucket(&r_runner.priority, 8u);
    CHECK(b_leader == 0,
          "noise on the leader only: its spread is scored (bucket %d)",
          b_leader);
    CHECK(b_runner == 0,
          "noise on the runner-up only: its spread is scored (bucket %d)",
          b_runner);
}

/* ---------------------------------------------------------------- *
 * 8. A game without infoset keys
 * ---------------------------------------------------------------- */

static void test_keyless_game(void)
{
    toy_params_t params;
    pe_external_game_t game = toy_game(&params, 3.0, 0.0, 2.25);
    pe_external_br_config_t fifo = make_config();
    pe_external_br_config_t prio = make_config();
    pe_external_br_result_t out;

    printf("  a keyless game is refused under the policy, measured under FIFO\n");
    game.infoset_key = NULL;
    prio.priority.policy = PE_WORK_SCHED_UNCERTAINTY_AWARE;
    CHECK(pe_external_best_response_sampled(&game, 0u, &fifo, &out) == 0,
          "FIFO measures a keyless game as before");
    CHECK(pe_external_best_response_sampled(&game, 0u, &prio, &out) != 0,
          "the policy refuses a keyless game instead of calling a NULL key");
}

int main(void)
{
    printf("test_br_priority (issue #271)\n");
    test_default_is_inert();
    test_cap_is_a_cap();
    test_cap_bites();
    test_classification();
    test_aging_promotes();
    test_reproducible();
    test_both_spreads();
    test_keyless_game();
    if (g_failures != 0)
    {
        fprintf(stderr, "%d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
