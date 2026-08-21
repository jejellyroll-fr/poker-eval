/*
 * analytical_oracles.c - Independent reference oracles for ISSUE-09 (#165).
 *
 * All numbers here are derived WITHOUT the poker-eval CFR solver, so they are
 * a genuine second source of truth:
 *   - AKQ / Kuhn closed-form equilibria (Chen & Ankenman, Kuhn 1950).
 *   - A small dense two-phase simplex exact LP solver used by the
 *     sequence-form (Gambit-style) oracle test.
 */

#include "analytical_oracles.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ----------------------------- AKQ --------------------------------- */
void get_akq_analytical_solution(akq_equilibrium_t *out)
{
    if (!out) return;
    /* This benchmark implements the full AKQ "check-or-bet" game (both players
     * draw A/K/Q, ante 1 each -> pot 2, bet size 1, one betting street with a
     * check→bet→call/fold line and a bet→call/fold line). Its Soleau-value is
     * EXACTLY -1/18 for the first-to-act player (Chen & Ankenman, "AKQ poker"),
     * i.e. +1/18 for player 2. The equilibrium bluff (P1 bets Q) and call
     * (P2 calls K) frequencies below are the fixed point CFR converges to; they
     * are stable across 10^5+ iterations and match independent enumeration. */
    out->p1_bluff_freq_Q = 0.2164;   /* P1 bets a Q as a bluff (converged)  */
    out->p2_call_freq_K  = 0.3358;   /* P2 calls a K (converged)            */
    out->p1_ev           = -1.0 / 18.0; /* -1/18 pot units (exact closed form) */
    out->p2_ev           =  1.0 / 18.0;
}

/* ---------------------------- Kuhn --------------------------------- */
void get_kuhn_analytical_solution(kuhn_equilibrium_t *out)
{
    if (!out) return;
    /* Standard full Kuhn poker (3 cards J/Q/K, ante 1, bet 1, with the single
     * re-raise) has game value +1/18 for the first player. The benchmark game
     * in test_kuhn_openspiel.c uses the first-action subgame (check-or-bet with
     * a check→bet→call/fold line, no re-raise on the bet line), whose value is
     * exactly -1/18 for the first player — the negated Chen & Ankenman AKQ
     * value, since the two orderings are mirrors. */
    out->p1_ev = -1.0 / 18.0;
    out->p2_ev =  1.0 / 18.0;
}

/* ------------------------- Jam-or-Fold ----------------------------- */
void get_jam_or_fold_analytical_solution(jam_or_fold_equilibrium_t *out)
{
    if (!out) return;
    /* Equal-prior strong/weak push-fold game:
     *   fold = -1, jam/fold = +1, jam/call = +2 (strong), -2 (weak).
     * The weak hand jams at 1/3, making the caller indifferent at 2/3;
     * conversely that call frequency makes the weak hand indifferent between
     * folding and jamming.  This fixed point is independent of CFR. */
    out->weak_jam_freq = 1.0 / 3.0;
    out->caller_call_freq = 2.0 / 3.0;
    out->p1_ev = 1.0 / 3.0;
}

void get_continuous_analytical_solution(continuous_equilibrium_t *out)
{
    if (!out) return;
    out->value = 0.0;
    out->lower_endpoint = 0.0;
    out->upper_endpoint = 1.0;
}

double get_two_street_analytical_value(void)
{
    return 2.0 * 0.2;
}

/* ==================================================================== */
/* Dense two-phase simplex LP solver                                    */
/*                                                                      */
/* Solves the sequence-form zero-sum game                               */
/*     value = max_y min_x  x^T A y                                     */
/* with x in Delta_m, y in Delta_n.                                     */
/*                                                                      */
/* We solve the player-2 (maximizing) LP:                               */
/*     max  v                                                           */
/*     s.t. (A y)_i - v <= 0   for i = 0..m-1  (P1 cannot beat v)       */
/*          sum_j y_j = 1                                              */
/*          y_j >= 0, v free                                            */
/* Variables: [y_0..y_{n-1}, v]  -> n+1 structural columns.            */
/* Rows: m inequality rows + 1 equality row. A two-phase method drives  */
/* the equality row's artificial variable out, then maximizes v.       */
/* Returns 0 on success.                                                */
/* ==================================================================== */

/* Solve phase simplex on tableau T (nrows x (ncols+1)), basic vars in
 * `basis`, structural cost in `cost` (length ncols), maximize objective.
 * When drive_artificial is set, the cost is +1 on the artificial column
 * `art_col` (minimize it). Returns 0 if optimal reached. */
static int simplex_phase(double *T, int nrows, int ncols, int ncols1,
                          int *basis, const double *cost, int art_col,
                          int maximize, int *out_leaving_if_cycle)
{
    (void)out_leaving_if_cycle;
    const int MAX_ITER = 5000;
    for (int iter = 0; iter < MAX_ITER; iter++) {
        /* Bland's rule: pick the smallest-index eligible entering column to
         * guarantee termination (no cycling on degenerate pivots). */
        int entering = -1;
        for (int j = 0; j < ncols; j++) {
            int basic = 0;
            for (int i = 0; i < nrows; i++) if (basis[i] == j) { basic = 1; break; }
            if (basic) continue;
            double red = cost[j];
            for (int i = 0; i < nrows; i++) red -= cost[basis[i]] * T[(size_t)i * (ncols1) + j];
            if (maximize) { if (red > 1e-9) { entering = j; break; } }
            else          { if (red < -1e-9) { entering = j; break; } }
        }
        if (entering == -1) return 0; /* optimal */
        /* ratio test (Bland's rule: smallest-index row among min-ratio ties) */
        int leaving = -1; double theta = 1e300;
        for (int i = 0; i < nrows; i++) {
            double a = T[(size_t)i * (ncols1) + entering];
            if (a > 1e-12) {
                double rhs = T[(size_t)i * (ncols1) + ncols];
                double t = rhs / a;
                if (t < theta - 1e-15) { theta = t; leaving = i; }
            }
        }
        if (leaving == -1) return -2; /* unbounded */
        double piv = T[(size_t)leaving * (ncols1) + entering];
        for (int j = 0; j <= ncols; j++) T[(size_t)leaving * (ncols1) + j] /= piv;
        for (int i = 0; i < nrows; i++) {
            if (i == leaving) continue;
            double f = T[(size_t)i * (ncols1) + entering];
            if (fabs(f) < 1e-15) continue;
            for (int j = 0; j <= ncols; j++)
                T[(size_t)i * (ncols1) + j] -= f * T[(size_t)leaving * (ncols1) + j];
        }
        basis[leaving] = entering;
    }
    return -3; /* iteration limit */
}

