# CFR Solver Documentation Suite

This folder groups all references related to the multiway postflop CFR
adapter.  Each guide focuses on a different stage of the workflow so you
can jump directly to the topic you need.

| Guide | Purpose | Status |
|-------|---------|--------|
| CFR Tree Format | Reference for predefined-tree JSON (profiles, nodes, validation) | planned — use `mpf_tree.h` and `mpf_run_with_metrics --help` |
| CFR Metrics | Runtime metrics API, snapshots, examples | planned — CLI/API exist, standalone guide absent |
| Multiplayer Convergence Metrics | NashConv, per-player BR gaps, units, guarantees, sampling metadata (issue #234) | ✅ — see `convergence_metrics.md` |
| Memory Metrics | bytes per infoset, subsystem breakdown, precision trade-offs, explicit storage tiers, adapter separation (issue #235) | ✅ — see `memory_metrics.md` |
| PLO5/PLO6 Range Syntax | rank patterns, the suit-shape grammar, what is refused, resource guard (issue #236) | ✅ — see `plo_range_syntax.md` |
| Conditional Card Removal | the posterior-range invariant for sampled deals, the exact oracle, what the suite rejects (issue #259) | ✅ — see `conditional_card_removal.md` |
| Confidence-Guided Best Response | sampled BR decisions that stop once the best action is resolved: union-bound sequential intervals, re-estimated values, telemetry, benchmarks (issue #257) | ✅ — see `confidence_guided_best_response.md` |
| Adaptive Variance Sampling | variance-driven chance replication for Lane B: budget rule, unbiasedness (R fixed beforehand, 1/R weights), groups, trajectory cap, telemetry, benchmarks (issue #256) | ✅ — see `adaptive_variance_sampling.md` |
| Uncertainty-Aware Work Prioritisation | ordering work by how undecided a decision is: the gap/spread score, geometric buckets, the coverage tier, aging, telemetry, and why it is a layer rather than a change to `pe_work_schedule()` (issue #258) | ✅ — see `work_priority_scheduling.md` |
| PLO Hand Features | PLO4/5/6 made hands, draws, blockers and private structure under two-plus-three; bucket keys; strategy aggregation (issue #238) | ✅ — see `plo_hand_features.md` |
| Omaha Hi/Lo 8-or-Better | PLO4/5/6 Hi/Lo showdowns: selecting it, two-plus-three for both halves, side-pot splitting, rake order, what is refused (issue #237) | ✅ — see `omaha_hilo8.md` |
| CFR Export Results | Post-run result exports (JSON / CSV) | planned — see `mpf_dump_results` |
| CFR Performance | Perf counters & instrumentation tips | planned |
| CFR Data Pipeline | End-to-end walkthrough: build tree → run → monitor → export | planned — see `examples/4way_postflop/` |
| Hand Abstraction via Clustering | k-means hand clustering (FEAT-04): features, `.pe_bkt`, solver wiring | ✅ |
| Subgame Re-solving | CFR-D gadget re-solving from a blueprint (FEAT-05): value constraints, API, 2-player | ✅ |

## CLI helpers

Feature 5 introduces two utilities (built in `tools/`):

- `mpf_run_with_metrics` – loads a tree, runs CFR, streams metrics, and
  optionally saves a checkpoint and node-key map.
- `mpf_dump_results` – reloads the tree + checkpoint + node map and
  exports JSON/CSV summaries using the export API.

See the CFR data pipeline documentation and `examples/4way_postflop/` for concrete
usage.

Additional example: `examples/heads_up_river/` runs a deeper two-player
river tree, produces JSON + CSV exports, and showcases EV aggregation.

## GPU CFR pipeline

The GPU production track now converts the classic hash-map CFR storage to
dense matrix buffers before handing them to the CUDA/OpenCL backends. Use
`cfr_convert_to_matrix()` to transform an existing `cfr_storage_t` into a
row-major matrix (`regrets`, `avg_strategy`, per-infoset metadata). The
companion `cfr_convert_from_matrix()` helper restores CPU storage, making
round-trips straightforward when debugging or checkpointing hybrid runs.

## Remaining work

- Migration notes and FAQ covering new solver options.
- Additional tutorials/QA once more tooling lands.
