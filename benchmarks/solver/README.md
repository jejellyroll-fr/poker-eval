# Solver benchmark corpus

This directory is the reproducible benchmark/validation layer for the sampled
solver. It deliberately **does not implement solver logic**: every case runs
the product-facing `pe-preflop-solve` binary with explicit inputs, captures its
native JSON report and enriches it with timing, memory, stop-cause and
per-street coverage metrics.

The benchmark trees are reused from `examples/`; there are no benchmark-only
copies that can drift from the examples used by Studio and the CLI.

## Quick start

Build the solver:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_BINDINGS=OFF \
  -DBUILD_GPU=OFF \
  -DENABLE_OPENMP=OFF
cmake --build build --target pe-preflop-solve --parallel
```

Run the small cross-variant structural smoke suite:

```bash
python3 benchmarks/solver/run_benchmarks.py \
  --suite smoke \
  --iterations 64 \
  --repeat 2 \
  --check-reproducibility \
  --strict
```

Run the longer Hold'em learning probe used by CI:

```bash
python3 benchmarks/solver/run_benchmarks.py \
  --case holdem_flop \
  --iterations 10000 \
  --repeat 2 \
  --check-reproducibility \
  --strict
```

CI additionally requires every learning-probe run to contain at least one
`non_uniform_rows` entry, so a deterministic regression that leaves every
strategy at the initial uniform policy cannot pass merely by reproducing the
same broken result twice.

Run the whole cross-variant corpus with its checked-in iteration budget:

```bash
python3 benchmarks/solver/run_benchmarks.py --suite standard --strict
```

Run a longer qualification pass without editing the manifest:

```bash
python3 benchmarks/solver/run_benchmarks.py \
  --suite standard \
  --iterations 50000 \
  --output-dir build/solver-benchmarks-50k
```

List cases:

```bash
python3 benchmarks/solver/run_benchmarks.py --suite standard --list
```

A specific case can be selected with `--case`, for example:

```bash
python3 benchmarks/solver/run_benchmarks.py \
  --case plo6_river \
  --iterations 10000
```

## Corpus

`cases.json` covers:

| Variant | preflop | flop | turn | river | preflop -> river |
|---|---:|---:|---:|---:|---:|
| NLHE | yes | yes | yes | yes | yes |
| PLO4 | yes | yes | yes | yes | yes |
| PLO5 | yes | yes | yes | yes | yes |
| PLO6 | yes | yes | yes | yes | yes |

The street trees under `examples/plo_hu_streets/` encode betting geometry,
street, board snapshots and stacks; the number of private cards is selected by
`--game`, not by the tree. PLO4 therefore intentionally reuses the PLO5 tree
shape. The Hold'em turn/river coverage cases reuse the same legal pot-sized
betting geometry. Hold'em's full-tree case uses its native NLHE example.

Full multi-street cases use the `large` board abstraction so a qualification
run has a bounded state space. Single-street cases keep exact boards.

## Reproducibility contract

The default baseline is intentionally conservative:

- `external-mccfr`;
- `cpu_ref`;
- one CPU thread;
- `f64`;
- deterministic seed `20260906`;
- `--target-mbb 0`, so a benchmark always runs to its iteration budget;
- explicit memory and description-table budgets;
- one showdown sample and a fixed BR sample count.

`--check-reproducibility` compares only deterministic solver fields. Timing
and measured memory are recorded but are **not** required to be byte-for-byte
identical across runs.

The reproducibility fingerprint hashes the sorted strategy rows:

```text
street + hand + tree node + actor + action frequencies + board
```

EV display rollouts are intentionally excluded from that fingerprint.

## Output

Each run gets its own directory:

```text
build/solver-benchmarks/
  selection.json
  summary.json
  summary.csv
  holdem_flop/
    run-1/
      benchmark.json
      solver-report.json
      stdout.log
      stderr.log
