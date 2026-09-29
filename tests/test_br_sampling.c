/*
 * test_br_sampling.c - confidence-guided best-response decisions (issue #257).
 *
 *  1. The decision rule on synthetic, seeded samplers: a clearly positive and
 *     a clearly negative value against a fixed boundary, a value near the
 *     boundary, two-action winner and near tie, four actions with dominated
 *     ones eliminated, an ordering that flips after the first draws, the
 *     minimum and maximum budgets, the tolerance at its edges, zero and high
 *     variance, reproducibility, and the configuration.
 *  2. Sequential validity, measured: over thousands of close decisions, the
 *     share of wrong choices among resolved ones stays under
 *     1 - confidence, although every decision looked many times.
 *  3. The sampled best response on a toy game with a known exact value: the
 *     confidence-guided estimate lands on it, a large fixed budget agrees
 *     with it at a higher cost, and the historical one-rollout estimate is
 *     biased upward by taking the maximum of noisy draws.
 */

#include <poker_eval/solver/pe_br_sampling.h>
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
 * Synthetic samplers
 * ---------------------------------------------------------------- */

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

/* Standard normal by Box-Muller. */
static double rng_normal(uint64_t *s)
{
    double u = rng_unit(s), v = rng_unit(s);
    return sqrt(-2.0 * log(u)) * cos(6.283185307179586 * v);
}

typedef struct {
    uint64_t rng;
    double mean[8];
    double sd[8];
    uint64_t draws[8];
    /* Misleading start: this action's first `lure_draws` draws come out at
       `lure_value` before its true distribution takes over. */
    int lure_action;
    uint32_t lure_draws;
    double lure_value;
} sampler_t;

static double sample(void *user, uint16_t action)
{
    sampler_t *s = (sampler_t *)user;
    uint64_t n = s->draws[action]++;
    if ((int)action == s->lure_action && n < s->lure_draws)
        return s->lure_value;
    return s->mean[action] + s->sd[action] * rng_normal(&s->rng);
}

static sampler_t make_sampler(uint64_t seed)
{
    sampler_t s;
    memset(&s, 0, sizeof(s));
    s.rng = seed ? seed : 1u;
    s.lure_action = -1;
    return s;
}

static pe_br_sampling_config_t config(uint32_t min, uint32_t max,
                                      uint32_t interval, double conf,
                                      double abs_tol)
{
    pe_br_sampling_config_t c, r;
    memset(&c, 0, sizeof(c));
    c.min_samples = min;
    c.max_samples = max;
    c.check_interval = interval;
    c.confidence = conf;
    c.absolute_tolerance = abs_tol;
    if (pe_br_sampling_resolve(&c, &r) != 0)
    {
        fprintf(stderr, "bad test config\n");
        exit(2);
    }
    return r;
}

/* ---------------------------------------------------------------- *
 * 1. The decision rule
 * ---------------------------------------------------------------- */

