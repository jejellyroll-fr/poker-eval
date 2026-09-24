# Memory as a First-Class Metric (issue #235)

For large sampled solves, memory is the practical limit before evaluator
speed. A solver that processes millions of iterations per second is still
unusable if every newly discovered infoset retains too much metadata, so the
memory a solve holds must be measured, attributed and reported with the same
rigour as convergence.

This guide formalises the phase-1 measurement model: what the solver counts,
which subsystem owns each byte, and where the figures surface.

## The metric

The primary figure is **bytes per infoset**:

    bytes_per_infoset = storage_bytes / total_infosets

with its slot-normalised sibling

    bytes_per_strategy_slot = storage_bytes / total_slots

Both are derived from the same `storage_bytes` total that
`pe_storage_bytes()` (the storage port's `bytes` op) reports, so a report can
never disagree with the figure the memory budget enforces.

## Subsystem breakdown

`pe_storage_memory_report_t` (in `pe_storage_port.h`) attributes every byte
to the subsystem that owns it:

| Field | Owns |
|-------|------|
| `hash_index_bytes` | The open-addressed key -> id map. |
| `metadata_bytes` | Per-infoset metadata: shape, value offsets, flags, and for FIXED16 the per-infoset scale vectors. |
| `regret_bytes` | Resident regret values, in the selected representation. |
| `average_bytes` | Resident average-strategy values. |
| `other_values_bytes` | Resident CURRENT / LOCKED arrays (lazily allocated; untouched arrays cost nothing). |
| `staging_bytes` | Decoded double spans a compact representation keeps at the port boundary, plus their index arrays. |
| `allocator_overhead_bytes` | A documented estimate: `PE_STORAGE_ALLOC_OVERHEAD_BYTES` (16) per heap block the storage owns. An estimate, not a measurement; allocator rounding is not accounted for. |

`storage_bytes` is the total of every category plus the storage structure
itself. The categories and the total are computed in one walk
(`pe_storage_memory_report()` in `src/solver/domain/storage_v2.c`), so
consistency holds by construction.

## Precision and compaction

The resident representation is chosen at storage creation
(`pe_storage_create_with_precision`):

- `PE_PREC_F64` — reference, 8 bytes per slot;
- `PE_PREC_F32` — 4 bytes per slot plus a decoded staging span per
  materialised infoset;
- `PE_PREC_FIXED16` — 2 bytes per slot, a per-infoset scale, and the same
  staging spans.

The report makes the trade visible: compact precisions shrink `regret_bytes`
/ `average_bytes` exactly with their slot width, and the staging cost of the
port boundary appears in `staging_bytes` instead of being hidden. Any
reduced-precision mode must pass the strategy-quality regression tests that
gated its introduction (`tests/test_pe_storage_precision.c`).

## Adapter memory is reported separately

The game adapter's own retained state — per-infoset descriptions, deal
samplers — is *not* solver storage. `pe_metrics_t::adapter_bytes` reports it
on top of `storage_memory`, derived from the same footprint the
`max_ram_bytes` budget enforces, minus the storage's own self-report through
its `bytes()` op. The subtraction relies on `bytes()` alone, so a custom
storage port that implements no detailed breakdown still keeps its footprint
attributed to the storage — never misreported as game-adapter memory.
Keeping the two apart means a reporting or debug limit can be audited against
convergence-critical state: a reporting cap must never change strategy
results, and the separation makes that auditable.

## Where the figures surface

- **C API** — `pe_solver_metrics()` fills `pe_metrics_t::storage_memory`
  (the full breakdown plus the derived per-infoset / per-slot figures) and
  `pe_metrics_t::adapter_bytes`, measured at query time from the live
  storage. A storage adapter that cannot attribute its memory leaves the
  breakdown zeroed rather than guessing; only the breakdown stops there,
  while `bytes()` still anchors the storage footprint for `adapter_bytes`.

  The accounting is filled on **every** return, including
  `PE_SOLVER_ERR_INVALID_STATE`. That status means one thing only: the
  convergence block was never measured, so its zeros are absence. Every other
  field is still valid. Issue #249.
- **CLI** — `pe-preflop-solve` prints a `memory infosets=… storage_bytes=…
  adapter_bytes=… bytes_per_infoset=… bytes_per_strategy_slot=…` diagnostic
  line after the `guarantee=` line, and the JSON report (`--output`) carries
  a top-level `memory` object with the full breakdown. Both are appended
  fields: the Studio's fixed-prefix parse of earlier lines is untouched. The
  `guarantee=` line ends with `metrics_available=1|0`, and the JSON `metrics`
  object carries the same value as a boolean, so an unmeasured convergence
  block never reads as a measured zero.
- **Studio** — `tools/poker_eval_studio.c` parses the marker, keeps it on the
  synthetic `guarantee=` line it rebuilds from the captured telemetry, and
  shows `not measured` with the `stop_reason` that ended the run instead of
  painting the block's zeros as a final result. A binary older than the marker
  states nothing, and the Studio then keeps its previous behaviour rather than
  guessing: "not stated" and "stated as unmeasured" are not the same thing.
- **Benchmarks** — `benchmarks/solver/run_benchmarks.py` captures the exact
  solver accounting as `memory.solver_accounting` in the benchmark payload,
  making `bytes_per_infoset` a first-class benchmark metric next to
  peak/final RAM. It also captures `metrics.metrics_available`, and the
  validator accepts an `unspecified` guarantee only when the solver says the
  block was not measured — a run that loses its metrics without declaring an
  early stop is a failure, where the guarantee name alone used to be the
  signal. The native report's boolean is cross-checked against the stdout
  marker (`validate_native_report()`), so one run cannot archive two
  contradictory answers about whether its convergence block was measured.

## Public ABI note

`pe_metrics_t` grew (`storage_memory`, `adapter_bytes`; phase 6 adds the
tier and drop accounting). As with issue #234, the solver shared library's
SOVERSION was bumped (now 4) so an application built against the old
headers cannot be mixed with a newer library.

## Explicit storage tiers (phase 6)

With the baseline attributed, the flagship optimisation of issue #235
becomes a first-class axis: an explicit **storage tier** decides how much
derived state each street keeps. The contract is the same as the sampling
policy (ISS-232): kept next to the axis, explicit, visible in the plan —
never inferred from a preset. The CFR semantics never change.

| Tier | Deep streets keep | Reduction | CPU cost |
| --- | --- | --- | --- |
| `full` (default) | everything, derived included | 0 | 0 |
| `compact` | flop stored; turn/river re-materialise | largest so far | decode work |
| `recompute-deep` | nothing kept; every street re-materialises | largest | most decode work |

The red line is unchanged: a tier never touches strategy results. Dropping
derived decoded state and re-decoding it byte-exact is not an
approximation, and the pinned regression test (`test_storage_tiers_solve`,
label `sto02`) holds that under the same precision and seed, every tier
produces bitwise-identical convergence aggregates — F64 and FIXED16.

### Port capability

`pe_storage_ops_t` gained an optional `drop_recomputable(self, min_street)`
op. A solver pass that has just applied an iteration may drop the derived
double staging spans of infosets acting at or beyond `min_street` (streets
tagged unknown always pass), after flushing them byte-exact into the
resident compact arrays. Dropped spans re-materialise on the next access.

The drop passes run at the one point that guarantees no caller anywhere
holds a span pointer: the **iteration boundary**, between the batch apply
and the next traversal. A checkpoint can only observe the storage at this
boundary, never mid-iteration. An adapter that holds no derived layer (the
legacy hash storage) leaves the op NULL and honours nothing; the policy is
a documented no-op for it.

### Tiers bite only under a compact precision

Under `f64` there is no derived decoded layer anywhere, so a tier is a
no-op by construction. The same holds for `mixed`: it is not a staged
compact precision — it stages no compact arrays (its estimate contract
sizes storage by the F64 reduction buffer, 8 bytes per slot), so it holds
no decoded spans and a tier answers success and drops nothing. Under
`f32`/`fixed16` every access to an infoset decodes a double staging span
resident beside the compact arrays; the tier decides which streets keep
those spans. Where a tier bites:

- `full` — nowhere: nothing is ever deep enough.
- `compact` — infosets acting at street ≥ 2 (turn, river) plus streets
  tagged unknown; preflop and flop stay hot.
- `recompute-deep` — every infoset, including the opening streets.

### Reading the measured trade

`pe_metrics_t` appends the tier that actually ran and what the drop passes
reported — even when the resolved port implements none of the drop
machinery:

- `storage_memory_policy` — the tier that ran; never inferred.
- `recompute_calls` / `recompute_time_ms` — re-materialisations of
  recomputable state a drop pass removed, and the wall clock they spent.
  The measurable CPU cost of the trade.
- `bytes_saved_vs_full` — recomputable bytes drop passes removed across the
  run (cumulative; a span evicted twice counts twice). The measured RAM
  saved against `PE_STORAGE_FULL`.

The memory diagnostic line carries the resolved tier and the retained vs
recomputable split:

```text
memory infosets=… storage_bytes=… adapter_bytes=… bytes_per_infoset=…
bytes_per_strategy_slot=… memory_policy=compact retained_bytes=…
recomputable_bytes=… recompute_calls=… bytes_saved_vs_full=…
```

and the JSON report's `memory` object appends `retained_strategy_bytes` /
`recomputable_strategy_bytes` (the convergence-critical layer vs the derived
layer: dropped only with the storage itself vs dropped at an iteration
boundary and re-decoded byte-exact), plus `recompute_calls` /
`bytes_saved_vs_full`. The benchmark payload captures the same split
additively in `memory.solver_accounting`.

### Choosing a tier

- `full` retains every resident layer, derived included, once
  materialised — the baseline every other mode is benchmarked against.
- `compact` is the issue's candidate behaviour for flop/turn/river
  materialisation: derived state kept for the hot opening streets, dropped
  for the deep ones.
- `recompute-deep` is the largest reduction, paid in the most decode work.

Select on `pe-preflop-solve` with `--memory-policy NAME` (`full`,
`compact`, `recompute-deep`), or on the C API with
`pe_solver_config_t::execution::storage_policy`. The resolved tier is
recorded in the execution plan (`storage policy` line) so diagnostics
report the mode that actually ran.

## Tiers at scale: fixed budget, query latency and the wall-clock inversion (issue #247)

The committed `pe_storage_tiers.json` baseline pins the tier trade at 2 000
iterations. Issue #247 transfers three measurements that #235 left with a
"when practical" latitude. They are recorded in two artifacts,
`benchmarks/baseline/pe_storage_tier_scale.json` and
`benchmarks/baseline/pe_query_latency.json`, both machine-specific.

### The wall-clock inversion is the cumulative commit sweep

The baseline shows an inversion that the working-set hypothesis does not
explain: at equal precision and seed, `recompute-deep` finishes a PLO4 solve
faster than `full` (29.6 s vs 4.1 s at 2 000 iterations), even though it does
strictly more decoding work. Profiling `pe-preflop-solve` on macOS arm64 with
the system `sample` tool (2 579 samples at 1 ms, PLO4/f32/`full`) resolves it:
**2 555 of 2 579 samples (99.1 %) sit in `pe_low_precision_commit_all`.**

The mechanism is algorithmic, not cache-related. Under `PE_STORAGE_FULL` (and
for preflop/flop under `compact`) no staging span is ever released, so
`pe_low_precision_commit_all` walks *every* allocated infoset — O(N) — on each
`pe_storage_resolve` / `pe_low_precision_values` call, and copies every float
of every infoset whose `staging_dirty` bit is set. `pe_low_precision_commit_one`
deliberately leaves that bit set, so the dirty set grows with infoset
discovery and is never reset: the run pays a cumulative O(N × accesses) sweep.
`PE_STORAGE_RECOMPUTE_DEEP` releases the staging spans and clears
`staging_dirty` at each iteration boundary (`pe_storage_drop_recomputable`), so
its active dirty set stays bounded by the current iteration.

**Verdict: the working-set hypothesis is refuted.** The inversion is the
cumulative re-commit sweep, which `full` pays and `recompute-deep` does not.
The same sweep is why `full` scales worse than its tiers: on the reference
machine, PLO4/`full` costs 53 s at 1 000 iterations and 140 s at 2 000 (whole
process, report phase included), a superlinear factor of 2.6× for 2× the
iterations.

### Fixed RAM budget: materially more iterations and infosets

`cases_storage_tier_scale.json` carries a `*_budget_*` family that runs the
same Hold'em twins under a deliberately tight budget (`--max-ram 4`,
`--desc-limit 1`) so every tier is stopped by `memory_budget` at a different
iteration. Each budget case declares `expected_stop_cause: "memory_budget"`,
which lets the runner accept the clean early stop (an incomplete `progress`
flag and an `unspecified` guarantee are expected, and every cross-check
between the native report and stdout still applies).