```

`solver-report.json` is the native `pe-preflop-solve/v1` report.
Its `stack`, `small_blind`, `big_blind` and `ante` fields echo the command
line. The `root` object describes the root that was actually solved, which a
tree can override: `source` (`blinds`, `tree-header` for a native Monker
preflop tree seated from its header, or `postflop`), `street`,
`first_to_act`, `pot` (posts plus dead money, or the postflop pot),
`dead_money`, `min_raise` (the smallest raise increment at the root, e.g.
the straddle over a straddled header), and per-seat `posts` and `stacks`
(before the posts; a seat whose post is its whole stack starts all in).
`benchmark.json` uses `pe-solver-benchmark/v1` and adds the measurements below.

### Performance

The runner records three separate wall-clock measurements:

- `solve_elapsed_seconds`: from the existing unbuffered `solver created`
  telemetry marker until the solver-owned `solve_loop_end` telemetry event;
- `elapsed_seconds`: the complete CLI subprocess lifetime, including watcher
  shutdown, strategy report generation, per-action EV display rollouts, JSON
  output and cleanup;
- `post_solve_elapsed_seconds`: the difference between those two values, useful
  for spotting CLI/watch/report/materialisation cost.

Using `solve_loop_end` deliberately excludes the CLI's stop-watcher shutdown
latency. On very short solves that latency can be tens of milliseconds and
would otherwise dominate the measured solve time.

`iterations_per_second` is derived **only from `solve_elapsed_seconds`**. An
exhaustive `--report-rows 0` run can spend much longer walking and rendering
strategy rows than solving; that report work must not depress solver
throughput or make variants with more visible infosets look artificially slow.

The performance output also records:

- actual/requested iterations;
- solver infosets;
- infosets per 1,000 iterations.

### Memory

The runner consumes the solver's existing telemetry and records:

- peak measured footprint;
- final footprint;
- storage bytes;
- adapter bytes;
- description-table bytes;
- bytes per infoset;
- storage bytes per infoset;
- whether descriptions were capped.

"Peak measured" means the largest heartbeat/final footprint observed by the
solver. It is not an OS RSS high-water mark.

### Convergence

- guarantee classification;
- empirical BR/exploitability values;
- BR sample count;
- stop cause.

### Per-street coverage

For each of preflop/flop/turn/river the runner records:

- decision nodes present in the tree;
- strategy rows materialized in the report;
- uniform vs non-uniform rows;
- tree nodes that have at least one reported strategy row;
- unique boards represented;
- decision-node coverage;
- share of materialized infosets;
- share of non-uniform rows.

With `report_rows=0` the report is *requested* exhaustively. The result is only
published as `exhaustive=true` when all of the following hold:

- the description table was not capped;
- the reporter emitted its completion marker;
- the normalized hand-table row count exactly equals the solver's retained
  strategy/infoset count from `report_phase=starting`.

`--strict` rejects an uncapped exhaustive request when those cardinalities do
not match. This prevents a deterministic reporter/parser regression from
silently publishing incomplete street coverage as exhaustive.

A non-uniform row is a practical signal that the infoset moved away from regret
matching's initial uniform policy; it is not mislabelled as an exact
traversal-visit count.

### Sampling policies (ISS-232)

The solver now exposes a sampling-policy axis (`--sampling-policy`,
`--street-replicates`) and emits `street_stats street=... visits=... updates=...
chance_samples=... unique_infosets=... uniform_rows=...` telemetry at the end of
every external-sampling solve; the runner parses it into
`benchmark.street_stats`. `cases_street_balance.json` holds standard vs
street-balanced twins of the full-tree case (same tree, seed, ranges,
abstraction and memory budget) so the per-street effect of a policy can be
measured under an equal iteration budget:

```
python3 run_benchmarks.py --manifest cases_street_balance.json --output-dir <dir>
```

`PE_SAMPLING_STREET_BALANCED` replicates a chance draw into street *s*
`street_replicates[s]` times per visit and returns the mean of the replicates,
which is the unbiasedness correction: each replicate is an independent unbiased
trajectory, so deep streets receive that factor more useful updates per
iteration while the game being solved is unchanged.

### Storage-tier accounting (ISS-235)

The solver exposes a resolved storage-tier axis (`--memory-policy`: `full`,
`compact`, `recompute-deep`). `cases_storage_tiers.json` holds storage twins of
the full-tree case per variant (same tree, seed, ranges, abstraction, iteration
and memory budget) with precision forced to `f32`, so the staged compact
precision actually stages and the resolved tier actually trades:

```
python3 run_benchmarks.py --manifest cases_storage_tiers.json --output-dir <dir>
python3 storage_tier_report.py --summary <dir>/summary.json --selection <dir>/selection.json --output benchmarks/baseline/pe_storage_tiers.json
```

Every figure in the comparison is copied from the runner's measurements — the
exact `solver_accounting` bytes (resolved tier, retained/recomputable bytes,
recompute calls, bytes saved vs full), solve-owned wall clock and the
exhaustive-report strategy fingerprints (published as
`strategy_matches_baseline`; no verdict is attached). Only family grouping and
ratios are computed. The checked-in artifact at
`benchmarks/baseline/pe_storage_tiers.json` is a machine-specific baseline.

### Storage tiers at scale (ISS-247)

`cases_storage_tier_scale.json` carries two family kinds over the same
storage twins:

- `*_scale_*` — a large iteration budget (20k) under a hard memory budget
  (128 MiB) that does *not* bind. The comparison is throughput, peak and
  storage ratios at equal iteration counts, so the exhaustive strategy
  fingerprints must match the `full` baseline.
- `*_budget_*` — a deliberately tight RAM budget (`--max-ram 4`,
  `--desc-limit 1`) so every tier is stopped by `memory_budget` at a
  different iteration. Each budget case declares
  `expected_stop_cause: "memory_budget"` so the runner accepts the clean
  early stop instead of flagging it; the comparison is iterations and
  infosets *reached*.

```
python3 run_benchmarks.py --manifest cases_storage_tier_scale.json --suite tiers  --output-dir build/solver-benchmarks-scale
python3 run_benchmarks.py --manifest cases_storage_tier_scale.json --suite budget --output-dir build/solver-benchmarks-budget
python3 tier_scale_report.py \
  --summary build/solver-benchmarks-scale/summary.json \
  --summary build/solver-benchmarks-budget/summary.json \
  --selection build/solver-benchmarks-scale/selection.json \
  --output benchmarks/baseline/pe_storage_tier_scale.json
