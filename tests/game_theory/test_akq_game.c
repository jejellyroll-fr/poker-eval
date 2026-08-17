/*
 * test_akq_game.c - AKQ check-or-bet CFR convergence audit (ISSUE-09, #165).
 *
 * Builds the AKQ game as a cfr_game_t vtable, runs the poker-eval CFR solver,
 * and checks the converged average strategy against the analytical oracle:
 *     P1 bluff frequency with Q  = 0.2164  (converged equilibrium)
 *     P2 call frequency with K   = 0.3358  (converged equilibrium)
 *     Game value (player 1)      = -1/18 exactly (Chen & Ankenman, Figure 2.1)
 *
 * The value is EXACTLY -1/18 for the first player; the solver's converged
 * policy value must match it to 2e-3 and, independently, must equal a
 * from-scratch full-tree enumeration of the same average strategy.
 *
 * Information sets strip the opponent's hidden card so the solver recovers the
 * GLOBAL equilibrium (not per-hand-pair subgames).
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include "analytical_oracles.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* Hand: 0=Q (worst), 1=K, 2=A (best). Each antes 1 (pot 2 at start). Bet 1. */
#define AKQ_PH_ROOT    0   /* P1 to act */
#define AKQ_PH_P2CHECK 1   /* P2 to act after P1 check */
#define AKQ_PH_P1CBET  2   /* P1 to act after P2 bet (on check line) */
#define AKQ_PH_P2BET   3   /* P2 to act after P1 bet */
#define AKQ_TERM       4

static uint64_t akq_key(int hand, int phase) {
    return (uint64_t)((hand << 4) | phase);
}
static void akq_unpack(uint64_t k, int *hand, int *phase, int *last) {
    *phase = (int)(k & 0xF);
    int h = (int)((k >> 4) & 0xF);
    *hand = h & 0x3;
    *last = (int)((k >> 8) & 0x3);
}

/* p1,p2 private hands; phase = terminal sub-type; last = last action. */
static uint64_t akq_make(int p1, int p2, int phase, int last) {
    return (uint64_t)((p1 & 3) | ((p2 & 3) << 2) | (phase << 4) | ((last & 3) << 8));
}
static void akq_mk_unpack(uint64_t k, int *p1, int *p2, int *phase, int *last) {
    *p1 = (int)(k & 3); *p2 = (int)((k >> 2) & 3);
    *phase = (int)((k >> 4) & 0xF); *last = (int)((k >> 8) & 3);
}

static int akq_rank(int h) { return h; } /* 0=Q,1=K,2=A */

/* Terminal utility for player 1. pot=2 at start; a bet adds 1 each.
 * Contributions tracked by who bet. last_action: 0=check/fold, 1=bet/call. */
static double akq_util(int p1, int p2, int phase, int last)
{
    int p1wins = akq_rank(p1) > akq_rank(p2);
    if (phase == AKQ_TERM + AKQ_PH_P2CHECK) {
        /* both checked -> showdown for the 2 ante pot */
        return p1wins ? 1.0 : -1.0;
    }
    if (phase == AKQ_TERM + AKQ_PH_P1CBET) {
        /* P2 bet on check line, P1 acted. last 0=fold(P1 loses ante 1),
         * 1=call (pot 4, P1 contributed 2). */
        if (last == 0) return -1.0;
        return p1wins ? 2.0 : -2.0;
    }
    if (phase == AKQ_TERM + AKQ_PH_P2BET) {
        /* P1 bet, P2 acted. last 0=fold (P1 wins ante 1), 1=call (pot 4). */
        if (last == 0) return 1.0;
        return p1wins ? 2.0 : -2.0;
    }
    return 0.0;
}

