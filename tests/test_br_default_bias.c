/*
 * test_br_default_bias.c - the default sampled best response is debiased.
 * Issue #274.
 *
 * The historical sampled best response, at every best-responder decision,
 * played one rollout per action and took the maximum. The maximum of noisy
 * draws overshoots the true value, and averaging more trajectories estimates
 * the same overshoot, so the reported exploitability has a floor that no
 * --br-samples value removes. The confidence-guided evaluation (issue #257)
 * is therefore the default configuration; max_samples == 0 is the explicit
 * opt-out.
 *
 * Fixture: Kuhn poker (J<Q<K, ante 1, one bet of 1) through the Lane B
 * external-game callbacks, both players uniformly random. The exact best
 * response is computed by the deterministic traversal and is hand-derivable:
 *   policy value (player 0) = +1/8
 *   BR value player 0       = +1/2   ->   br gap = 3/8
 *   BR value player 1       = +5/12  ->   br gap = 13/24
 *
 * The test pins three things:
 *   1. the default configuration enables the confidence-guided evaluation;
 *   2. the historical estimator overstates the exact gap, and keeps
 *      overstating however many trajectories it is given;
 *   3. the default estimator tracks the exact gap within a bounded margin,
 *      and max_samples == 0 still selects the historical estimator.
 */

#include <poker_eval/solver/pe_external_best_response.h>
#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Kuhn poker external adapter (same shape as test_external_br_exact.c)
 * ------------------------------------------------------------------ */

/* deal d in 0..5 -> (card0, card1), cards 0=J 1=Q 2=K */
static const int kuhn_deals[6][2] = {
    {0, 1}, {0, 2}, {1, 0}, {1, 2}, {2, 0}, {2, 1}
};

/* State handles are one-based because NULL is reserved for an invalid child. */
#define KUHN_NODE_DEAL(state) (((int)(uintptr_t)(state) - 1) / 100)
#define KUHN_NODE_ID(state)   (((int)(uintptr_t)(state) - 1) % 100)

static int kuhn_terminal(const void *state, void *user)
{
    int node = KUHN_NODE_ID(state);
    (void)user;
    return node == 3 || node == 5 || node == 6 || node == 7 || node == 8;
}

static int kuhn_acting(const void *state, void *user)
{
    int node = KUHN_NODE_ID(state);
    (void)user;
    if (node == 0 || node == 4) return 0;
    if (node == 1 || node == 2) return 1;
    return -1;
}

static uint16_t kuhn_actions(const void *state, void *user)
{
    int node = KUHN_NODE_ID(state);
    (void)user;
    return (node == 0 || node == 1 || node == 2 || node == 4) ? 2u : 0u;
}

static uint64_t kuhn_key(const void *state, void *user)
{
    int deal = KUHN_NODE_DEAL(state);
    int node = KUHN_NODE_ID(state);
    (void)user;
    if (node == 0) return (uint64_t)(kuhn_deals[deal][0] * 16 + 0);
    if (node == 1) return (uint64_t)(kuhn_deals[deal][1] * 16 + 1);
    if (node == 2) return (uint64_t)(kuhn_deals[deal][1] * 16 + 2);
    if (node == 4) return (uint64_t)(kuhn_deals[deal][0] * 16 + 3);
    return 0u;
}

static const void *kuhn_apply_action(const void *state, uint16_t action,
                                     void *user)
{
    int deal = KUHN_NODE_DEAL(state);
    int node = KUHN_NODE_ID(state);
    (void)user;
    if (node == 0)
        return (const void *)(uintptr_t)(deal * 100 + (action ? 2 : 1) + 1);
    if (node == 1)
        return (const void *)(uintptr_t)(deal * 100 + (action ? 4 : 3) + 1);
    if (node == 2)
        return (const void *)(uintptr_t)(deal * 100 + (action ? 6 : 5) + 1);
    if (node == 4)
        return (const void *)(uintptr_t)(deal * 100 + (action ? 8 : 7) + 1);
    return NULL;
}

static double kuhn_probability(const void *state, uint64_t infoset,
                               uint16_t action, void *user)
{
    (void)state; (void)infoset; (void)action; (void)user;
    return 0.5; /* both players uniformly random */
}