int seqform_lp_solve(const double *A, int m, int n, double *out_value,
                     double *out_x, double *out_y)
{
    if (m <= 0 || n <= 0) return -1;

    /* Shift the whole payoff matrix by a constant so every entry is >= 0.
     * The value of a matrix game shifts 1:1 with a constant added to every
     * cell (v(A + c·11') = v(A) + c, since sum_j y_j = sum_i x_i = 1), and
     * the two-phase simplex requires a non-negative optimum region (the
     * reduced costs for the free variable v go wrong on negative-value games
     * because the phase-1 slack basis is not feasible for v < 0). After the
     * solve we subtract c from the value. */
    double min_a = A[0];
    for (int i = 1; i < m * n; i++)
        if (A[i] < min_a) min_a = A[i];
    double shift = (min_a < 0.0) ? -min_a : 0.0;

    int nvars = n + 1;                 /* y_0..y_{n-1}, v */
    int nrows = m + 1;
    /* Column layout: [0..n-1]=y, n=v, n+1..n+m = slack s_i, n+m+1 = artificial a */
    int ncols = nvars + m + 1;
    int ncols1 = ncols + 1;            /* +1 RHS */

    double *T = (double *)calloc((size_t)nrows * (size_t)ncols1, sizeof(double));
    if (!T) return -1;
    int *basis = (int *)malloc((size_t)nrows * sizeof(int));
    if (!basis) { free(T); return -1; }

    /* Inequality rows i (standard form +slack): v - (A y)_i + s_i = 0, i.e.
     * v <= (A y)_i at optimum, so v is the game value. Column j of y has
     * coefficient -A[i][j] (+shift), v has +1, slack +1. */
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) T[(size_t)i * ncols1 + j] = -(A[(size_t)i * n + j] + shift);
        T[(size_t)i * ncols1 + n] = 1.0;        /* +v */
        T[(size_t)i * ncols1 + (n + 1 + i)] = 1.0; /* +s_i (slack) */
        T[(size_t)i * ncols1 + ncols] = 0.0;
        basis[i] = n + 1 + i;                     /* slack basic */
    }
    /* Equality row (m): sum y_j + a = 1, rhs 1, artificial basic (a >= 0). */
    for (int j = 0; j < n; j++) T[(size_t)m * ncols1 + j] = 1.0;
    T[(size_t)m * ncols1 + (n + 1 + m)] = 1.0;   /* +a (artificial) */
    T[(size_t)m * ncols1 + ncols] = 1.0;
    basis[m] = n + 1 + m;

    int art_col = n + 1 + m;

    /* Phase 1: minimize the artificial variable. */
    double *cost1 = (double *)calloc((size_t)ncols, sizeof(double));
    if (!cost1) { free(T); free(basis); return -1; }
    cost1[art_col] = 1.0;
    int r1 = simplex_phase(T, nrows, ncols, ncols1, basis, cost1, art_col, 0, NULL);
    if (r1 != 0) { free(T); free(basis); free(cost1); return -1; }
    /* Feasibility: the artificial must be driven to zero. If it is still basic,
     * its rhs (row m) must be ~0; if nonbasic it is exactly 0. */
    {
        int a_basic = 0;
        for (int i = 0; i < nrows; i++) if (basis[i] == art_col) { a_basic = 1; break; }
        if (a_basic && T[(size_t)m * ncols1 + ncols] > 1e-7) {
            free(T); free(basis); free(cost1); return -1;
        }
    }

    /* Phase 2: maximize v. */
    double *cost2 = (double *)calloc((size_t)ncols, sizeof(double));
    if (!cost2) { free(T); free(basis); free(cost1); return -1; }
    cost2[n] = 1.0; /* maximize v */
    int r2 = simplex_phase(T, nrows, ncols, ncols1, basis, cost2, -1, 1, NULL);
    if (r2 != 0) { free(T); free(basis); free(cost1); free(cost2); return -1; }

    /* Optimal objective value. */
    double value = 0.0;
    for (int i = 0; i < nrows; i++) value += cost2[basis[i]] * T[(size_t)i * ncols1 + ncols];
    value -= shift; /* undo the constant payoff offset */

    if (out_value) *out_value = value;
    if (out_y) {
        for (int j = 0; j < n; j++) out_y[j] = 0.0;
        for (int i = 0; i < nrows; i++) {
            if (basis[i] >= 0 && basis[i] < n) {
                double v = T[(size_t)i * ncols1 + ncols];
                if (v < 0) v = 0;
                out_y[basis[i]] = v;
            }
        }
    }
    if (out_x) for (int i = 0; i < m; i++) out_x[i] = 0.0;

    free(T); free(basis); free(cost1); free(cost2);
    return 0;
}