static void test_decisions(void)
{
    pe_br_decision_t d;
    pe_br_sampling_stats_t stats;
    sampler_t s;
    const pe_br_sampling_config_t c = config(4u, 256u, 4u, 0.95, 0.0);

    printf("  decisions\n");

    /* A scalar against a boundary is a two-action decision where the second
       action is the boundary itself, drawn with no spread. */
    s = make_sampler(11u);
    s.mean[0] = 1.0; s.sd[0] = 0.3;   /* the value */
    s.mean[1] = 0.0; s.sd[1] = 0.0;   /* the boundary */
    pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
    CHECK(d.best == 0u && d.end == PE_BR_DECISION_SEPARATED && d.samples < 40u,
          "clearly positive: resolved above the boundary quickly (%llu draws)",
          (unsigned long long)d.samples);
    /* Issue #271: the two standard errors are reported apart and unscaled.
       They are not equal in general - here the boundary has none - and the
       half-width is the sequential z (the union bound over looks and
       actions, about 3.5 here, not 1.96) times their *sum*: it is neither a
       standard error nor their quadrature. */
    {
        const double z = pe_br_sequential_z(&c, 2u);
        CHECK(d.runner_stderr <= 0.0 && d.best_stderr > 0.0 && z > 3.0 &&
                  fabs(d.gap_half_width -
                       z * (d.best_stderr + d.runner_stderr)) <=
                      1e-12 * d.gap_half_width,
              "per-action standard errors (best %.4f, runner %.4f, z %.4f, "
              "half-width %.4f)", d.best_stderr, d.runner_stderr, z,
              d.gap_half_width);
    }

    s = make_sampler(12u);
    s.mean[0] = -1.0; s.sd[0] = 0.3;
    s.mean[1] = 0.0; s.sd[1] = 0.0;
    pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
    CHECK(d.best == 1u && d.end == PE_BR_DECISION_SEPARATED && d.samples < 40u,
          "clearly negative: resolved below the boundary quickly");

    s = make_sampler(13u);
    s.mean[0] = 0.001; s.sd[0] = 1.0;
    s.mean[1] = 0.0; s.sd[1] = 0.0;
    pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
    CHECK(d.end == PE_BR_DECISION_MAX_BUDGET && d.samples == 2u * 256u + 4u,
          "near the boundary with no tolerance: unresolved at the cap, 256 "
          "per action plus the 4-draw estimate (end %d, %llu draws)",
          (int)d.end, (unsigned long long)d.samples);

    /* Two-action near tie, with a tolerance that makes the gap irrelevant. */
    {
        const pe_br_sampling_config_t tol = config(4u, 256u, 4u, 0.95, 0.5);
        s = make_sampler(14u);
        s.mean[0] = 1.00; s.sd[0] = 0.2;
        s.mean[1] = 1.02; s.sd[1] = 0.2;
        pe_br_resolve_decision(2u, sample, &s, &tol, &d, NULL);
        CHECK(d.end == PE_BR_DECISION_TOLERANCE && d.samples < 512u,
              "near tie within tolerance: stops on the tolerance (end %d)",
              (int)d.end);
    }

    /* Four actions, one clear winner: the dominated ones are eliminated and
       stop being sampled, so the winner and the close runner-up get most of
       the draws. */
    s = make_sampler(15u);
    s.mean[0] = 0.0; s.sd[0] = 1.0;
    s.mean[1] = 2.5; s.sd[1] = 1.0;
    s.mean[2] = 3.0; s.sd[2] = 1.0;
    s.mean[3] = -2.0; s.sd[3] = 1.0;
    memset(&stats, 0, sizeof(stats));
    pe_br_resolve_decision(4u, sample, &s, &c, &d, &stats);
    CHECK(d.best == 2u, "four actions: the best one wins (chose %u)", d.best);
    CHECK(d.eliminated >= 2u && s.draws[3] < s.draws[2] &&
              s.draws[0] < s.draws[2],
          "dominated actions are dropped early (draws %llu %llu %llu %llu)",
          (unsigned long long)s.draws[0], (unsigned long long)s.draws[1],
          (unsigned long long)s.draws[2], (unsigned long long)s.draws[3]);
    CHECK(stats.decisions == 1u && stats.samples == d.samples,
          "the decision is counted in the totals");
    CHECK(stats.separated + stats.tolerance_stops + stats.max_budget_hits +
              stats.single_action == stats.decisions,
          "the end reasons add up to the decisions");

    /* An ordering that flips: action 0 opens with two draws at 3, which
       put it ahead after the first four, then settles at 0; action 1 sits
       at 1. The early lead is noisy, so it is not conclusive, and the
       decision keeps sampling until the order corrects itself. */
    s = make_sampler(16u);
    s.mean[0] = 0.0; s.sd[0] = 0.5;
    s.mean[1] = 1.0; s.sd[1] = 0.5;
    s.lure_action = 0; s.lure_draws = 2u; s.lure_value = 3.0;
    pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
    CHECK(d.best == 1u, "a lead built on early draws does not survive "
          "(chose %u)", d.best);

    /* Minimum budget: zero variance and a clear winner resolve at the first
       look, after exactly min_samples each. */
    s = make_sampler(17u);
    s.mean[0] = 1.0; s.mean[1] = 0.0;
    pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
    CHECK(d.end == PE_BR_DECISION_SEPARATED && s.draws[0] == 4u + 4u &&
              s.draws[1] == 4u,
          "zero variance: resolved after the minimum, then the 4-draw "
          "estimate of the winner (%llu, %llu draws)",
          (unsigned long long)s.draws[0], (unsigned long long)s.draws[1]);

    /* Maximum budget: two identical actions never separate; each gets the
       cap and not one draw more. */
    s = make_sampler(18u);
    s.mean[0] = 0.5; s.sd[0] = 1.0;
    s.mean[1] = 0.5; s.sd[1] = 1.0;
    pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
    CHECK(d.end == PE_BR_DECISION_MAX_BUDGET &&
              s.draws[d.best] == 256u + 4u && s.draws[1u - d.best] == 256u,
          "identical actions: both at the cap, and not one draw more than "
          "the chosen one's estimate (%llu, %llu)",
          (unsigned long long)s.draws[0], (unsigned long long)s.draws[1]);

    /* Tolerance edges with zero variance: an exact tie meets any positive
       tolerance, and is unresolved without one. */
    {
        const pe_br_sampling_config_t tol = config(4u, 16u, 4u, 0.95, 1e-9);
        s = make_sampler(19u);
        s.mean[0] = 2.0; s.mean[1] = 2.0;
        pe_br_resolve_decision(2u, sample, &s, &tol, &d, NULL);
        CHECK(d.end == PE_BR_DECISION_TOLERANCE, "exact tie, tiny tolerance");
        s = make_sampler(19u);
        s.mean[0] = 2.0; s.mean[1] = 2.0;
        {
            const pe_br_sampling_config_t none = config(4u, 16u, 4u, 0.95, 0.0);
            pe_br_resolve_decision(2u, sample, &s, &none, &d, NULL);
        }
        CHECK(d.end == PE_BR_DECISION_MAX_BUDGET,
              "exact tie, no tolerance: unresolved");
    }

    /* High variance: the right answer still, at a higher price. */
    s = make_sampler(20u);
    s.mean[0] = 0.0; s.sd[0] = 20.0;
    s.mean[1] = 10.0; s.sd[1] = 20.0;
    pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
    CHECK(d.best == 1u && d.samples > 40u,
          "high variance: still resolved, with more draws (%llu)",
          (unsigned long long)d.samples);

    /* One action: nothing to decide. It is counted as a single-action
       decision, not as a stop, so the end reasons still add up; and its
       running mean is the plain mean, since nothing was selected. */
    memset(&stats, 0, sizeof(stats));
    s = make_sampler(22u);
    s.mean[0] = 1.0; s.sd[0] = 0.5;
    CHECK(pe_br_resolve_decision(1u, sample, &s, &c, &d, &stats) == 0 &&
              d.end == PE_BR_DECISION_SINGLE && d.samples == 4u &&
              fabs(d.value - d.selection_value) <= 0.0 &&
              stats.decisions == 1u && stats.single_action == 1u &&
              stats.separated == 0u && stats.tolerance_stops == 0u &&
              stats.max_budget_hits == 0u,
          "one action: a single-action decision, not a stop (%llu draws, "
          "end %d)", (unsigned long long)d.samples, (int)d.end);

    /* Reproducible: the same seed gives the same decision. */
    {
        pe_br_decision_t a, b;
        sampler_t x = make_sampler(21u), y = make_sampler(21u);
        x.mean[1] = y.mean[1] = 0.3;
        x.sd[0] = y.sd[0] = x.sd[1] = y.sd[1] = 1.0;
        pe_br_resolve_decision(2u, sample, &x, &c, &a, NULL);
        pe_br_resolve_decision(2u, sample, &y, &c, &b, NULL);
        CHECK(a.best == b.best && a.samples == b.samples &&
                  memcmp(&a.value, &b.value, sizeof(a.value)) == 0,
              "same seed, same decision");
    }
}

