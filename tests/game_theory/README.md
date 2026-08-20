# Analytical Game Benchmark Suite & External Oracles (ISSUE-09, #165)

This directory is the **mathematical qualification layer** for the poker-eval CFR
core. It solves small, analytically-characterized games and checks the solver
against *independent* oracles that are **not** derived from the solver itself.

Standalone reference oracles (`analytical_oracles.[ch]`):

- **Chen & Ankenman AKQ / Kuhn values** – exact closed-form equilibria
  (the implemented check-or-bet games have first-player value exactly `-1/18`).
- **Sequence-form / normal-form exact LP** (`seqform_lp_solve`) – a self-contained
  dense two-phase simplex that returns the *exact* zero-sum game value with
  double precision. It works for arbitrarily-signed games (it internally shifts
  the payoff matrix so the simplex stays feasible) and is cross-checked against a
  brute-force minimax grid in `test_gambit_exact_lp`.
- **OpenSpiel 2.0.2 multiway Kuhn** – exact rational EV and NashConv fixtures
  generated from uniform `TabularPolicy` profiles for
  `kuhn_poker(players=3)` and `kuhn_poker(players=4)`. The in-tree game mirrors
  OpenSpiel's `N+1`-card deck and betting termination rules, while remaining
  dependency-free in CI.

## Tests

| Test | Game | Oracle asserted |
|------|------|-----------------|
| `test_akq_game` | AKQ check-or-bet (`A>K>Q`, ante 1 → pot 2, bet 1) | CFR policy value → `-1/18` ± 2e-3, `solver == independent enumeration`, zero-sum mirror, converged bluff/call frequencies |
| `test_kuhn_openspiel` | 2-player first-action Kuhn subgame (`J<Q<K`) | CFR policy value → `-1/18` ± 2e-3, `solver == independent enumeration`, zero-sum mirror |
| `test_kuhn_multiway_openspiel` | OpenSpiel-compatible Kuhn (3p/4p) | Uniform-policy EVs, per-player unilateral improvements, and NashConv match OpenSpiel 2.0.2 exactly |
| `test_leduc_openspiel` | Leduc Hold'em (6 cards, 2 rounds, max 2 raises) | `solver == independent full-tree enumeration` ± 1e-3, zero-sum mirror, >100 infosets (proves the full chance tree is traversed) |
| `test_gambit_exact_lp` | 2×2 simultaneous matrix game (+ an exact sequence-form LP oracle) | LP exact value `0.2`; CFR policy value → `0.2` ± 1e-4; solver == brute minimax; zero-sum mirror |
| `test_best_response_exploitability` (ISSUE-12, #168) | Kuhn 2p, 3-player Kuhn poker, perfect-information Gambit game | Engine `cfr_best_response_value`/`cfr_exploitability` (and multiway variants) equal an independent recursive oracle; deliberately exploitable policies (Always Fold → BR=+1, Always Call → BR=+0.5) match closed forms; Kuhn 2p policy value converges to `-1/18` over iterations; 3-player Kuhn NashConv == independent oracle; exact Gambit-LP equilibrium → exploitability `e < 1e-7` |

Every test builds the game as a `cfr_game_t` vtable, runs the real
`cfr_solve`, then verifies:

1. The converged **policy value equals an independent full-tree enumeration**
   (`solver == brute`), i.e. internal algorithmic consistency.
2. The value matches the **exact analytical/LP oracle** (the mathematical truth).
3. Zero-sum property: P2's converged value mirrors P1's.

## On exploitability (important)

`cfr_solve` returns the *perfect-information best-response* exploitability
(a `double`, not an error code). For an imperfect-information game (every poker
game here) that value is an **upper bound that stays positive at equilibrium**,
because the best response is allowed to see the opponent's hidden card/action.
The tests therefore **do not** gate on exploitability `< ε`; they gate on
policy-value agreement with the independent oracle, which is exact.

`test_best_response_exploitability` additionally validates the *perfect-
information* exploitability engine itself. Because `cfr_exploitability()` is a
perfect-information best response, it is an upper bound that stays positive at
equilibrium for imperfect-information games (AKQ, Kuhn, Leduc). To verify the
engine reports `e(sigma) ~ 0` at an exact equilibrium, that test uses a
perfect-information sequential game (the perfectly-observable 2×2 Gambit
sequence-form game) where the perfect-information exploitability equals the
true exploitability and must be `< 1e-7` at the locked exact equilibrium.

## Build & run

These are regular CTest tests, labelled `engine;cfr;game_theory;feat09` and
marked `sanitizer-skip`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build -L game_theory --output-on-failure
```

No external dependency (OpenSpiel / Gambit binaries) is assumed: the "External
Oracles" are re-implemented self-contained *reference solvers* so the suite runs
anywhere with just a C compiler and the built poker-eval library.
