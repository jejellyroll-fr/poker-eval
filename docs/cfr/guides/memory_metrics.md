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
- **CLI** — `pe-preflop-solve` prints a `memory infosets=… storage_bytes=…
  adapter_bytes=… bytes_per_infoset=… bytes_per_strategy_slot=…` diagnostic
  line after the `guarantee=` line, and the JSON report (`--output`) carries
  a top-level `memory` object with the full breakdown. Both are appended
  fields: the Studio's fixed-prefix parse of earlier lines is untouched.
- **Benchmarks** — `benchmarks/solver/run_benchmarks.py` captures the exact
  solver accounting as `memory.solver_accounting` in the benchmark payload,
  making `bytes_per_infoset` a first-class benchmark metric next to
  peak/final RAM.

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