static double kuhn_utility(const void *state, int player, void *user)
{
    int deal = KUHN_NODE_DEAL(state);
    int node = KUHN_NODE_ID(state);
    int c0 = kuhn_deals[deal][0], c1 = kuhn_deals[deal][1];
    int p0_wins = c0 > c1;
    double p0;
    (void)user;
    switch (node)
    {
    case 3: p0 = p0_wins ? 1.0 : -1.0; break; /* both passed  */
    case 5: p0 = 1.0; break;                  /* p1 folded    */
    case 6: p0 = p0_wins ? 2.0 : -2.0; break; /* bet called   */
    case 7: p0 = -1.0; break;                 /* p0 folded    */
    case 8: p0 = p0_wins ? 2.0 : -2.0; break; /* bet called   */
    default: return 0.0;
    }
    return player == 0 ? p0 : -p0;
}

static int kuhn_sample_chance(const void *state, pe_rng_t *rng,
                              pe_chance_sample_t *out, void *user)
{
    (void)user;
    if (!rng || !out) return -1;
    if (KUHN_NODE_ID(state) != 9) return 1;
    out->outcome = (int)pe_rng_below(rng, 6u);
    out->importance_ratio = 1.0;
    return 0;
}

static const void *kuhn_apply_chance(const void *state, int outcome,
                                     void *user)
{
    (void)state; (void)user;
    if (outcome < 0 || outcome > 5) return NULL;
    return (const void *)(uintptr_t)(outcome * 100 + 1);
}

static uint32_t kuhn_chance_outcomes(const void *state, void *user)
{
    (void)state; (void)user;
    return 6u;
}

static void kuhn_game(pe_external_game_t *game)
{
    memset(game, 0, sizeof(*game));
    game->root = (const void *)(uintptr_t)10u; /* chance root */
    game->player_count = 2u;
    game->is_terminal = kuhn_terminal;
    game->acting_player = kuhn_acting;
    game->action_count = kuhn_actions;
    game->infoset_key = kuhn_key;
    game->apply_action = kuhn_apply_action;
    game->action_probability = kuhn_probability;
    game->terminal_value = kuhn_utility;
    game->sample_chance_with_user = kuhn_sample_chance;
    game->apply_chance = kuhn_apply_chance;
    game->chance_outcome_count = kuhn_chance_outcomes;
}

/* ------------------------------------------------------------------ *
 * Checks
 * ------------------------------------------------------------------ */

static int failures = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (!(cond))                                                \
        {                                                           \
            fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
            ++failures;                                             \
        }                                                           \
    } while (0)

#define SEEDS 16u

/* Mean sampled gap over fixed seeds, for the given sampling settings. */
static int sampled_gap_mean(const pe_external_game_t *game, uint8_t player,
                            uint32_t samples, uint32_t min_samples,
                            uint32_t max_samples, double *out_mean)
{
    double total = 0.0;
    uint32_t s;
    for (s = 0u; s < SEEDS; ++s)
    {
        pe_external_br_config_t cfg = pe_external_br_config_default();
        pe_external_br_result_t result;
        cfg.mode = PE_BR_SAMPLED;
        cfg.samples = samples;
        cfg.max_depth = 64u;
        cfg.seed = 0x2740u + (uint64_t)s;
        cfg.sampling.min_samples = min_samples;
        cfg.sampling.max_samples = max_samples;
        if (pe_external_best_response_sampled(game, player, &cfg, &result) != 0)
            return -1;
        if (result.mode != PE_BR_SAMPLED ||
            result.guarantee != PE_GUARANTEE_EMPIRICAL)
            return -1;
        total += result.br_gap;
    }
    *out_mean = total / (double)SEEDS;
    return 0;
}

