/*
 * test_cfr_issue170_utility.c - ISSUE-14 (#170) generic CFR utility abstraction
 *
 * Validates pe_utility_fn / pe_cfr_utility_config_t / pe_cfr_set_utility_function
 * plus the cfr_game_t::get_final_stacks callback, against simple non-linear,
 * non-zero-sum terminal payoff functions (logarithmic / quadratic).
 *
 * Coverage:
 *   * pe_cfr_set_utility_function wires the config onto the game and can reset
 *     it (NULL) to restore linear-chip behaviour.
 *   * Terminal evaluation routes through the generic pe_utility_fn when
 *     configured; the computed payoffs match utility_fn(final_stacks, ...).
 *   * Default linear-chip evaluation is 100% backward compatible (identical
 *     values when utility_fn == NULL).
 *   * cfr_config_t::utility is applied to the game at solve time.
 *   * CFR converges under non-linear payoffs (finite, normalized strategies and
 *     a finite exploitability metric).
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return 1;                                       \
        }                                                   \
    } while (0)

#define UT_NUM_PLAYERS 2
#define UT_START 100 /* starting stack, for linear deltas */

/* Minimal 2-player game: a single decision point at the root followed by two
 * terminal states.  Each terminal state just carries the players' final stacks,
 * so payoffs can be evaluated either linearly from UT_START (legacy get_utility)
 * or generically from the stack vector via a pe_utility_fn. */
typedef struct
{
    int is_terminal;
    int player;
    int32_t stacks[UT_NUM_PLAYERS];
} ut_state_t;

static ut_state_t ut_root = {0, 0, {UT_START, UT_START}};
static ut_state_t ut_fold = {1, -1, {95, 105}}; /* P0 folds, loses 5 */
static ut_state_t ut_call = {1, -1, {80, 120}}; /* P1 wins showdown */
static int ut_current_player(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    return ((ut_state_t *)(uintptr_t)k)->player;
}
static int ut_is_terminal(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    return ((ut_state_t *)(uintptr_t)k)->is_terminal;
}
static int ut_get_actions(cfr_game_t *g, uint64_t k, int *out, int maxn, void *u)
{
    (void)g; (void)u;
    const ut_state_t *st = (const ut_state_t *)(uintptr_t)k;
    if (st->is_terminal || maxn < 2)
        return 0;
    out[0] = 0;
    out[1] = 1;
    return 2;
}
static uint64_t ut_apply_action(cfr_game_t *g, uint64_t k, int a, void *u)
{
    (void)g; (void)u;
    const ut_state_t *st = (const ut_state_t *)(uintptr_t)k;
    if (st == &ut_root)
        return (uint64_t)(uintptr_t)(a == 0 ? &ut_fold : &ut_call);
    return k;
}
static int ut_get_final_stacks(cfr_game_t *g, uint64_t k, int32_t *out, void *u)
{
    (void)g; (void)u;
    const ut_state_t *st = (const ut_state_t *)(uintptr_t)k;
    if (!st->is_terminal)
        return -1;
    for (int p = 0; p < UT_NUM_PLAYERS; ++p)
        out[p] = st->stacks[p];
    return 0;
}
static double ut_get_utility(cfr_game_t *g, uint64_t k, int p, void *u)
{
    (void)g; (void)u;
    const ut_state_t *st = (const ut_state_t *)(uintptr_t)k;
    return (double)(st->stacks[p] - UT_START); /* linear chip delta */
}

/* Risk-averse, non-linear, non-zero-sum utility: log of the final stack. */
static double ut_log_utility(const int32_t *stacks, int num_players, int player, void *u)
{
    (void)num_players; (void)u;
    return log((double)stacks[player] + 1.0);
}

/* Quadratic penalty: risk-averse, strongly non-linear, non-zero-sum. */
static double ut_quad_utility(const int32_t *stacks, int n, int player, void *u)
{
    (void)n; (void)u;
    double s = (double)stacks[player];
    double d = s - (double)UT_START;
    return d - 0.001 * d * d;
}

static cfr_game_t ut_make_game(void)
{
    cfr_game_t g;
    memset(&g, 0, sizeof(g));
    g.current_player = ut_current_player;
    g.get_actions = ut_get_actions;
    g.apply_action = ut_apply_action;
    g.is_terminal = ut_is_terminal;
    g.get_utility = ut_get_utility;
    g.get_final_stacks = ut_get_final_stacks;
    g.initial_state = (void *)(uintptr_t)&ut_root;
    g.state_size = sizeof(ut_state_t);
    g.num_players = UT_NUM_PLAYERS;
    return g;
}

/* Root exposes a single action so that after solve the policy value equals the
 * (single) terminal payoff exactly, making routing assertions deterministic. */
static int ut_get_actions_single(cfr_game_t *g, uint64_t k, int *out, int maxn, void *u)
{
    (void)g; (void)u;
    const ut_state_t *st = (const ut_state_t *)(uintptr_t)k;
    if (st->is_terminal || maxn < 1)
        return 0;
    out[0] = 0;
    return 1;
}

/*
 * Confirm default (utility_fn == NULL) evaluation is unchanged and that
 * pe_cfr_set_utility_function reroutes terminal payoffs through the generic
 * pe_utility_fn, and can restore linear behaviour by clearing the config.
 */
