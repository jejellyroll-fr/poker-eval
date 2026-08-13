# CFR Solver Documentation Suite

This folder groups all references related to the multiway postflop CFR
adapter.  Each guide focuses on a different stage of the workflow so you
can jump directly to the topic you need.

| Guide | Purpose | Status |
|-------|---------|--------|
| CFR Tree Format | Reference for predefined-tree JSON (profiles, nodes, validation) | ✅ |
| CFR Metrics | Runtime metrics API, snapshots, examples | ✅ |
| CFR Export Results | Post-run result exports (JSON / CSV) | ✅ |
| CFR Performance | Perf counters & instrumentation tips | ✅ |
| CFR Data Pipeline | End-to-end walkthrough: build tree → run → monitor → export | ✅ |
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