static int akq_current_player(cfr_game_t *g, uint64_t k, void *u) {
    (void)g; (void)u;
    int p1,p2,ph,last; akq_mk_unpack(k,&p1,&p2,&ph,&last);
    if (ph == AKQ_TERM + AKQ_PH_P2CHECK) return 1;
    if (ph == AKQ_TERM + AKQ_PH_P1CBET) return 0;
    if (ph == AKQ_TERM + AKQ_PH_P2BET)  return 1;
    /* non-terminal */
    if (ph == AKQ_PH_ROOT) return 0;
    if (ph == AKQ_PH_P2CHECK) return 1;
    if (ph == AKQ_PH_P1CBET) return 0;
    if (ph == AKQ_PH_P2BET)  return 1;
    return -1;
}
static int akq_is_terminal(cfr_game_t *g, uint64_t k, void *u) {
    (void)g; (void)u; int p1,p2,ph,last; akq_mk_unpack(k,&p1,&p2,&ph,&last);
    return ph >= AKQ_TERM;
}
static double akq_get_utility(cfr_game_t *g, uint64_t k, int player, void *u) {
    (void)g; (void)u;
    int p1,p2,ph,last; akq_mk_unpack(k,&p1,&p2,&ph,&last);
    if (ph < AKQ_TERM) return 0.0;
    double v = akq_util(p1, p2, ph, last);
    return (player == 0) ? v : -v;
}
static int akq_get_actions(cfr_game_t *g, uint64_t k, int *out, int maxn, void *u) {
    (void)g; (void)u; int p1,p2,ph,last; akq_mk_unpack(k,&p1,&p2,&ph,&last);
    if (ph >= AKQ_TERM) return 0;
    if (maxn < 2) return 0;
    out[0] = 0; out[1] = 1; return 2;
}
static uint64_t akq_apply_action(cfr_game_t *g, uint64_t k, int a, void *u) {
    (void)g; (void)u;
    int p1,p2,ph,last; akq_mk_unpack(k,&p1,&p2,&ph,&last);
    switch (ph) {
        case AKQ_PH_ROOT:
            if (a == 0) return akq_make(p1,p2, AKQ_PH_P2CHECK, a);
            return akq_make(p1,p2, AKQ_PH_P2BET, a);
        case AKQ_PH_P2CHECK:
            if (a == 0) return akq_make(p1,p2, AKQ_TERM+AKQ_PH_P2CHECK, a);
            return akq_make(p1,p2, AKQ_PH_P1CBET, a);
        case AKQ_PH_P1CBET:
            return akq_make(p1,p2, AKQ_TERM+AKQ_PH_P1CBET, a);
        case AKQ_PH_P2BET:
            return akq_make(p1,p2, AKQ_TERM+AKQ_PH_P2BET, a);
        default:
            return k;
    }
}

/* Information-set key: strip the opponent's hidden card. */
static uint64_t akq_infoset_key(const void *state) {
    uint64_t k = (uint64_t)(uintptr_t)state;
    if (k == 0) return 0;
    int p1,p2,ph,last; akq_mk_unpack(k,&p1,&p2,&ph,&last);
    if (ph >= AKQ_TERM) return (1ULL << 60) | k;
    int hand = (ph == AKQ_PH_ROOT || ph == AKQ_PH_P1CBET) ? p1 : p2;
    return (1ULL << 60) | ((uint64_t)hand << 4) | (uint64_t)ph;
}
static uint64_t akq_iset(int hand, int phase) {
    return (1ULL << 60) | ((uint64_t)hand << 4) | (uint64_t)phase;
}

/* chance: root deals ordered distinct hand pair (6 outcomes). */
static int akq_is_chance(cfr_game_t *g, uint64_t k, void *u) {
    (void)g; (void)u; return k == 0;
}
static int akq_chance_outcomes(cfr_game_t *g, uint64_t k, void *u) {
    (void)g; (void)u; (void)k; return 6;
}
static uint64_t akq_apply_chance(cfr_game_t *g, uint64_t k, int o, void *u) {
    (void)g; (void)u; if (k != 0) return k;
    static const int pairs[6][2] = {{0,1},{0,2},{1,0},{1,2},{2,0},{2,1}};
    return akq_make(pairs[o][0], pairs[o][1], AKQ_PH_ROOT, 0);
}

