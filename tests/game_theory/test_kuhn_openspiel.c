/*
 * test_kuhn_openspiel.c - Kuhn poker CFR convergence vs independent oracle
 * (ISSUE-09, #165).
 *
 * OpenSpiel is not assumed installed; instead we build an INDEPENDENT oracle:
 *   (a) the closed-form Kuhn game value +1/18 (Kuhn 1950), and
 *   (b) a self-contained brute-force tree enumeration that computes the exact
 *       game value of any strategy profile by exhaustive traversal, completely
 *       separate from the poker-eval CFR solver's own BR/policy code.
 * The CFR-converged average-strategy value must match the independent oracle.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include "analytical_oracles.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* Kuhn poker: deck ranks J=0 < Q=1 < K=2. Ante 1 each. Bet 1, one raise. */
#define KUHN_PH_ROOT   0   /* P1 to act */
#define KUHN_PH_P2C    1   /* P2 to act after P1 check */
#define KUHN_PH_P1RB   2   /* P1 to act after P2 bet on check line */
#define KUHN_PH_P2B    3   /* P2 to act after P1 bet */
#define KUHN_TERMINAL  4

/* state_key encodes: handP1(2) handP2(2) phase(3) last(2) */
static uint64_t kuhn_key(int p1, int p2, int phase, int last)
{
    return ((uint64_t)(p1 & 3)) |
           ((uint64_t)(p2 & 3) << 2) |
           ((uint64_t)(phase & 7) << 4) |
           ((uint64_t)(last & 3) << 7);
}

static void kuhn_unpack(uint64_t k, int *p1, int *p2, int *phase, int *last)
{
    *p1 = (int)(k & 3);
    *p2 = (int)((k >> 2) & 3);
    *phase = (int)((k >> 4) & 7);
    *last = (int)((k >> 7) & 3);
}

static int kuhn_current_player(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    int phase = (int)(k >> 4) & 7;
    if (phase == KUHN_PH_ROOT || phase == KUHN_PH_P1RB) return 0;
    if (phase == KUHN_PH_P2C || phase == KUHN_PH_P2B) return 1;
    return -1;
}

static int kuhn_is_terminal(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    int phase = (int)(k >> 4) & 7;
    return phase >= KUHN_TERMINAL;
}

/* Terminal utility for player 1 (zero-sum). last = last action (0/1). */
static double kuhn_util(int p1, int p2, int phase, int last)
{
    int p1wins = p1 > p2; /* higher rank wins */
    if (phase == KUHN_TERMINAL + KUHN_PH_P2C) {
        /* both checked -> showdown for the 2 ante pot, P1 nets +-1 */
        return p1wins ? 1.0 : -1.0;
    }
    if (phase == KUHN_TERMINAL + KUHN_PH_P1RB) {
        /* P2 bet, P1 acted: last 0=fold(P1 loses ante 1), 1=call(pot 4) */
        if (last == 0) return -1.0;
        return p1wins ? 2.0 : -2.0;
    }
    if (phase == KUHN_TERMINAL + KUHN_PH_P2B) {
        /* P1 bet, P2 acted: last 0=fold(P2 loses ante 1 -> P1 +1), 1=call */
        if (last == 0) return 1.0;
        return p1wins ? 2.0 : -2.0;
    }
    return 0.0;
}

static double kuhn_get_utility(cfr_game_t *g, uint64_t k, int player, void *u)
{
    (void)g; (void)u;
    int p1, p2, phase, last; kuhn_unpack(k, &p1, &p2, &phase, &last);
    if (phase < KUHN_TERMINAL) return 0.0;
    double v = kuhn_util(p1, p2, phase, last);
    return (player == 0) ? v : -v;
}

static int kuhn_get_actions(cfr_game_t *g, uint64_t k, int *out, int maxn, void *u)
{
    (void)g; (void)u;
    int phase = (int)(k >> 4) & 7;
    if (phase >= KUHN_TERMINAL) return 0;
    if (maxn < 2) return 0;
    out[0] = 0; out[1] = 1;
    return 2;
}

