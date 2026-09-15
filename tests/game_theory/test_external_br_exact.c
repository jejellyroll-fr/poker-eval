/* Issue #233: exact best response against independently enumerated values.
 *
 * Fixture: Kuhn poker (J<Q<K, ante 1, one bet of 1) exposed through the Lane
 * B external-game callbacks, with a chance root enumerating the six ordered
 * deals and both players following the uniform behavioral strategy.
 *
 * Hand-derived ground truth under uniform play:
 *   policy value (player 0) = +1/8      (player 1 = -1/8, zero-sum)
 *   BR value player 0       = +1/2      (always bet, always call)
 *   BR value player 1       = +5/12     (best response against uniform p0)
 *   br gaps                 = 3/8 and 13/24, NashConv = 11/12
 *
 * The exact BR is additionally checked against a fully independent
 * enumeration over all 2^12 pure-strategy profiles. Resource guards and the
 * AUTO fallback are exercised explicitly; sampled mode is checked for stable
 * metadata and finite estimates.
 */

#include <poker_eval/solver/pe_external_best_response.h>
#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Kuhn poker external adapter
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
    /* The acting player's own card plus the visible history tag. */
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
    {
        const void *child = (const void *)(uintptr_t)(deal * 100 +
                                                      (action ? 2 : 1) + 1);
        return child;
    }
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
    return 0.5; /* uniform behavioral strategy everywhere */
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
    case 3: p0 = p0_wins ? 1.0 : -1.0; break;              /* both passed  */
    case 5: p0 = 1.0; break;                               /* p1 folded    */
    case 6: p0 = p0_wins ? 2.0 : -2.0; break;              /* bet called   */
    case 7: p0 = -1.0; break;                              /* p0 folded    */
    case 8: p0 = p0_wins ? 2.0 : -2.0; break;              /* bet called   */
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

static void kuhn_game(pe_external_game_t *game, int with_enumeration)
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
    game->chance_outcome_count = with_enumeration ? kuhn_chance_outcomes : NULL;
}

/* ------------------------------------------------------------------ *
 * Independent enumeration (deliberately written without the adapter)
 * ------------------------------------------------------------------ */

static double kuhn_payoff(int deal, int node)
{
    int c0 = kuhn_deals[deal][0], c1 = kuhn_deals[deal][1];
    int p0_wins = c0 > c1;
    switch (node)
    {
    case 3: return p0_wins ? 1.0 : -1.0;
    case 5: return 1.0;
    case 6: return p0_wins ? 2.0 : -2.0;
    case 7: return -1.0;
    case 8: return p0_wins ? 2.0 : -2.0;
    default: return 0.0;
    }
}

/* Pure strategy: bit (player*6 + infoset_in_player*3 + card) selects the
   aggressive action. Infosets per player: first decision, second decision. */
static double kuhn_value_p0(int deal, int node, uint32_t p0_bits,
                            uint32_t p1_bits)
{
    int c0 = kuhn_deals[deal][0], c1 = kuhn_deals[deal][1];
    int aggressive;
    switch (node)
    {
    case 3: case 5: case 6: case 7: case 8:
        return kuhn_payoff(deal, node);
    case 0:
        aggressive = (p0_bits >> (0u * 3u + c0)) & 1u;
        return kuhn_value_p0(deal, aggressive ? 2 : 1, p0_bits, p1_bits);
    case 1:
        aggressive = (p1_bits >> (0u * 3u + c1)) & 1u;
        return kuhn_value_p0(deal, aggressive ? 4 : 3, p0_bits, p1_bits);
    case 2:
        aggressive = (p1_bits >> (1u * 3u + c1)) & 1u;
        return kuhn_value_p0(deal, aggressive ? 6 : 5, p0_bits, p1_bits);
    case 4:
        aggressive = (p0_bits >> (1u * 3u + c0)) & 1u;
        return kuhn_value_p0(deal, aggressive ? 8 : 7, p0_bits, p1_bits);
    default:
        return 0.0;
    }
}

/* Average over all deals and all pure profiles: the value of the uniform
   product strategy, computed independently of the adapter. */
static double kuhn_uniform_value_p0(void)
{
    double total = 0.0;
    uint32_t p0_bits, p1_bits;
    int deal;
    for (p0_bits = 0u; p0_bits < 64u; ++p0_bits)
        for (p1_bits = 0u; p1_bits < 64u; ++p1_bits)
            for (deal = 0; deal < 6; ++deal)
                total += kuhn_value_p0(deal, 0, p0_bits, p1_bits);
    return total / (64.0 * 64.0 * 6.0);
}

/* Best response: maximise over p0's (resp. p1's) pure strategies while the
   other player keeps the uniform product strategy (average over their pure
   profiles). A behavioral best response attains the same value. */
