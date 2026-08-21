/*
 * analytical_oracles.h - Analytical reference oracles for the CFR
 * convergence audit (ISSUE-09, #165).
 *
 * These are INDEPENDENT reference solutions: closed-form Chen & Ankenman
 * numbers, plus exact sequence-form LP oracles implemented separately from
 * the poker-eval CFR solver (see test_gambit_exact_lp.c). Together they form
 * the "layer of truth" against which the solver is qualified.
 */

#ifndef POKER_EVAL_ANALYTICAL_ORACLES_H
#define POKER_EVAL_ANALYTICAL_ORACLES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* AKQ (Chen & Ankenman, Figure 2.1)                                   */
/* Deck A>K>Q, ante 1 each -> pot 2, bet B=1. Zero-sum, pot units.     */
/* check-or-bet game: the first player's value is EXACTLY -1/18.       */
/* ------------------------------------------------------------------ */
typedef struct {
    double p1_bluff_freq_Q; /* P1 bets a Q as a bluff (converged)  */
    double p2_call_freq_K;  /* P2 calls a K (converged)            */
    double p1_ev;           /* Exact closed form: -1/18 (pot units) */
    double p2_ev;           /* Exact closed form: +1/18            */
} akq_equilibrium_t;

/* Fill *out with the analytical AKQ equilibrium (exact, no solver). */
void get_akq_analytical_solution(akq_equilibrium_t *out);

/* ------------------------------------------------------------------ */
/* Kuhn poker (2p) exact equilibrium value                            */
/* Deck {J,Q,K}, ante 1, bet 1. The benchmark game (test_kuhn_openspiel)
 * uses the first-action check-or-bet subgame whose value is -1/18 for
 * the first player (the negated Chen & Ankenman AKQ value, the two
 * orderings are mirrors).                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    double p1_ev; /* Exact Kuhn value for player 1 (zero-sum): -1/18 */
    double p2_ev; /* Exact Kuhn value for player 2: +1/18 */
} kuhn_equilibrium_t;

void get_kuhn_analytical_solution(kuhn_equilibrium_t *out);

/* ------------------------------------------------------------------ */
/* Additional Chen & Ankenman-style benchmark oracles                 */
/* ------------------------------------------------------------------ */
typedef struct {
    double weak_jam_freq;
    double caller_call_freq;
    double p1_ev;
} jam_or_fold_equilibrium_t;

void get_jam_or_fold_analytical_solution(jam_or_fold_equilibrium_t *out);

/* Continuous action benchmark.  The production CFR interface is finite,
 * therefore the test samples [0, 1] at a uniform grid.  For u(x,y)=x-y the
 * continuous game value and every finite-grid value are exactly zero. */
typedef struct {
    double value;
    double lower_endpoint;
    double upper_endpoint;
} continuous_equilibrium_t;

void get_continuous_analytical_solution(continuous_equilibrium_t *out);

/* The two-street matrix benchmark uses A = {{2,-1},{-1,1}}, whose value is
 * 1/5.  Independent streets therefore have value 2/5. */
double get_two_street_analytical_value(void);

/* ------------------------------------------------------------------ */
/* Sequence-form LP exact solver (Gambit-style oracle)                 */
/* Solves a two-player zero-sum extensive game given its sequence form */
/* payoff matrix A (player 1's utility) and sequence-count vectors.    */
/* Returns the exact game value for player 1.                          */
/* ------------------------------------------------------------------ */

/* Solve min_x max_y  x^T A y  s.t. sum x = 1, x>=0, sum y = 1, y>=0  */
/* via the equivalent LP:
 *     max  v
 *     s.t. A y - 1*v <= 0        (player-1 best response constraint)
 *          sum y = 1, y >= 0
 * then v is the game value for player 1 (by symmetry of the support).
 * A is m x n (m = #P1 sequences facing, n = #P2 sequences). We solve the
 * primal LP with a small dense simplex. Returns 0 on success.
 */
int seqform_lp_solve(const double *A, int m, int n, double *out_value,
                     double *out_x, double *out_y);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ANALYTICAL_ORACLES_H */