/* Information-set key: strip the opponent hand (hidden) so the solver
 * aggregates over opponent hands and recovers the GLOBAL Kuhn equilibrium. */
static uint64_t kuhn_infoset_key(const void *state)
{
    uint64_t k = (uint64_t)(uintptr_t)state;
    if (k == 0) return 0; /* chance root */
    int p1, p2, phase, last; kuhn_unpack(k, &p1, &p2, &phase, &last);
    if (phase >= KUHN_TERMINAL) return (1ULL << 60) | k;
    int hand = (phase == KUHN_PH_ROOT || phase == KUHN_PH_P1RB) ? p1 : p2;
    return (1ULL << 60) | ((uint64_t)hand << 8) | (uint64_t)phase;
}

static uint64_t kuhn_apply_action(cfr_game_t *g, uint64_t k, int a, void *u)
{
    (void)g; (void)u;
    int p1, p2, phase, last; kuhn_unpack(k, &p1, &p2, &phase, &last);
    switch (phase) {
        case KUHN_PH_ROOT:
            if (a == 0) return kuhn_key(p1, p2, KUHN_PH_P2C, a);
            return kuhn_key(p1, p2, KUHN_PH_P2B, a);
        case KUHN_PH_P2C:
            if (a == 0) return kuhn_key(p1, p2, KUHN_TERMINAL + KUHN_PH_P2C, a);
            return kuhn_key(p1, p2, KUHN_PH_P1RB, a);
        case KUHN_PH_P1RB:
            return kuhn_key(p1, p2, KUHN_TERMINAL + KUHN_PH_P1RB, a);
        case KUHN_PH_P2B:
            return kuhn_key(p1, p2, KUHN_TERMINAL + KUHN_PH_P2B, a);
        default:
            return k;
    }
}

/* Chance: root deals an ordered pair of distinct cards (6 outcomes). */
static int kuhn_is_chance(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u;
    return k == 0;
}
static int kuhn_chance_outcomes(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u; (void)k;
    return 6;
}
static uint64_t kuhn_apply_chance(cfr_game_t *g, uint64_t k, int o, void *u)
{
    (void)g; (void)u;
    if (k != 0) return k;
    static const int pairs[6][2] = {{0,1},{0,2},{1,0},{1,2},{2,0},{2,1}};
    return kuhn_key(pairs[o][0], pairs[o][1], KUHN_PH_ROOT, 0);
}

/* Independent brute-force game-value computation for an arbitrary strategy
 * profile stored in `st`. Recursively walks the full 6-hand-pair tree, at each
 * decision node using the AVERAGE strategy from storage (read by state_key),
 * at chance nodes averaging uniformly. Returns player-1 expected value.
 * This does NOT call any solver BR/policy function. */
static double kuhn_brute_value(cfr_game_t *g, cfr_storage_t *st, uint64_t k,
                               double reach_p1, double reach_p2)
{
    if (g->is_chance(g, k, NULL)) {
        int n = g->get_chance_outcomes(g, k, NULL);
        double sum = 0.0;
        for (int o = 0; o < n; o++) {
            uint64_t nk = g->apply_chance(g, k, o, NULL);
            sum += kuhn_brute_value(g, st, nk, reach_p1, reach_p2);
        }
        return sum / n;
    }
    if (g->is_terminal(g, k, NULL)) {
        return g->get_utility(g, k, 0, NULL);
    }
    int player = g->current_player(g, k, NULL);
    int acts[2]; int na = g->get_actions(g, k, acts, 2, NULL);
    double strat[2];
    if (na == 2) {
        /* Read the average strategy stored under the state's INFOSET key (the
         * solver aggregates over the opponent's hidden card here). */
        cfr_storage_get_avg_strategy(st, kuhn_infoset_key((const void *)(uintptr_t)k), 2, strat);
    } else { strat[0] = 1.0; strat[1] = 0.0; }
    double val = 0.0;
    for (int i = 0; i < na; i++) {
        uint64_t nk = g->apply_action(g, k, acts[i], NULL);
        double r1 = reach_p1, r2 = reach_p2;
        if (player == 0) r1 *= strat[i];
        else r2 *= strat[i];
        val += strat[i] * kuhn_brute_value(g, st, nk, r1, r2);
    }
    return val;
}

