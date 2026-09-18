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
whether the metrics were lost.

Per-street query latency — the phase-6 item that total solve time cannot
show — is measured by `query_latency_probe.py`, which drives the solver's
own `--interactive` protocol (one process per tier *and street*, then one
`query <cards>` round trip):

```
python3 query_latency_probe.py --output benchmarks/baseline/pe_query_latency.json
```

Two protocol details are load-bearing, and getting either wrong silently
erases the tier difference the probe exists to measure:

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

## CI policy

`.github/workflows/solver-benchmark-smoke.yml` builds only the product solver
and runs two complementary checks:

- the five-case cross-variant structural smoke twice at 64 iterations;
- a deterministic `holdem_flop` learning probe twice at 10,000 iterations.

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
- the learning probe still contains zero non-uniform strategy rows.

It does **not** gate on wall-clock speed because shared GitHub runners are not
performance-identical. Longer performance qualification remains a manual
benchmark run; the resulting JSON/CSV is suitable for storing machine-specific
baselines.