Under the *same* 4 MiB budget, on the reference machine:

| Tier | Iterations reached | Infosets reached | vs `full` (iterations) | vs `full` (infosets) |
| --- | --- | --- | --- | --- |
| `full` | 1 250 | 8 953 | 1.00× | 1.00× |
| `compact` | 2 500 | 16 003 | 2.00× | 1.79× |
| `recompute-deep` | 3 750 | 21 949 | **3.00×** | **2.45×** |

The compressed tiers reach materially more iterations and infosets before the
budget stops them, which is the #235 criterion measured directly rather than
inferred.

Two properties of the early stop are worth recording, because they shape how
the artifact must be read:

- The solver announces the stop explicitly — `memory budget reached: 2.204796
  MB held, 4.000000 MB allowed (largest step between checks 1.992111 MB, so
  the next one would not fit); stopping at iteration 1250 with the solve
  intact` — and the `solve_loop_end` / `stop_detail` lines carry the iteration
  reached and the storage/adapter split. Those are the figures the artifact
  uses.
- The convergence block is genuinely absent, and the solver now says so. A
  budget stop lands before the first best-response measurement, so
  `pe_solver_metrics()` returns `PE_SOLVER_ERR_INVALID_STATE` and the emitted
  telemetry carries `metrics_available=0` (the JSON report carries the same
  field as a boolean). `exploitability_mbb=0.000000` on such a run is absence,
  not a converged solve that reached zero; the marker is what makes the two
  distinguishable, and the artifact records it per case.

