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
  telemetry marker until `solver_phase=complete ... report=starting`;
- `elapsed_seconds`: the complete CLI subprocess lifetime, including strategy
  report generation, per-action EV display rollouts, JSON output and cleanup;
- `post_solve_elapsed_seconds`: the difference between those two values, useful
  for spotting report/materialisation cost.

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

The next sampling-policy work can add raw per-street visit/update counters to
the solver telemetry. This corpus already gives it stable cases, exact inputs
and before/after state-coverage measurements.

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
- the requested iteration budget is not reached;
- the stop cause is not `max_iterations`;
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