```

`tier_scale_report.py` accepts a repeated `--summary` precisely so the two
suites can be run (and re-run) independently and still land in one
artifact.

A budget case's `memory_accounting` is real, and so is its
`metrics_available`. Issue #249: a `memory_budget` stop lands before the first
best-response measurement, so the solver reports `metrics_available=0` and the
validator accepts the `unspecified` guarantee that goes with it — but only
when the case declared the early stop. The storage accounting itself never
needed a measurement and is filled either way, which is what makes
`memory_policy` on the `memory` line name the tier that actually ran rather
than `full` for every tier. The `progress is not complete` relaxation stays:
an early stop genuinely is incomplete, and that is a separate question from
whether the metrics were lost. That statement is checked twice: the validator
reads it from stdout, and `validate_native_report()` requires the native
report's boolean to say the same thing — one run cannot archive two
contradictory answers about whether its convergence block was measured.

The estimator behind the exploitability is cross-checked the same way (issue
#274): `metrics.br_estimator` in the report must agree with the
`br_estimator=` field on stdout. `metrics.br_mode` alone cannot carry that
statement, because both estimators report `sampled`; the name is what keeps a
one-rollout maximum from being archived under the confidence-guided label.

Per-street query latency — the phase-6 item that total solve time cannot
show — is measured by `query_latency_probe.py`, which drives the solver's
own `--interactive` protocol (one process per tier *and street*, then one
`query <cards>` round trip):

```
python3 query_latency_probe.py --output benchmarks/baseline/pe_query_latency.json
```

Two protocol details are load-bearing, and getting either wrong silently
erases what the probe exists to measure (after issue #250 the answer is that
the tiers agree within noise — the earlier spread was the commit sweep):

- **A query is only cold once per process.** Every query re-materialises
  and *retains* the spans it touches, so a second query against the same
  solve is already warm. One process per (tier, street) is started, and the
  first query in it is reported as `cold_seconds`; the rest are warm.
- **The startup report warms the storage.** With `--report-rows 0` the
  solver prints an exhaustive strategy report *before* the interactive
  handshake, and that report calls `pe_solver_strategy` for every infoset —
  re-materialising exactly the spans a drop pass evicted. On the reference
  machine that alone collapses the three tiers to within 3 % of each other.
  `--startup-report-rows` (default 1) keeps that report minimal.

### Private-deal count

The result schema contains `sampled_private_deals`, currently `null`.
The product solver does not expose a retained private-deal sample count today.
The benchmark intentionally emits `null` instead of inferring or fabricating a
number.

### PLO4 river oracle and baseline (issue #280)

`test_plo4_river_oracle` is the correctness side. Small deterministic river
fixtures with 4-card private hands pin the Omaha rule (exactly two hole cards
plus three board cards) against an explicit 2+3 enumeration, check that
4-card hands take the documented showdown path for their size (the pairwise
fallback below 32 opponents, the blocker-aware PLO4 sweep at or above), and
cross-check blockers, legal reach
mass, win/tie/loss payouts and per-combo EV against an independent
enumeration. The heads-up river terminal is compared deal by deal on a
tractable fixture. Every fixture is exhaustive; nothing in that test is
sampled.

`tools/pe_plo4_river_bench.c` is the performance side. It evaluates the full
Cartesian product of two deterministic ranges (16, 128 and 512 combos per
player) on the checked-in `Ks9d4c2hTs` river board through the product
`pe_showdown_vector()`, which for 4-card hands is the pairwise fallback below
32 opponents and the blocker-aware PLO4 sweep at or above. The report records
whichever path ran, and the probe rejects a report whose path does not match the
range size it measured. Setup (range construction plus hand-strength
evaluation) and evaluation are timed separately:

```bash
cmake --build build --target pe-plo4-river-bench --parallel
python3 benchmarks/solver/plo4_river_probe.py \
  --bench build/tools/pe-plo4-river-bench \
  --repeat 2 \
  --output build/plo4-river-baseline.json