static void test_configuration(void)
{
    pe_br_sampling_config_t in, out;
    printf("  configuration\n");
    memset(&in, 0, sizeof(in));
    CHECK(pe_br_sampling_resolve(&in, &out) == 0 && !pe_br_sampling_enabled(&out),
          "all-zero is off");
    in.max_samples = 64u;
    CHECK(pe_br_sampling_resolve(&in, &out) == 0 && out.min_samples == 4u &&
              out.check_interval == 4u && out.confidence > 0.9499 &&
              out.confidence < 0.9501,
          "defaults");
    in.min_samples = 1u;
    CHECK(pe_br_sampling_resolve(&in, &out) == 0 && out.min_samples == 2u,
          "a minimum of one becomes two");
    in.min_samples = 100u;
    CHECK(pe_br_sampling_resolve(&in, &out) != 0, "min above max");
    in.min_samples = 0u;
    in.confidence = 1.0;
    CHECK(pe_br_sampling_resolve(&in, &out) != 0, "confidence 1");
    /* Below 1, but its union-bound level rounds to 1: refused up front
       rather than failing every decision. */
    in.confidence = 0.9999999999999999;
    CHECK(pe_br_sampling_resolve(&in, &out) != 0,
          "a confidence whose adjusted level rounds to 1");
    in.confidence = 0.999999;
    CHECK(pe_br_sampling_resolve(&in, &out) == 0 &&
              pe_br_sequential_z(&out, 64u) < HUGE_VAL,
          "a high but representable confidence is kept");
    in.confidence = 0.0;
    in.absolute_tolerance = -1.0;
    CHECK(pe_br_sampling_resolve(&in, &out) != 0, "negative tolerance");

    memset(&in, 0, sizeof(in));
    CHECK(pe_br_sampling_parse_option(&in, "max-samples", "32") == 0 &&
              pe_br_sampling_parse_option(&in, "confidence", "0.9") == 0 &&
              pe_br_sampling_parse_option(&in, "relative-tolerance", "0.1") == 0 &&
              in.max_samples == 32u,
          "options parse");
    CHECK(pe_br_sampling_parse_option(&in, "samples", "3") != 0 &&
              pe_br_sampling_parse_option(&in, "max-samples", "x") != 0,
          "bad options are refused");

    /* The sequential z exceeds the one-look z and grows with looks and
       actions. */
    {
        pe_br_sampling_config_t few = config(4u, 8u, 4u, 0.95, 0.0);
        pe_br_sampling_config_t many = config(4u, 1024u, 4u, 0.95, 0.0);
        double z1 = pe_confidence_z(0.95);
        double zf = pe_br_sequential_z(&few, 2u);
        double zm = pe_br_sequential_z(&many, 2u);
        double z4 = pe_br_sequential_z(&many, 4u);
        CHECK(zf > z1 && zm > zf && z4 > zm,
              "union-bound z grows with looks and actions (%.3f < %.3f < "
              "%.3f < %.3f)", z1, zf, zm, z4);
    }
}

