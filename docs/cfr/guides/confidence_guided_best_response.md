# Confidence-Guided Best Response

Issue #257. API: `include/poker_eval/solver/pe_br_sampling.h` (decision rule),
`include/poker_eval/solver/pe_external_best_response.h` (sampled best
response). Implementation: `src/solver/domain/br_sampling.c`,
`src/solver/domain/external_best_response.c`. Test: `tests/test_br_sampling.c`.
Benchmark: `scripts/benchmarks/bench_br_sampling.py`.

The sampled best response measures exploitability. When it reaches one of the
best-responder's decisions, it has to find the best action from noisy rollouts.
Historically it played **one rollout per action and took the maximum**. That
is cheap, but it is noisy near a tie, and it is biased upward because the
maximum of noisy draws overshoots.

Confidence-guided evaluation samples each action **until the best one is
resolved**. A clear decision stops after a handful of draws. A near tie gets
more, up to a hard cap. Only this measurement changes: CFR training, and
therefore the solved strategy, are untouched.

## This is the default (issue #274)

The one-rollout estimator's overshoot is a property of the *decision rule*,
not of the sample size: every decision takes the maximum of one noisy draw per
action, and averaging more trajectories estimates that same maximum. The
reported exploitability therefore has a floor that no `--br-samples` value
lowers. Measured on Kuhn poker, whose exact gap is `3/8`: the one-rollout
mean is `0.620` at 256 trajectories and still `0.615` at 4,096, while the
confidence-guided mean is `0.376`. `tests/test_br_default_bias.c` pins that.