```

The emitted document is `pe-plo4-river-baseline/v1`. It carries the measured
wall clocks, the OS peak RSS high-water mark where available, and the reported
showdown path. The fingerprint hashes the per-combo values, so the
deterministic part is reproducible across runs and machines while the timings
are explicitly machine-specific evidence, not a CI speed gate.

The checked-in `benchmarks/baseline/pe_plo4_river.json` is one such
machine-specific artifact, alongside the other `benchmarks/baseline/` reports.

The allocation figure is a **net heap delta measured across the timed repetitions
and reported per evaluation** (blocks on macOS, bytes on glibc), so it does not
move when `--repetitions` changes. It is not a count of allocation calls, and
on platforms without a heap counter it is reported unavailable with the reason
instead of being inferred. `evaluated_pairs` counts the blocker-legal
hero/opponent pairs the pairwise fallback actually compares.

## CI policy

`.github/workflows/solver-benchmark-smoke.yml` builds only the product solver
and runs two complementary checks:

- the five-case cross-variant structural smoke twice at 64 iterations;
- a deterministic `holdem_flop` learning probe twice at 10,000 iterations;
- the bounded PLO4 river microbenchmark twice, whose deterministic fields and
  value fingerprints must match (wall clocks are recorded, not gated).

The job fails when:

- the solver exits non-zero;
- the requested iteration budget is not reached (a case may declare
  `expected_stop_cause` — for example `memory_budget` — to accept an early
  clean stop, but it must still respect the iteration cap it was given);
- the stop cause is not the one the case declared (`max_iterations` by
  default);
- the solver reports that the convergence block was not measured and the case
  did not declare an early stop (issue #249) — a declared early stop is the
  only thing that makes an unmeasured block expected;
- the solver timing markers are missing;
- no infoset is materialized;
- a required street is absent;
- an uncapped exhaustive report does not contain one normalized row per solver
  infoset;
- deterministic fingerprints/metrics differ between repetitions;
- the learning probe still contains zero non-uniform strategy rows;
- the PLO4 baseline is sampled, reports a showdown path that does not match its
  range size (pairwise below 32 opponents, PLO4 sweep at or above), or its value
  fingerprint changes between repetitions.

It does **not** gate on wall-clock speed because shared GitHub runners are not
performance-identical. Longer performance qualification remains a manual
benchmark run; the resulting JSON/CSV is suitable for storing machine-specific
baselines.

## HS-DCFR schedules and time-to-target (issue #284)

Discounted CFR's fixed exponents are the control. The training-free schedules
of Zhang et al. (AAAI 2026) replace them per iteration:

    alpha(t) = 1 + 3 * min(t, n) / n
    beta(t)  = -1 - 2 * min(t, n) / n
    gamma(t) = G - 5 * min(t, n) / n       G = 15 or 30

`n` is the horizon (`--dcfr-horizon`, default 1000) and the parameters are
clamped at the horizon instead of extrapolated. Negative `beta` is legal under
these schedules; `alpha` and `gamma` stay non-negative. The product driver
exposes them as `--dcfr-schedule fixed|hs15|hs30` and records the choice in the
JSON report as `dcfr_schedule` / `dcfr_horizon`.

`pe-plo4-lane-a` is the lane A driver for the same spot: it builds the
certification river game (`Ks9d4c2hTs`, pot 63, stack 68.5, one pot-sized bet,
512 combos per range by default) and hands it to the v3 vector solver under the
requested preset and DCFR schedule. Every convergence measurement is the exact
best response of the vector lane, so the report says `br_mode=exact` and
`guarantee=nash`; the `--target-mbb` value is expressed in chips per hand and
the driver converts it to the solver's vector-combo scale internally.

`plo4_time_to_target.py` measures time-to-target exploitability with that
driver, holding game, ranges, bet size and best-response evaluator constant:

    python3 benchmarks/solver/plo4_time_to_target.py \
        --solver build/local/tools/pe-plo4-lane-a \
        --targets 1000,500,250 --combos 512 --seeds 11,23,37 \
        --output build/plo4-time-to-target.json

Each run records wall-clock seconds, the exploitability and measurement
iteration, the best-response mode and guarantee (`br_mode`, `guarantee`), the
solver-reported storage bytes and infosets, and the seed. The JSON keeps every
run plus per-schedule/per-target medians and spreads. A target counts as
reached only when every seed reached it; unreached targets have no time and are
marked in the Markdown table. An exact measurement is a measured value, not an
estimate. The probe still writes `diagnostics.schedules_are_inert` and warns if
a driver ever applies one update rule for every schedule, so identical rows are
never presented as an algorithm comparison, and the probe then exits non-zero so CI fails instead of
publishing identical rows.

`check_lane_a_schedules.py` is the direct guard: it runs `pe-plo4-lane-a` with
`fixed` and `hs30` on the same spot at two checkpoints, requires `br_mode=exact`
and `guarantee=nash` on every report, and fails unless exploitability or the
best-response gaps differ at at least one checkpoint.

`pe-plo4-lane-a --help` lists the spot overrides (`--board`, `--pot`,
`--stack`, `--bet-size`, `--combos`, `--first-to-act`, `--dcfr-horizon`).