static double kuhn_br_value_p0(int br_player)
{
    double best = -INFINITY;
    uint32_t br_bits, opp_bits;
    int deal;
    for (br_bits = 0u; br_bits < 64u; ++br_bits)
    {
        double total = 0.0;
        for (opp_bits = 0u; opp_bits < 64u; ++opp_bits)
            for (deal = 0; deal < 6; ++deal)
            {
                if (br_player == 0)
                    total += kuhn_value_p0(deal, 0, br_bits, opp_bits);
                else
                    total += -kuhn_value_p0(deal, 0, opp_bits, br_bits);
            }
        total /= (64.0 * 6.0);
        if (total > best) best = total;
    }
    return best;
}

/* ------------------------------------------------------------------ *
 * Checks
 * ------------------------------------------------------------------ */

static int failures = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);        \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static int close_enough(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

static void check_exact_br(pe_external_game_t *game)
{
    pe_external_br_config_t cfg = pe_external_br_config_default();
    pe_external_br_result_t out;
    int player;

    cfg.mode = PE_BR_EXACT;
    cfg.max_depth = 64u;
    cfg.max_br_time_ms = 10000u; /* generous: must not interfere */
    for (player = 0; player < 2; ++player)
    {
        {
            int rc = pe_external_best_response_exact(game, (uint8_t)player,
                                                     &cfg, &out);
            if (rc != PE_BR_OK)
            {
                fprintf(stderr, "exact player %d rc=%d\n", player, rc);
                CHECK(0, "exact BR should succeed on enumerable Kuhn");
                return;
            }
        }
        CHECK(out.mode == PE_BR_EXACT, "exact BR reports exact mode");
        CHECK(out.guarantee == PE_GUARANTEE_NASH,
              "exact heads-up BR carries the Nash guarantee");
        CHECK(out.empirical == 0, "exact BR is not flagged empirical");
        CHECK(out.nodes_visited > 0, "exact BR counts its states");
        if (player == 0)
        {
            CHECK(close_enough(out.policy_value, 0.125, 1e-12),
                  "policy value p0 = 1/8");
            CHECK(close_enough(out.br_value, 0.5, 1e-12),
                  "BR value p0 = 1/2");
            CHECK(close_enough(out.br_gap, 0.375, 1e-12), "BR gap p0 = 3/8");
        }
        else
        {
            CHECK(close_enough(out.policy_value, -0.125, 1e-12),
                  "policy value p1 = -1/8");
            CHECK(close_enough(out.br_value, 5.0 / 12.0, 1e-12),
                  "BR value p1 = 5/12");
            CHECK(close_enough(out.br_gap, 13.0 / 24.0, 1e-12),
                  "BR gap p1 = 13/24");
        }
    }
}

static void check_against_enumeration(pe_external_game_t *game)
{
    pe_external_br_config_t cfg = pe_external_br_config_default();
    pe_external_br_result_t out;
    double uniform = kuhn_uniform_value_p0();
    double br0 = kuhn_br_value_p0(0);
    double br1 = kuhn_br_value_p0(1);

    cfg.mode = PE_BR_EXACT;
    cfg.max_depth = 64u;
    CHECK(close_enough(uniform, 0.125, 1e-12),
          "enumerated uniform value matches the hand-derived 1/8");
    CHECK(close_enough(br0, 0.5, 1e-12),
          "enumerated BR p0 matches the hand-derived 1/2");
    CHECK(close_enough(br1, 5.0 / 12.0, 1e-12),
          "enumerated BR p1 matches the hand-derived 5/12");
    if (pe_external_best_response_exact(game, 0u, &cfg, &out) == PE_BR_OK)
        CHECK(close_enough(out.br_gap, br0 - uniform, 1e-12),
              "exact BR gap p0 matches independent enumeration");
    else
        CHECK(0, "exact BR p0 should succeed");
    if (pe_external_best_response_exact(game, 1u, &cfg, &out) == PE_BR_OK)
        CHECK(close_enough(out.br_gap, br1 - (-uniform), 1e-12),
              "exact BR gap p1 matches independent enumeration");
    else
        CHECK(0, "exact BR p1 should succeed");
}

static void check_resource_guards(pe_external_game_t *game)
{
    pe_external_br_config_t cfg = pe_external_br_config_default();
    pe_external_br_result_t out;

    cfg.mode = PE_BR_EXACT;
    cfg.max_depth = 64u;
    cfg.max_br_nodes = 3u;
    CHECK(pe_external_best_response_exact(game, 0u, &cfg, &out) ==
          PE_BR_ERR_BUDGET,
          "exact BR refuses explicitly on a node budget");

    cfg.max_br_nodes = 0u;
    cfg.max_br_time_ms = 1u;
    {
        /* Kuhn is tiny and may finish inside the budget; either an explicit
           budget refusal or success is acceptable, but a refused traversal
           must never be reported as an exact result. */
        int rc = pe_external_best_response_exact(game, 0u, &cfg, &out);
        if (rc == PE_BR_ERR_BUDGET)
            CHECK(out.mode != PE_BR_EXACT,
                  "a refused traversal is never reported as exact");
        else
            CHECK(rc == PE_BR_OK, "time-guarded exact BR succeeds or refuses");
    }
    cfg.max_br_time_ms = 0u;
}

/* Minimal two-player chain used to exercise the time guard: a long row of
   player nodes where the policy always continues and the only payoff is 1
   at the end. Shallow enough for recursion, long enough to plausibly miss a
   1 ms budget on a slow machine. */
#define CHAIN_LENGTH 1000
#define CHAIN_GOAL (CHAIN_LENGTH)
#define CHAIN_DEAD (CHAIN_LENGTH + 100)

static int chain_terminal(const void *state, void *user)
{
    int s = (int)(uintptr_t)state - 1;
    (void)user;
    return s == CHAIN_GOAL || s == CHAIN_DEAD;
}
static int chain_acting(const void *state, void *user)
{
    (void)user;
    return chain_terminal(state, user) ? -1 : 0;
}
static uint16_t chain_actions(const void *state, void *user)
{
    (void)user;
    return chain_terminal(state, user) ? 0u : 2u;
}
static double chain_probability(const void *state, uint64_t infoset,
                                uint16_t action, void *user)
{
    (void)state; (void)infoset; (void)user;
    return action == 0u ? 1.0 : 0.0;
}
static const void *chain_apply(const void *state, uint16_t action, void *user)
{
    int s = (int)(uintptr_t)state - 1;
    (void)user;
    if (action == 0u)
        return (const void *)(uintptr_t)((s + 1 <= CHAIN_GOAL
                                             ? s + 1 : CHAIN_GOAL) + 1);
    return (const void *)(uintptr_t)(CHAIN_DEAD + 1);
}
static double chain_utility(const void *state, int player, void *user)
{
    (void)player; (void)user;
    return (int)(uintptr_t)state - 1 == CHAIN_GOAL ? 1.0 : 0.0;
}

static void check_time_guard(void)
{
    pe_external_game_t chain;
    pe_external_br_config_t cfg = pe_external_br_config_default();
    pe_external_br_result_t out;

    memset(&chain, 0, sizeof(chain));
    chain.root = (const void *)(uintptr_t)1u;
    chain.player_count = 2u;
    chain.is_terminal = chain_terminal;
    chain.acting_player = chain_acting;
    chain.action_count = chain_actions;
    chain.apply_action = chain_apply;
    chain.action_probability = chain_probability;
    chain.terminal_value = chain_utility;

    cfg.mode = PE_BR_EXACT;
    cfg.max_depth = (uint16_t)(CHAIN_LENGTH + 2u);
    cfg.max_br_time_ms = 1u;
    {
        int rc = pe_external_best_response_exact(&chain, 0u, &cfg, &out);
        if (rc == PE_BR_ERR_BUDGET)
            CHECK(out.mode != PE_BR_EXACT,
                  "time-guarded refusal is not reported as exact");
        else if (rc == PE_BR_OK)
            CHECK(close_enough(out.br_gap, 0.0, 1e-12),
                  "chain BR gap is zero when the guard does not trigger");
        else
            CHECK(0, "chain BR: unexpected error");
    }
}

static void check_auto_dispatch(pe_external_game_t *enumerable,
                                pe_external_game_t *opaque)
{
    pe_external_br_config_t cfg = pe_external_br_config_default();
    pe_external_br_result_t out;

    /* AUTO on a small enumerable game resolves to exact. */
    cfg.mode = PE_BR_AUTO;
    cfg.max_depth = 64u;
    CHECK(pe_external_best_response(enumerable, 0u, &cfg, &out) == PE_BR_OK,
          "AUTO succeeds on enumerable Kuhn");
    CHECK(out.mode == PE_BR_EXACT, "AUTO selects exact for a small game");
    CHECK(out.guarantee == PE_GUARANTEE_NASH, "AUTO exact result is Nash");

    /* AUTO with a tiny budget falls back and says so. */
    cfg.max_br_nodes = 3u;
    CHECK(pe_external_best_response(enumerable, 0u, &cfg, &out) == PE_BR_OK,
          "AUTO falls back to sampled on budget refusal");
    CHECK(out.mode == PE_BR_SAMPLED,
          "AUTO fallback reports the sampled mode");
    CHECK(out.guarantee == PE_GUARANTEE_EMPIRICAL,
          "AUTO fallback reports the empirical guarantee");
    CHECK(out.empirical == 1, "AUTO fallback result is empirical");
    cfg.max_br_nodes = 0u;

    /* EXACT on a game whose chance cannot be enumerated fails explicitly. */
    cfg.mode = PE_BR_EXACT;
    CHECK(pe_external_best_response(opaque, 0u, &cfg, &out) ==
          PE_BR_ERR_CHANCE_NOT_ENUMERABLE,
          "EXACT refuses a non-enumerable chance node explicitly");

    /* AUTO falls back to sampling there too, without claiming exactness. */
    cfg.mode = PE_BR_AUTO;
    CHECK(pe_external_best_response(opaque, 0u, &cfg, &out) == PE_BR_OK,
          "AUTO falls back on a non-enumerable chance node");
    CHECK(out.mode == PE_BR_SAMPLED && out.empirical == 1 &&
              out.guarantee == PE_GUARANTEE_EMPIRICAL,
          "AUTO fallback on opaque chance stays empirical");
}

static void check_sampled_calibration(pe_external_game_t *game)
{
    const uint32_t sample_counts[] = {32u, 64u, 128u, 256u, 512u, 1024u};
    const size_t count = sizeof(sample_counts) / sizeof(sample_counts[0]);
    double err_last = 0.0;
    size_t i;
    int player;

    for (player = 0; player < 2; ++player)
    {
        const double exact_gap = player == 0 ? 0.375 : 13.0 / 24.0;
        for (i = 0; i < count; ++i)
        {
            pe_external_br_config_t cfg = pe_external_br_config_default();
            pe_external_br_result_t out;
            double err;
            cfg.mode = PE_BR_SAMPLED;
            cfg.samples = sample_counts[i];
            cfg.max_depth = 64u;
            cfg.seed = 0x233u + (uint64_t)player * 1000u;
            if (pe_external_best_response(game, (uint8_t)player, &cfg,
                                          &out) != PE_BR_OK)
            {
                CHECK(0, "sampled BR should succeed");
                return;
            }
            CHECK(out.mode == PE_BR_SAMPLED,
                  "sampled BR reports the sampled mode");
            CHECK(out.guarantee == PE_GUARANTEE_EMPIRICAL,
                  "sampled BR reports the empirical guarantee");
            err = fabs(out.br_gap - exact_gap);
            if (i == count - 1) err_last = err;
            /* Sampling is stochastic and need not improve monotonically for
               a fixed seed; keep this a broad sanity check. */
            CHECK(err < 1.0, "sampled BR gap stays in a sane range");
        }
        CHECK(isfinite(err_last), "sampled BR estimate remains finite");
    }
}

static void check_solver_lifecycle(pe_external_game_t *game)
{
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_metrics_t metrics;

    cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    cfg.max_iterations = 8u;
    cfg.problem.expected_infosets = 12u;
    cfg.problem.expected_actions = 2u;
    cfg.problem.expected_combos = 1u;
    cfg.seed = 0x233u;
    cfg.exploitability_interval = 4u;
    /* Issue #233: the solver can measure BR exactly and must then publish
       the Nash guarantee, not the empirical one. */
    cfg.br_mode = PE_BR_EXACT;
    deps.external_game = game;
    solver = pe_solver_create(&cfg, &deps);
    if (!solver)
    {
        CHECK(0, "solver create failed");
        return;
    }
    if (pe_solver_run(solver) != PE_SOLVER_OK)
    {
        CHECK(0, "solver run with exact BR failed");
        pe_solver_destroy(solver);
        return;
    }
    if (pe_solver_metrics(solver, &metrics) != PE_SOLVER_OK)
    {
        CHECK(0, "solver metrics unavailable");
        pe_solver_destroy(solver);
        return;
    }
    CHECK(metrics.br_mode == PE_BR_EXACT, "solver reports the exact mode");
    CHECK(metrics.guarantee == PE_GUARANTEE_NASH,
          "solver exact BR carries the Nash guarantee");
    CHECK(metrics.br_gap[0] >= 0.0 && metrics.br_gap[1] >= 0.0,
          "solver exact BR gaps are non-negative");
    CHECK(metrics.exploitability_raw >= 0.0,
          "solver exact exploitability is non-negative");
    pe_solver_destroy(solver);
}

int main(void)
{
    pe_external_game_t enumerable, opaque;

    kuhn_game(&enumerable, 1);
    kuhn_game(&opaque, 0);

    check_exact_br(&enumerable);
    check_against_enumeration(&enumerable);
    check_resource_guards(&enumerable);
    check_time_guard();
    check_auto_dispatch(&enumerable, &opaque);
    check_sampled_calibration(&enumerable);
    check_solver_lifecycle(&enumerable);

    if (failures)
    {
        fprintf(stderr, "test_external_br_exact: %d check(s) failed\n",
                failures);
        return 1;
    }
    puts("test_external_br_exact: exact BR, guards, AUTO and calibration passed");
    return 0;
}