That marker is issue #249. Before it, the entire `memory` line was zeroed on a
budget stop — **including `memory_policy`**, so `compact` and
`recompute-deep` runs were both attributed to `full`: a wrong value that
looked like a plausible one, rather than an obviously absent one. The storage
accounting (`storage_memory`, `adapter_bytes`, the tier and the drop counters)
is read from the live storage at query time and never needed a measurement, so
it is now filled on that path too. The budget family's `memory_accounting`
block carries the real per-tier figures:

| Tier | `memory_policy` | `storage_bytes` | `bytes_per_infoset` | `recompute_calls` | `bytes_saved_vs_full` |
| --- | --- | --- | --- | --- | --- |
| `full` | `full` | 1 360 648 | 152.0 | 0 | 0 |
| `compact` | `compact` | 1 644 464 | 102.8 | 9 306 | 158 360 |
| `recompute-deep` | `recompute-deep` | 2 369 064 | 107.9 | 39 043 | 654 720 |

The drop accounting in the last two columns was invisible on this path before
the fix — which is precisely the trade the fixed-budget criterion exists to
measure.

### Query latency per street

Total solve time cannot separate "the solve was slower" from "answering a board
query was slower". `query_latency_probe.py` measures the second directly by
driving the solver's own `--interactive` protocol: a `query <cards>` round trip
per street, timed from the request to the `query_done` marker.