/* The external evaluator's widest decision, and a check interval far above
   the budget. */
typedef struct {
    uint64_t rng;
    uint64_t calls;
} wide_sampler_t;

static double wide_sample(void *user, uint16_t action)
{
    wide_sampler_t *w = (wide_sampler_t *)user;
    w->calls++;
    return (action == PE_EXTERNAL_MAX_ACTIONS - 1u ? 10.0 : 0.0) +
           rng_normal(&w->rng);
}

static void test_limits(void)
{
    pe_br_decision_t d;
    wide_sampler_t w;
    printf("  limits\n");

    /* Every action count the external evaluator accepts is decidable. */
    {
        const pe_br_sampling_config_t c = config(4u, 64u, 4u, 0.95, 0.0);
        w.rng = 30u;
        w.calls = 0u;
        CHECK(pe_br_resolve_decision((uint16_t)PE_EXTERNAL_MAX_ACTIONS,
                                     wide_sample, &w, &c, &d, NULL) == 0 &&
                  d.best == PE_EXTERNAL_MAX_ACTIONS - 1u,
              "%u actions: decided (best %u)",
              (unsigned)PE_EXTERNAL_MAX_ACTIONS, (unsigned)d.best);
        CHECK(pe_br_resolve_decision((uint16_t)(PE_BR_SAMPLING_MAX_ACTIONS + 1u),
                                     wide_sample, &w, &c, &d, NULL) != 0,
              "above the limit: refused");
    }

    /* A huge check interval is bounded by the remaining budget: the round
       makes the few draws left rather than billions of empty passes. */
    {
        pe_br_sampling_config_t in, c;
        memset(&in, 0, sizeof(in));
        in.min_samples = 4u;
        in.max_samples = 8u;
        in.check_interval = UINT32_MAX;
        CHECK(pe_br_sampling_resolve(&in, &c) == 0, "huge interval resolves");
        w.rng = 31u;
        w.calls = 0u;
        CHECK(pe_br_resolve_decision(2u, wide_sample, &w, &c, &d, NULL) == 0 &&
                  w.calls <= 2u * 8u + 4u && w.calls == d.samples,
              "huge interval: at most the hard bound (%llu draws)",
              (unsigned long long)w.calls);
    }
}