Since issue #274 `pe_solver_config_default()` and
`pe_external_br_config_default()` enable the evaluation, so a bare sampled
measurement (both `pe-preflop-solve` and `mpf_run_with_metrics --lane-b`)
reports the bounded-bias estimate. This is a *measurement*, not training: the
cost is the adaptive row of the benchmark below (for Hold'em heads-up, 55,625
terminal evaluations against the one-rollout row's 11,244). `max_samples = 0` is the explicit opt-out
back to the historical one-rollout estimator, and a checkpoint made under one
estimator does not resume under the other.

## Selecting it

```c
cfg.br_sampling.max_samples = PE_BR_SAMPLING_DEFAULT_MAX_SAMPLES; /* 64; already the default */
cfg.br_sampling.max_samples = 0;         /* opt out: one rollout per action */
cfg.br_sampling.absolute_tolerance = 0;  /* optional */
```

```sh
pe-preflop-solve ... --br-max-samples 64 [--br-min-samples 4] [--br-confidence 0.95]
                     [--br-check-interval 4] [--br-absolute-tolerance 5]
mpf_run_with_metrics --lane-b ... --br-max-samples 64
```

| Setting | Default (when 0) | Meaning |
|---|---|---|
| `max_samples` | `PE_BR_SAMPLING_DEFAULT_MAX_SAMPLES` = 64 | draws per action to decide, hard cap; 0 selects the one-rollout estimator |
| `min_samples` | 4 (at least 2) | draws per action before the first look |
| `check_interval` | 4 | draws per surviving action between looks |
| `confidence` | 0.95 | of the whole decision |
| `absolute_tolerance` / `relative_tolerance` | 0 | a gap this small does not matter |

The settings enter the checkpoint compatibility hash only when the evaluation
is on, and as *resolved*: writing a default out explicitly (`--br-min-samples 4`
for the implicit 4) leaves the hash unchanged, so the same configuration
resumes. `pe_external_br_config_t::sampling` and `pe_solver_config_t::br_sampling`
enlarge public structs, so the solver library moves to SOVERSION 7.

## The decision rule

`pe_br_resolve_decision()` is reusable. It takes one sampler per action and
works as follows:

1. Every action gets `min_samples` draws.
2. At each look, an action whose upper confidence bound falls below the
   leader's lower bound is **eliminated** and never sampled again. Work goes
   only to actions that could still change the ordering.
3. The decision stops in one of three ways:
   - **separated**: only the leader survives;
   - **tolerance**: every survivor's upper bound is within the tolerance of
     the leader's lower bound, so the remaining gap cannot matter by more
     than that;
   - **max budget**: every survivor has `max_samples` draws. This case is
     counted as *unresolved*.
4. Otherwise each survivor gets `check_interval` more draws.

A scalar compared against a boundary `b` is the two-action case in which the
second action is `b` itself.

### Sequential validity

Looking again and again at an ordinary confidence interval overstates
confidence. Every half-width here is built at level
`1 − (1 − confidence) / (L × A)`, where `L` is the number of looks the budget
allows and `A` the number of actions. By a union bound, all of a decision's
intervals then hold together, at every look, with probability at least
`confidence`, each interval being the usual normal approximation.

This is conservative. The test measures it: over 4,000 decisions where one
action beats the other by 0.3 standard deviation, at 90% confidence, 3,895 are
resolved and **one** is wrong, against a bound of 400.

An empirical Bernstein confidence sequence would be tighter, but it needs a
known range for the values, and the callers here do not have one. The bound
lives in `pe_br_sequential_z()`, so a tighter one can replace it there.

### The reported value is re-estimated

Stopping when a decision looks resolved tends to stop just when the leader's
mean happens to be high. That mean is therefore biased upward, on top of the
bias of reporting a maximum. Once the action is chosen, the rule therefore
draws it `min_samples` more times, and reports the mean of those fresh draws.
Those draws played no part in the choice, so they are an unbiased estimate of
its value.

On the test's toy game, whose exact best-response value is 1.0 — the rows are
numbers `test_best_response()` prints and asserts:

| Estimate | Value |
|---|---|
| one rollout per action | 1.157 |
| running mean of the chosen action | 1.0497 |
| re-estimated | **1.006** |
| fixed 64 draws per action | 0.997 |

The running-mean row is `selection_value_sum / decisions` over the same 400
trajectories: the leader's mean at the moment it was chosen, which is exactly
what optional stopping inflates. It is the quantity the re-estimation
replaces, so the test asserts it stays above the re-estimated value rather
than merely quoting it.

The hard bound is therefore `actions × max_samples + min_samples` draws per
decision.

Draws are made in a fixed order, round by round and action by action, so a
seeded measurement is reproducible.

## Telemetry

Each sampled measurement reports `br_sampling terminal_evaluations=N`. With
the evaluation on, it also reports:

```text
br_decisions decisions=307 samples=6308 avg_samples=20.547 early_stop_pct=99.67
  separated=306 tolerance_stops=0 max_budget_hits=1 single_action=0
  eliminated=306 mean_gap=30.7709 mean_gap_half_width=11.1518
  mean_selection_value=14.0654 histogram=168,57,60,20,2,0,0,0
```

- `early_stop_pct`: decisions that ended before the cap;
- `max_budget_hits`: decisions still unresolved at the cap;
- `single_action`: decisions with one legal action, which had nothing to
  decide. With `separated` and `tolerance_stops` these four add up to
  `decisions`, so a reader can tell a decision that stopped early from one
  that never had a choice;
- `histogram`: decisions by draws per action, in buckets of `min_samples`,
  then ×2, ×4, and so on;
- `mean_gap` and `mean_gap_half_width`: the final best-versus-runner-up gap
  and its uncertainty;
- `mean_selection_value`: the mean of the running means the decisions were
  made on — the biased quantity the re-estimation replaces. A diagnostic for
  the bias, never an estimate of the best-response value.

`pe_external_br_result_t::sampling` carries the same totals.

## Benchmarks

`bench_br_sampling.py` solves four spots with full ranges:

- Hold'em heads-up;
- PLO4 heads-up;
- PLO5 heads-up;
- PLO4 three-way.

Each spot runs 500 iterations with 2,000 best-response trajectories per
player, over 5 seeds. For a given seed, every row sees the same solved
strategy, so the difference to the fixed-64 reference is paired, per seed.

| Spot | Measurement | BR terminal evals | Draws / decision | Wall (s) | Paired NashConv diff vs fixed (mBB) |
|---|---|---|---|---|---|
| Hold'em HU | one rollout | 11,244 | – | 0.12 | +785 ± 1,320 |
| | fixed 64 | 627,528 | 192.0 | 2.52 | reference |
| | adaptive | **55,625** | 15.7 | 0.28 | +239 ± 1,001 |
| | adaptive, tolerance 5 | 50,440 | 14.1 | 0.28 | −289 ± 1,340 |
| PLO4 HU | one rollout | 11,013 | – | 0.16 | −675 ± 775 |
| | fixed 64 | 583,445 | 192.0 | 2.41 | reference |
| | adaptive | **64,283** | 19.7 | 0.38 | −1,242 ± 1,687 |
| | adaptive, tolerance 5 | 53,928 | 16.2 | 0.35 | −1,101 ± 621 |
| PLO5 HU | one rollout | 11,008 | – | 0.16 | +328 ± 887 |
| | fixed 64 | 582,566 | 192.0 | 3.10 | reference |
| | adaptive | **64,289** | 19.7 | 0.46 | −975 ± 998 |
| | adaptive, tolerance 5 | 53,847 | 16.2 | 0.43 | −462 ± 1,232 |
| PLO4 3-way | one rollout | 17,504 | – | 0.44 | +2,267 ± 1,101 |
| | fixed 64 | 1,063,340 | 192.0 | 10.03 | reference |
| | adaptive | **159,889** | 27.9 | 1.93 | +375 ± 1,230 |
| | adaptive, tolerance 5 | 126,121 | 21.7 | 1.58 | −1,363 ± 2,146 |

Everything but the wall-clock column is deterministic for a given seed, and a
second machine reproduced the table exactly — every terminal-evaluation
count, every draws-per-decision figure and every paired difference to the
decimal. The wall column is machine-dependent and indicative only.

- **Cost.** Against a fixed budget of 64 rollouts per action, the adaptive
  measurement makes **9–11× fewer terminal evaluations** heads-up (6.6×
  three-way, where more decisions are close), and 95–100% of decisions stop
  before the cap. Wall clock follows the same work but is not a property of
  the feature: the authoring machine ran 5–9× faster, the second one
  4.5–5.1×, with the evaluation counts identical.
- **Accuracy.** Its paired difference from the fixed-64 measurement stays
  within the measurement's own noise: the ± above is the spread of the
  per-seed difference, dominated by the 2,000 sampled trajectories. No
  systematic shift is detectable.
- **Tolerance trades accuracy for cost.** It stops as soon as the remaining
  gap is under 5 chips, even if that settles on a marginally worse action. In
  PLO4 the resulting underestimate (−1,101 ± 621 over 5 seeds) is visible.
  Leave the tolerance at 0 when the measurement must match a fixed budget.
- **The one-rollout estimate** is biased upward when there is more to take
  the maximum of, most visibly three-way (+2,267 ± 1,101).

## Scope

- **CFR/MCCFR training** is unchanged: this module only runs inside the
  best-response measurement.
- **`best_response_ii.c`**, the vector information-set best response, is
  deterministic: it enumerates chance and iterates to a fixed point, with no
  Monte Carlo draws to stop early, so it has nothing to adapt.
- **`mpf_run_with_metrics --lane-b`** accepts the settings and then measures
  a best response at the final iteration. Its sampled solves
  are still subject to the multiway postflop adapter's non-re-entrant state
  cache (see `adaptive_variance_sampling.md`).