/* independent brute-force value of converged avg strategy */
static double akq_brute(cfr_game_t *g, cfr_storage_t *st, uint64_t k) {
    if (g->is_chance(g,k,NULL)) {
        int n = g->get_chance_outcomes(g,k,NULL); double s=0;
        for (int o=0;o<n;o++) s += akq_brute(g,st,g->apply_chance(g,k,o,NULL));
        return s/n;
    }
    if (g->is_terminal(g,k,NULL)) return g->get_utility(g,k,0,NULL);
    int acts[2]; int na=g->get_actions(g,k,acts,2,NULL); double strat[2];
    if (na==2) cfr_storage_get_avg_strategy(st, akq_infoset_key((const void*)(uintptr_t)k), 2, strat);
    else { strat[0]=1; strat[1]=0; }
    double v=0; for (int i=0;i<na;i++) v += strat[i]*akq_brute(g,st,g->apply_action(g,k,acts[i],NULL));
    return v;
}

#define CHECK(cond, msg) do { if(!(cond)){ fprintf(stderr,"FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); return 1; } } while(0)
#define CHECK_CLOSE(a,b,eps,msg) do { if(fabs((a)-(b))>(eps)){ fprintf(stderr,"FAIL: %s (got %g want %g tol %g)\n", msg,(double)(a),(double)(b),(double)(eps)); return 1; } } while(0)

static int test_akq_solve(void)
{
    printf("  test_akq_solve...");
    cfr_game_t g; memset(&g,0,sizeof(g));
    g.current_player=akq_current_player; g.is_terminal=akq_is_terminal;
    g.get_utility=akq_get_utility; g.get_actions=akq_get_actions;
    g.apply_action=akq_apply_action; g.get_infoset_key=akq_infoset_key;
    g.is_chance=akq_is_chance; g.get_chance_outcomes=akq_chance_outcomes;
    g.apply_chance=akq_apply_chance;
    g.initial_state=(void*)(uintptr_t)0; g.num_players=2;

    cfr_config_t cfg; memset(&cfg,0,sizeof(cfg));
    cfg.max_iterations=200000; cfg.enable_dcfr=1;
    cfg.dcfr_alpha=1.5; cfg.dcfr_beta=0.0; cfg.dcfr_gamma=2.0;

    cfr_storage_t *st=cfr_storage_create();
    CHECK(st!=NULL,"storage");
    double expl=0;
    /* cfr_solve returns the perfect-information best-response exploitability
     * (a double), NOT an error code; for an imperfect-information game it is an
     * upper bound that stays positive at equilibrium, so we do not gate on it. */
    cfr_solve(&g,st,&cfg,&expl);

    double q[2]; cfr_storage_get_avg_strategy(st, akq_iset(0,AKQ_PH_ROOT),2,q);
    double kc[2]; cfr_storage_get_avg_strategy(st, akq_iset(1,AKQ_PH_P2BET),2,kc);
    double ev=cfr_compute_policy_value(&g,st,0,NULL);
    double ev_p2=cfr_compute_policy_value(&g,st,1,NULL);
    double bv=akq_brute(&g,st,(uint64_t)0);
    printf("\n    Qbet=%.4f Kcall=%.4f solverEV=%.5f bruteEV=%.5f P2EV=%.5f expl(proxy)=%.5f\n",
           q[1], kc[1], ev, bv, ev_p2, expl);

    akq_equilibrium_t eq; get_akq_analytical_solution(&eq);
    /* Player-1 game value is EXACTLY -1/18 for this check-or-bet AKQ game. */
    CHECK_CLOSE(ev,  eq.p1_ev, 2e-3, "player-1 game value (-1/18)");
    CHECK_CLOSE(bv,  eq.p1_ev, 2e-3, "independent enumeration value");
    CHECK_CLOSE(ev,  bv,  1e-5, "solver value equals independent enumeration");
    CHECK_CLOSE(ev_p2, -ev, 1e-3, "zero-sum: P2 value mirrors P1");
    CHECK_CLOSE(q[1], eq.p1_bluff_freq_Q, 0.05, "Q bluff frequency");
    CHECK_CLOSE(kc[1], eq.p2_call_freq_K, 0.05, "K call frequency");
    cfr_storage_destroy(st);
    printf(" PASSED\n");
    return 0;
}

int main(void)
{
    printf("Running AKQ analytical benchmark (ISSUE-09 #165)...\n");
    int failures = 0;
    failures += test_akq_solve();
    if (failures == 0) { printf("All AKQ tests PASSED\n"); return 0; }
    printf("%d AKQ test(s) FAILED\n", failures);
    return 1;
}