/* ---------------------------------------------------------------- *
 * 2. Sequential validity, measured
 * ---------------------------------------------------------------- */

static void test_error_rate(void)
{
    const pe_br_sampling_config_t c = config(4u, 1024u, 8u, 0.90, 0.0);
    const int trials = 4000;
    int resolved = 0, wrong = 0;

    printf("  error rate over close decisions\n");
    for (int t = 0; t < trials; ++t)
    {
        pe_br_decision_t d;
        sampler_t s = make_sampler(1000u + (uint64_t)t);
        /* Action 1 is better by 0.3 standard deviation. */
        s.mean[0] = 0.0; s.sd[0] = 1.0;
        s.mean[1] = 0.3; s.sd[1] = 1.0;
        pe_br_resolve_decision(2u, sample, &s, &c, &d, NULL);
        if (d.end == PE_BR_DECISION_SEPARATED)
        {
            resolved++;
            if (d.best != 1u)
                wrong++;
        }
    }
    CHECK(resolved > trials / 4, "enough decisions resolved (%d)", resolved);
    CHECK(wrong <= trials / 10,
          "wrong resolved decisions %d of %d trials: at most 10%% allowed by "
          "the 90%% confidence", wrong, trials);
    printf("    %d of %d close decisions resolved, %d wrong (bound %d)\n",
           resolved, trials, wrong, trials / 10);
}

/* ---------------------------------------------------------------- *
 * 3. The sampled best response on a toy game
 * ---------------------------------------------------------------- */

/* root: player 0 picks one of three actions (values 1.0, 0.3, 0.0, each
   plus uniform noise of spread `g_noise`), then a chance node draws the
   noise. Player 0's policy is uniform. Its best response is exactly 1.0. */
#define T_ROOT 1u
#define T_CHANCE 10u   /* 10 + action */
#define T_TERM 100u    /* 100 + action * 16 + outcome */
static const double k_base[3] = {1.0, 0.3, 0.0};
static double g_noise = 1.0;

