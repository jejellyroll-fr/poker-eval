/*
 * test_gambit_exact_lp.c - Sequence-form exact LP oracle (Gambit-style)
 * (ISSUE-09, #165).
 *
 * Implements a self-contained sequence-form linear-program solver
 * (analytical_oracles.h::seqform_lp_solve) that returns the EXACT zero-sum
 * game value, with zero tolerance. We validate it three ways:
 *   1. A hand-constructed 2x2 game whose exact value is 0.2 (closed form).
 *   2. An independent brute-force minimax over deterministic pure strategies.
 *   3. The same game built as a poker-eval CFR vtable (modelled as a proper
 *      simultaneous-move matrix game, so P2's information set hides P1's
 *      action and a mixed equilibrium exists): CFR convergence must match the
 *      exact LP value within eps < 1e-4 (acceptance criterion).
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include "analytical_oracles.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* ---- The tiny reference game (sequence form 2x2) ---------------- */
/* P1 sequences (rows): Up=0, Down=1. P2 sequences (cols): L=0, R=1.
 * A[i][j] = utility to P1 at the terminal consistent with (i,j).
 * Exact value (closed form) = 0.2.                                  */
static const double GAME_A[2][2] = {
    { 2.0, -1.0 },
    { -1.0, 1.0 }
};

/* Independent brute-force minimax (NOT the LP): grid-search the column
 * player's mixed strategy y and take max_y min_i (A y)_i. For the 2x2
 * reference game this reproduces the closed-form value 0.2 by exhaustive
 * refinement, giving a genuinely independent cross-check of seqform_lp_solve. */
static double brute_minimax(const double *A, int m, int n)
{
    if (n != 2) return 0.0; /* only the 2x2 reference game is exercised here */
    const int STEPS = 20000;
    double best = -1e300;
    for (int t = 0; t <= STEPS; ++t) {
        double y0 = (double)t / (double)STEPS;
        double y1 = 1.0 - y0;
        double mn = 1e300;
        for (int i = 0; i < m; ++i) {
            double v = A[(size_t)i * 2 + 0] * y0 + A[(size_t)i * 2 + 1] * y1;
            if (v < mn) mn = v;
        }
        if (mn > best) best = mn;
    }
    return best;
}

/* ---- CFR vtable for the same game (simultaneous matrix game) ----- */
/* Root: P1 picks Up/Down. Then a node where P2 picks L/R, but P2's
 * information set is the SAME regardless of P1's choice (P2 does not observe
 * P1), so a mixed equilibrium exists. Terminal payoff = GAME_A[p1][p2]. */
#define GM_PH_ROOT   0
#define GM_PH_P2     1
#define GM_TERM      2

static uint64_t gm_key(int p1act, int phase, int p2act)
{
    return ((uint64_t)(p1act & 3)) | ((uint64_t)(phase & 3) << 2) | ((uint64_t)(p2act & 3) << 5);
}
static void gm_dec(uint64_t k, int *p1, int *ph, int *p2)
{
    *p1 = (int)(k & 3); *ph = (int)((k >> 2) & 3); *p2 = (int)((k >> 5) & 3);
}

static int gm_current_player(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u; int p1,ph,p2; gm_dec(k,&p1,&ph,&p2);
    return (ph == GM_PH_ROOT) ? 0 : 1;
}
static int gm_is_terminal(cfr_game_t *g, uint64_t k, void *u)
{
    (void)g; (void)u; int p1,ph,p2; gm_dec(k,&p1,&ph,&p2);
    return ph == GM_TERM;
}
static double gm_util(int p1, int p2)
{
    return GAME_A[p1][p2];
}
static double gm_get_utility(cfr_game_t *g, uint64_t k, int player, void *u)
{
    (void)g; (void)u; int p1,ph,p2; gm_dec(k,&p1,&ph,&p2);
    if (ph != GM_TERM) return 0.0;
    double v = gm_util(p1, p2);
    return (player == 0) ? v : -v;
}
static int gm_get_actions(cfr_game_t *g, uint64_t k, int *out, int maxn, void *u)
{
    (void)g; (void)u; int p1,ph,p2; gm_dec(k,&p1,&ph,&p2);
    (void)p1; (void)p2;
    if (ph == GM_TERM) return 0;
    if (maxn < 2) return 0;
    out[0] = 0; out[1] = 1; return 2;
}
static uint64_t gm_apply_action(cfr_game_t *g, uint64_t k, int a, void *u)
{
    (void)g; (void)u; int p1,ph,p2; gm_dec(k,&p1,&ph,&p2);
    if (ph == GM_PH_ROOT) return gm_key(a, GM_PH_P2, 0);
    if (ph == GM_PH_P2)   return gm_key(p1, GM_TERM, a);
    return k;
}

/* P2's information set hides P1's action: every P2 node maps to ONE infoset
 * key so P2 mixes over the (unobserved) P1 choice. P1's root is its own
 * infoset. */