Two protocol properties decide whether such a measurement is meaningful at all,
and both have to be respected or the tiers collapse into each other:

- **A query is only cold once per process.** Every query re-materialises and
  *retains* the spans it touches, so a second query against the same solve is
  already warm. The probe therefore starts **one process per (tier, street)**
  and reports the first query in each as `cold_seconds`; later repeats in the
  same process are the warm figure.
- **The startup report warms the storage.** With `--report-rows 0` the solver
  prints an exhaustive strategy report *before* the interactive handshake, and
  that report calls `pe_solver_strategy` for every infoset — re-materialising
  exactly the spans a drop pass had evicted, with no iteration boundary
  afterwards to evict them again. Measured on the reference machine, that alone
  collapses the three tiers from a 27–36 % spread to under 3 %: it silently
  measures queries against a fully re-materialised storage, not against the
  tiers' post-solve state. The probe defaults to `--startup-report-rows 1`.

Hold'em, 20 000 iterations, f32, `--board-abstraction large`, µs. Cold = the
first query in a fresh process; warm = median of the two steady-state repeats
that follow (spread under 2 %):

| Tier | FLOP cold | TURN cold | RIVER cold | FLOP warm | TURN warm | RIVER warm | Resident storage |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `full` | 3 488 600 | 965 600 | 714 300 | 3 734 300 | 984 600 | 717 800 | 6.29 MiB |
| `compact` | 2 969 300 | 799 200 | 575 500 | 3 003 100 | 794 200 | 563 700 | 4.65 MiB |
| `recompute-deep` | **2 534 000** | **640 100** | **456 700** | **2 571 400** | **648 800** | **459 900** | 3.95 MiB |