#define CHECK(cond, msg) do { if(!(cond)){ fprintf(stderr,"FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); return 1; } } while(0)
#define CHECK_CLOSE(a,b,eps,msg) do { if(fabs((a)-(b))>(eps)){ fprintf(stderr,"FAIL: %s (got %g want %g tol %g)\n", msg,(double)(a),(double)(b),(double)(eps)); return 1; } } while(0)

static int test_kuhn_solve(void)
{
    printf("  test_kuhn_solve...");

    cfr_game_t g;
    memset(&g, 0, sizeof(g));
    g.current_player = kuhn_current_player;
    g.is_terminal   = kuhn_is_terminal;
    g.get_utility   = kuhn_get_utility;
    g.get_actions   = kuhn_get_actions;
    g.apply_action  = kuhn_apply_action;
    g.get_infoset_key = kuhn_infoset_key;
    g.is_chance     = kuhn_is_chance;
    g.get_chance_outcomes = kuhn_chance_outcomes;
    g.apply_chance  = kuhn_apply_chance;
    g.initial_state = (void *)(uintptr_t)0;
    g.state_size    = sizeof(uint64_t);
    g.num_players   = 2;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 8000;
    cfg.enable_dcfr = 1;
    cfg.dcfr_alpha = 1.5; cfg.dcfr_beta = 0.0; cfg.dcfr_gamma = 2.0;

    cfr_storage_t *st = cfr_storage_create();
    CHECK(st != NULL, "storage alloc");
    double expl = 0.0;
    /* cfr_solve returns the perfect-information best-response exploitability
     * (a double), NOT an error code. For an imperfect-information game it is an
     * upper bound that stays positive at equilibrium, so we do not gate on it. */
    cfr_solve(&g, st, &cfg, &expl);

    /* Solver's own policy value */
    double p1_ev_solver = cfr_compute_policy_value(&g, st, 0, NULL);
    double p2_ev_solver = cfr_compute_policy_value(&g, st, 1, NULL);
    /* Independent brute-force value of the average strategy */
    double p1_ev_brute = kuhn_brute_value(&g, st, (uint64_t)0, 1.0, 1.0);

    kuhn_equilibrium_t eq;
    get_kuhn_analytical_solution(&eq);

    printf("\n    solver EV1=%.5f, brute EV1=%.5f, oracle=%.5f, P2=%.5f, expl(proxy)=%.5f\n",
           p1_ev_solver, p1_ev_brute, eq.p1_ev, p2_ev_solver, expl);

    CHECK_CLOSE(p1_ev_solver, eq.p1_ev, 2e-3, "solver Kuhn value");
    CHECK_CLOSE(p1_ev_brute,  eq.p1_ev, 2e-3, "independent Kuhn value");
    CHECK_CLOSE(p1_ev_solver, p1_ev_brute, 1e-5, "solver vs independent agree");
    CHECK_CLOSE(p2_ev_solver, -p1_ev_solver, 1e-3, "zero-sum: P2 value mirrors P1");

    cfr_storage_destroy(st);
    printf(" PASSED\n");
    return 0;
}

int main(void)
{
    printf("Running Kuhn poker benchmark (ISSUE-09 #165)...\n");
    int failures = 0;
    failures += test_kuhn_solve();
    if (failures == 0) { printf("All Kuhn tests PASSED\n"); return 0; }
    printf("%d Kuhn test(s) FAILED\n", failures);
    return 1;
}