int main(void)
{
    pe_external_game_t game;
    pe_external_br_config_t exact_cfg;
    pe_external_br_result_t exact;
    double exact_gap = 0.375; /* BR value 1/2 minus policy value 1/8 */
    double legacy_256, legacy_4096, debiased_256, debiased_512, opt_out;
    double margin;

    kuhn_game(&game);

    /* 1. The exact gap is the fixture's hand-derived 3/8. */
    exact_cfg = pe_external_br_config_default();
    exact_cfg.mode = PE_BR_EXACT;
    exact_cfg.max_depth = 64u;
    if (pe_external_best_response_exact(&game, 0u, &exact_cfg, &exact) != 0 ||
        exact.mode != PE_BR_EXACT)
    {
        fprintf(stderr, "test_br_default_bias: exact BR failed\n");
        return 1;
    }
    CHECK(fabs(exact.br_gap - exact_gap) < 1e-12,
          "exact Kuhn gap is 3/8");
    CHECK(fabs(exact.policy_value - 0.125) < 1e-12,
          "exact Kuhn policy value is 1/8");

    /* 2. The default configuration enables the confidence-guided
       evaluation; the historical estimator is the explicit opt-out. */
    CHECK(pe_external_br_config_default().sampling.max_samples ==
              PE_BR_SAMPLING_DEFAULT_MAX_SAMPLES,
          "default BR config enables the confidence-guided evaluation");
    CHECK(pe_solver_config_default().br_sampling.max_samples ==
              PE_BR_SAMPLING_DEFAULT_MAX_SAMPLES,
          "default solver config enables the confidence-guided evaluation");

    /* 3. The historical estimator overstates, and the overstatement is a
       property of the decision rule rather than of the trajectory count:
       it survives a 16x increase in --br-samples. */
    if (sampled_gap_mean(&game, 0u, 256u, 0u, 0u, &legacy_256) != 0 ||
        sampled_gap_mean(&game, 0u, 4096u, 0u, 0u, &legacy_4096) != 0)
    {
        fprintf(stderr, "test_br_default_bias: historical estimator failed\n");
        return 1;
    }
    CHECK(legacy_256 > exact_gap * 1.25,
          "one rollout per action overstates the exact gap");
    CHECK(legacy_4096 > exact_gap * 1.25,
          "more trajectories do not remove the one-rollout overstatement");
    CHECK(legacy_256 - legacy_4096 < exact_gap * 0.25,
          "the one-rollout overstatement barely moves with --br-samples");

    /* 4. The default estimator is a bounded-bias estimate. Two standard
       errors of the mean over 16 seeds is well under a quarter of the exact
       gap here, but the bound asserted is deliberately loose: the point is
       that the selection overshoot is gone, not that it is zero. */
    if (sampled_gap_mean(&game, 0u, 256u, 4u,
                         PE_BR_SAMPLING_DEFAULT_MAX_SAMPLES, &debiased_256) != 0 ||
        sampled_gap_mean(&game, 0u, 512u, 4u,
                         PE_BR_SAMPLING_DEFAULT_MAX_SAMPLES, &debiased_512) != 0)
    {
        fprintf(stderr, "test_br_default_bias: debiased estimator failed\n");
        return 1;
    }
    margin = exact_gap * 0.15;
    CHECK(fabs(debiased_256 - exact_gap) < margin,
          "the default estimator tracks the exact gap");
    CHECK(fabs(debiased_512 - exact_gap) < margin,
          "the default estimator stays debiased with more trajectories");
    CHECK(debiased_256 < legacy_256 - exact_gap * 0.15,
          "the default estimator is closer than the historical one");

    /* 5. max_samples == 0 still selects the historical estimator, so the
       opt-out is real and the comparison above is meaningful. */
    if (sampled_gap_mean(&game, 0u, 256u, 0u, 0u, &opt_out) != 0)
    {
        fprintf(stderr, "test_br_default_bias: opt-out estimator failed\n");
        return 1;
    }
    CHECK(fabs(opt_out - legacy_256) < 1e-12,
          "max_samples 0 reproduces the historical estimator exactly");

    if (failures)
    {
        fprintf(stderr, "test_br_default_bias: %d check(s) failed\n", failures);
        return 1;
    }
    printf("test_br_default_bias: exact %.6f, one rollout %.6f (%.6f at 4096), "
           "confidence-guided %.6f\n",
           exact_gap, legacy_256, legacy_4096, debiased_256);
    return 0;
}