Relative to `full`, `compact` answers in 0.85× / 0.83× / 0.81× (flop / turn /
river) and `recompute-deep` in **0.73× / 0.66× / 0.64×**.

**There is no query-latency penalty for the compressed tiers — the opposite.**
The reason is that a board query is a **full sweep**: it visits every infoset
the solve holds (52 140 here) and emits only the rows whose board matches
(3 222 on the flop, 1 374 on the turn, 2 684 on the river, as reported by the
solver's own `board_query_rows=` marker). Sweep cost therefore dominates, and a
tier that keeps a smaller resident footprint sweeps faster; the re-decode work
`recompute-deep` adds is small next to it, which is also why cold and warm are
within ~2 % of each other.

This is the opposite of the "recompute is expensive" intuition, and it is
consistent with the profiling result: the decode work a tier adds is real, but
at this scale it is dwarfed by the cumulative commit sweep `full` pays. Read
together, the two sections say that `recompute-deep` currently dominates `full`
on every measured axis — memory, solve time and query latency — while producing
byte-identical strategies. That is a statement about the *current* commit sweep,
not about the tier design; bounding the sweep is what would give the tiers back
their intended trade.

Method caveat: the figure is measured from the `query <cards>` request to the
`query_done` marker over a pipe, so it includes the cost of streaming the
matched rows back to the probe. Every tier emits the same rows, so that
constant cost does not affect the comparison.

### Scale counters

**Hold'em, 20 000 iterations, 128 MiB cap** (56 222 infosets, f32, seed
20260906):

| Tier | Solve (s) | vs `full` | Peak (MiB) | Storage (MiB) | vs `full` | Recompute calls | Bytes saved | Fingerprint |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `full` | 82.65 | 1.000× | 23.07 | 6.29 | 1.000× | 0 | 0 | `15db4458…` |
| `compact` | 48.28 | 0.584× | 21.42 | 4.65 | 0.738× | 138 798 | 1.43 MB | `15db4458…` |
| `recompute-deep` | 20.17 | **0.244×** | 20.73 | 3.95 | **0.627×** | 294 083 | 3.70 MB | `15db4458…` |

**PLO4, 5 000 iterations, 128 MiB cap** (125 004 infosets, f32, seed
20260906):

| Tier | Solve (s) | vs `full` | Peak (MiB) | Storage (MiB) | vs `full` | Recompute calls | Bytes saved | Fingerprint |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `full` | 217.63 | 1.000× | 33.76 | 16.98 | 1.000× | 0 | 0 | `3c19e016…` |
| `compact` | 81.49 | 0.374× | 29.56 | 12.79 | 0.753× | 35 862 | 2.20 MB | `3c19e016…` |
| `recompute-deep` | 26.20 | **0.120×** | 27.80 | 11.02 | **0.649×** | 52 712 | 3.20 MB | `3c19e016…` |

All three tiers produce byte-identical exhaustive-report strategy fingerprints
at the same iteration count, so the tiers are a pure memory/CPU trade and never
an approximation. The inversion is *stronger* at scale than at the 2 000-iteration
baseline (PLO4: 0.120× here vs 0.139× there), consistent with the cumulative
commit sweep: the longer `full` runs, the more it pays.

### What remains open

The 100k/500k/1M/2M slots of the issue stay out of reach of a session-scale
benchmark: a full preflop solve at 500k+ exceeds ~2 h per run, and the
cumulative commit sweep makes `full` the worst offender. The PLO4 scale slot is
therefore capped at 5 000 iterations (2.5× the committed baseline); the
Hold'em slot runs the full 20 000. Reaching the large counters needs a
dedicated CI budget or a dedicated bench machine, and is best done *after* the
commit sweep is bounded, since otherwise the measurement is dominated by an
artefact of the `full` tier rather than by solver throughput.

Two defects found while measuring this were tracked separately rather than
folded into the measurement:

- **#249 (fixed)** — the zeroed `memory` line and the wrong tier attribution on
  a budget stop. See "Fixed RAM budget" above. The figures in this guide were
  regenerated with the fix in place.
- **#250 (open)** — bounding the cumulative commit sweep in
  `pe_low_precision_commit_all`. The `full`-tier wall-clock figures here
  describe the tier *as it behaves today*; they will need regenerating once
  the sweep is bounded, since that is where the time goes.