static int t_terminal(const void *s, void *u) { (void)u; return (uintptr_t)s >= T_TERM; }
static int t_actor(const void *s, void *u) { (void)u; return (uintptr_t)s == T_ROOT ? 0 : -1; }
static uint16_t t_actions(const void *s, void *u) { (void)u; return (uintptr_t)s == T_ROOT ? 3u : 0u; }
static uint64_t t_key(const void *s, void *u) { (void)s; (void)u; return 5u; }
static const void *t_apply(const void *s, uint16_t a, void *u)
{
    (void)u;
    return (uintptr_t)s == T_ROOT && a < 3u ? (const void *)(uintptr_t)(T_CHANCE + a) : NULL;
}
static double t_prob(const void *s, uint64_t k, uint16_t a, void *u)
{
    (void)s; (void)k; (void)a; (void)u;
    return 1.0 / 3.0;
}
static double t_value(const void *s, int player, void *u)
{
    uintptr_t v = (uintptr_t)s - T_TERM;
    double value = k_base[v / 16u] + g_noise * (((double)(v % 16u) - 7.5) / 7.5);
    (void)u;
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

static pe_external_game_t toy_game(void)
{
    pe_external_game_t g;
    memset(&g, 0, sizeof(g));
    g.root = (const void *)(uintptr_t)T_ROOT;
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

static void test_best_response(void)
{
    pe_external_game_t game = toy_game();
    pe_external_br_config_t legacy = pe_external_br_config_default();
    pe_external_br_config_t fixed = legacy, adaptive = legacy;
    pe_external_br_result_t r_legacy, r_fixed, r_adaptive;

    printf("  sampled best response on a toy game (exact value 1.0)\n");
    legacy.mode = fixed.mode = adaptive.mode = PE_BR_SAMPLED;
    legacy.samples = fixed.samples = adaptive.samples = 400u;
    legacy.seed = fixed.seed = adaptive.seed = 9u;
    /* A large fixed budget: 64 rollouts of every action, every time. */
    fixed.sampling.min_samples = 64u;
    fixed.sampling.max_samples = 64u;
    /* Confidence-guided, same cap: the 0.7 gap resolves long before it. */
    adaptive.sampling.min_samples = 4u;
    adaptive.sampling.max_samples = 64u;
    adaptive.sampling.absolute_tolerance = 0.25;

    CHECK(pe_external_best_response_sampled(&game, 0u, &legacy, &r_legacy) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &fixed, &r_fixed) == 0 &&
              pe_external_best_response_sampled(&game, 0u, &adaptive, &r_adaptive) == 0,
          "the three estimates run");

    /* The fixed estimate averages 64 draws of noise +-1 (sd ~0.6): one
       trajectory's value has sd ~0.07, 400 of them ~0.004. Allow 0.05. */
    CHECK(fabs(r_fixed.br_value - 1.0) < 0.05,
          "large fixed budget: %.4f, exact 1.0", r_fixed.br_value);
    /* Estimated from fresh draws after the decision, so neither the early
       stop nor the choice biases it: as close as the fixed budget. */
    CHECK(fabs(r_adaptive.br_value - 1.0) < 0.05,
          "confidence-guided: %.4f, exact 1.0", r_adaptive.br_value);
    CHECK(r_adaptive.sampling.terminal_evaluations <
              r_fixed.sampling.terminal_evaluations / 2u,
          "confidence-guided spends less than half the fixed budget "
          "(%llu vs %llu terminal evaluations)",
          (unsigned long long)r_adaptive.sampling.terminal_evaluations,
          (unsigned long long)r_fixed.sampling.terminal_evaluations);
    /* One rollout per action, the maximum taken: biased well above 1. */
    CHECK(r_legacy.br_value > 1.15,
          "the one-rollout estimate is biased upward by the maximum (%.4f)",
          r_legacy.br_value);
    CHECK(r_legacy.sampling.decisions == 0u && r_fixed.sampling.decisions == 400u,
          "the adaptive totals are only kept when the evaluation is on");
    /* The running mean the decision stopped on is the biased estimate the
       guide's table quotes: stopping when the leader happens to look good
       favours a high mean. The re-estimate above is the unbiased one, so it
       must sit closer to the exact 1.0. Asserted, not just described. */
    {
        double running =
            r_adaptive.sampling.selection_value_sum /
            (double)r_adaptive.sampling.decisions;
        CHECK(running > 1.02,
              "the running mean the decisions stopped on is biased upward "
              "(%.4f against the exact 1.0)", running);
        CHECK(fabs(r_adaptive.br_value - 1.0) < fabs(running - 1.0),
              "the re-estimate sits closer to the exact 1.0 than the running "
              "mean it replaced (%.4f against %.4f away)",
              fabs(r_adaptive.br_value - 1.0), fabs(running - 1.0));
        printf("    running mean of the chosen action %.4f, re-estimated "
               "%.4f\n", running, r_adaptive.br_value);
    }
    printf("    BR value: one rollout %.3f, fixed 64 %.3f (%llu evals), "
           "confidence-guided %.3f (%llu evals)\n",
           r_legacy.br_value, r_fixed.br_value,
           (unsigned long long)r_fixed.sampling.terminal_evaluations,
           r_adaptive.br_value,
           (unsigned long long)r_adaptive.sampling.terminal_evaluations);

    /* Reproducible under the same seed. */
    {
        pe_external_br_result_t again;
        pe_external_best_response_sampled(&game, 0u, &adaptive, &again);
        CHECK(memcmp(&again.br_value, &r_adaptive.br_value, sizeof(double)) == 0 &&
                  again.sampling.terminal_evaluations ==
                      r_adaptive.sampling.terminal_evaluations,
              "same seed, same measurement");
    }

    /* Invalid settings are refused. */
    {
        pe_external_br_config_t bad = adaptive;
        bad.sampling.confidence = 2.0;
        CHECK(pe_external_best_response_sampled(&game, 0u, &bad, &r_fixed) != 0,
              "a confidence of 2 is refused");
    }
}

int main(void)
{
    printf("test_br_sampling: confidence-guided best-response decisions\n");
    test_decisions();
    test_configuration();
    test_limits();
    test_error_rate();
    test_best_response();
    if (g_failures)
    {
        fprintf(stderr, "test_br_sampling: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_br_sampling: all checks passed\n");
    return 0;
}
