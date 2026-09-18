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

`pe_metrics_t` grew (`storage_memory`, `adapter_bytes`). As with issue #234,
the solver shared library's SOVERSION was bumped so an application built
against the old headers cannot be mixed with a newer library.

## Phase-1 scope and what comes next

Implemented here: the breakdown, the derived metrics, the port capability,
the CLI/JSON/benchmark surfacing and the ABI bump.

Future phases of issue #235 (compact key representations, deep-street
recomputation policies, retained-vs-recomputable telemetry) build on this
accounting: a storage policy change is only measurable if the baseline
attributed its bytes first.