static int test_routing_and_backward_compat(void)
{
    cfr_game_t game = ut_make_game();
    game.get_actions = ut_get_actions_single; /* root -> fold only */
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "storage created");

    /* Backward compatible default: linear chip delta from get_utility. */
    double def0 = cfr_compute_policy_value(&game, storage, 0, NULL);
    double def1 = cfr_compute_policy_value(&game, storage, 1, NULL);
    CHECK(fabs(def0 - (-5.0)) < 1e-9, "default linear P0 = -5");
    CHECK(fabs(def1 - 5.0) < 1e-9, "default linear P1 = +5");

    /* Configure a non-linear utility through pe_cfr_set_utility_function. */
    pe_cfr_utility_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.utility_fn = ut_log_utility;
    cfg.user_data = (void *)(uintptr_t)0xCAFE;
    cfg.is_non_linear = 1;
    CHECK(pe_cfr_set_utility_function(&game, cfg) == 0, "set returns 0");
    CHECK(game.utility.utility_fn == ut_log_utility, "game->utility.utility_fn wired");
    CHECK(game.utility.user_data == (void *)(uintptr_t)0xCAFE, "user_data wired");
    CHECK(game.utility.is_non_linear == 1, "is_non_linear flag wired");

    /* Terminal payoffs now come from ut_log_utility(final_stacks, ...). */
    double lg0 = cfr_compute_policy_value(&game, storage, 0, NULL);
    double lg1 = cfr_compute_policy_value(&game, storage, 1, NULL);
    CHECK(fabs(lg0 - log(96.0)) < 1e-9, "non-linear P0 = log(96)");
    CHECK(fabs(lg1 - log(106.0)) < 1e-9, "non-linear P1 = log(106)");

    /* Clearing the config restores the legacy linear evaluation. */
    pe_cfr_utility_config_t none;
    memset(&none, 0, sizeof(none));
    CHECK(pe_cfr_set_utility_function(&game, none) == 0, "clear returns 0");
    double back0 = cfr_compute_policy_value(&game, storage, 0, NULL);
    double back1 = cfr_compute_policy_value(&game, storage, 1, NULL);
    CHECK(fabs(back0 + 5.0) < 1e-9, "back to linear P0 after clear");
    CHECK(fabs(back1 - 5.0) < 1e-9, "back to linear P1 after clear");

    cfr_storage_destroy(storage);
    return 0;
}

/* NULL-game guard on pe_cfr_set_utility_function. */
static int test_set_null_game(void)
{
    pe_cfr_utility_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.utility_fn = ut_log_utility;
    CHECK(pe_cfr_set_utility_function(NULL, cfg) == -1, "NULL game rejected");
    return 0;
}

/*
 * cfr_config_t::utility is applied to the game at solve time and CFR converges
 * under a non-linear payoff (finite, normalized strategy; finite exploitability).
 */
static int test_config_wiring_and_nonlinear_convergence(void)
{
    cfr_game_t game = ut_make_game(); /* root -> {fold, call} */
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "storage created");

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 3000;
    cfg.utility.utility_fn = ut_quad_utility;
    cfg.utility.is_non_linear = 1;

    double expl = -1.0;
    double res = cfr_solve(&game, storage, &cfg, &expl);
    /* cfr_solve returns the final exploitability metric, which is finite but not
     * necessarily non-negative for a non-zero-sum (non-linear) utility. */
    CHECK(isfinite(res), "solve with non-linear utility via config");
    CHECK(game.utility.utility_fn == ut_quad_utility, "config wired to game");
    CHECK(game.utility.is_non_linear == 1, "config flag wired");

    /* Converged average strategy: finite, normalized, non-negative. */
    uint64_t root_key = (uint64_t)(uintptr_t)&ut_root;
    double avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage, root_key, 2, avg);
    CHECK(isfinite(avg[0]) && isfinite(avg[1]), "strategies finite");
    CHECK(avg[0] >= 0.0 && avg[0] <= 1.0, "P0 strategy in [0,1]");
    CHECK(avg[1] >= 0.0 && avg[1] <= 1.0, "P1 strategy in [0,1]");
    CHECK(fabs(avg[0] + avg[1] - 1.0) < 1e-6, "strategies normalized");
    CHECK(isfinite(expl), "exploitability finite");

    cfr_storage_destroy(storage);
    return 0;
}

/* Non-linear utility also flows through the detailed policy-value surface:
 * both players' EV / variance stay finite under an extreme quadratic payoff. */
static int test_quadratic_stability_detailed(void)
{
    cfr_game_t game = ut_make_game();
    pe_cfr_utility_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.utility_fn = ut_quad_utility;
    cfg.is_non_linear = 1;
    CHECK(pe_cfr_set_utility_function(&game, cfg) == 0, "set quadratic utility");

    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "storage created");

    cfr_policy_value_result_t r;
    memset(&r, 0, sizeof(r));
    CHECK(cfr_compute_policy_values_detailed(&game, storage, NULL, &r) == 0,
          "detailed policy values computed");
    CHECK(isfinite(r.ev[0]) && isfinite(r.ev[1]), "policy EV finite");
    CHECK(isfinite(r.variance[0]) && isfinite(r.variance[1]), "policy variance finite");

    cfr_storage_destroy(storage);
    return 0;
}

int main(void)
{
    if (test_set_null_game() != 0)
        return 1;
    if (test_routing_and_backward_compat() != 0)
        return 1;
    if (test_config_wiring_and_nonlinear_convergence() != 0)
        return 1;
    if (test_quadratic_stability_detailed() != 0)
        return 1;
    printf(" PASSED\n");
    return 0;
}