static uint64_t gm_infoset_key(const void *state)
{
    uint64_t k = (uint64_t)(uintptr_t)state;
    int p1, ph, p2; gm_dec(k, &p1, &ph, &p2);
    if (ph == GM_TERM) return (1ULL << 60) | k;
    if (ph == GM_PH_ROOT) return 0x1234;   /* P1 root infoset */
    return 0x5678;                          /* P2 infoset (observes nothing) */
}

/* Independent brute-force value of the converged average strategy. */
static double gm_brute_value(cfr_game_t *g, cfr_storage_t *st, uint64_t k)
{
    if (g->is_terminal(g, k, NULL)) return g->get_utility(g, k, 0, NULL);
    int acts[2]; int na = g->get_actions(g, k, acts, 2, NULL);
    double strat[2];
    if (na == 2) {
        uint64_t ik = gm_infoset_key((const void *)(uintptr_t)k);
        cfr_storage_get_avg_strategy(st, ik, 2, strat);
    } else { strat[0] = 1.0; strat[1] = 0.0; }
    double val = 0.0;
    for (int i = 0; i < na; i++)
        val += strat[i] * gm_brute_value(g, st, g->apply_action(g, k, acts[i], NULL));
    return val;
}

#define CHECK(cond, msg) do { if(!(cond)){ fprintf(stderr,"FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); return 1; } } while(0)
#define CHECK_CLOSE(a,b,eps,msg) do { if(fabs((a)-(b))>(eps)){ fprintf(stderr,"FAIL: %s (got %g want %g tol %g)\n", msg,(double)(a),(double)(b),(double)(eps)); return 1; } } while(0)

static int test_gambit_lp(void)
{
    printf("  test_gambit_lp...");

    /* 1. Exact LP on the sequence-form matrix. */
    double Aflat[4];
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) Aflat[i*2+j] = GAME_A[i][j];
    double value = -1e9, y[2], x[2];
    int rc = seqform_lp_solve(Aflat, 2, 2, &value, x, y);
    CHECK(rc == 0, "seqform_lp_solve should succeed");
    printf("\n    LP exact value = %.10f (want 0.2)\n", value);
    CHECK_CLOSE(value, 0.2, 1e-9, "LP exact game value");
    double brute = brute_minimax(Aflat, 2, 2);
    CHECK_CLOSE(value, brute, 1e-9, "LP matches independent minimax");

    /* 2. Same game via CFR: convergence must match exact LP within 1e-4. */
    cfr_game_t g;
    memset(&g, 0, sizeof(g));
    g.current_player = gm_current_player;
    g.is_terminal   = gm_is_terminal;
    g.get_utility   = gm_get_utility;
    g.get_actions   = gm_get_actions;
    g.apply_action  = gm_apply_action;
    g.get_infoset_key = gm_infoset_key;
    g.initial_state = (void *)(uintptr_t)gm_key(0, GM_PH_ROOT, 0);
    g.state_size    = sizeof(uint64_t);
    g.num_players   = 2;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 60000;
    cfg.enable_dcfr = 1;
    cfg.dcfr_alpha = 1.5; cfg.dcfr_beta = 0.0; cfg.dcfr_gamma = 2.0;

    cfr_storage_t *st = cfr_storage_create();
    CHECK(st != NULL, "storage alloc");
    double expl = 0.0;
    /* NOTE: cfr_solve returns the final perfect-information best-response
     * exploitability (a double), NOT an error code. For an imperfect-/
     * simultaneous-information game that value is an upper bound that stays
     * positive even at equilibrium, so we do not assert on it here; the
     * qualification is the policy-value convergence to the exact LP value. */
    cfr_solve(&g, st, &cfg, &expl);

    double ev_solver = cfr_compute_policy_value(&g, st, 0, NULL);
    double ev_brute  = gm_brute_value(&g, st, (uint64_t)g.initial_state);
    double ev_p2     = cfr_compute_policy_value(&g, st, 1, NULL);

    printf("    solver value=%.5f brute value=%.5f LP=%.5f P2=%.5f expl(proxy)=%.6f\n",
           ev_solver, ev_brute, value, ev_p2, expl);
    CHECK_CLOSE(ev_solver, value, 1e-4, "CFR converges to exact LP value");
    CHECK_CLOSE(ev_brute,  value, 1e-4, "independent brute matches LP value");
    CHECK_CLOSE(ev_solver, ev_brute, 1e-6, "solver vs independent enumerate agree");
    CHECK_CLOSE(ev_p2, -value, 1e-4, "zero-sum: P2 value mirrors LP value");

    cfr_storage_destroy(st);
    printf(" PASSED\n");
    return 0;
}

int main(void)
{
    printf("Running Gambit exact-LP benchmark (ISSUE-09 #165)...\n");
    int failures = 0;
    failures += test_gambit_lp();
    if (failures == 0) { printf("All Gambit LP tests PASSED\n"); return 0; }
    printf("%d Gambit LP test(s) FAILED\n", failures);
    return 1;
}
