# 4-Way Postflop Walkthrough

This miniature tree demonstrates how to run the CFR adapter using the
CLI helpers introduced with Feature 5.

## Prerequisites

```bash
cmake -S . -B build
cmake --build build --target mpf_run_with_metrics mpf_dump_results
```

## 1. Run CFR with live metrics

From the project root:

```bash
build/tools/mpf_run_with_metrics \
  --tree examples/4way_postflop/tree.json \
  --iterations 200 \
  --metrics-interval 20 \
  --metrics-file examples/4way_postflop/metrics.jsonl \
  --checkpoint examples/4way_postflop/run.chk \
  --node-map examples/4way_postflop/node_map.csv
```

This command:

- loads `tree.json`
- runs 200 iterations
- writes JSONL snapshots to `metrics.jsonl`
- saves a storage checkpoint (`run.chk`)
- records state-key mappings in `node_map.csv`

## 2. Export aggregated results

```bash
build/tools/mpf_dump_results \
  --tree examples/4way_postflop/tree.json \
  --storage examples/4way_postflop/run.chk \
  --node-map examples/4way_postflop/node_map.csv \
  --output examples/4way_postflop/results.json \
  --format json \
  --street flop
```

`results.json` now contains per-node summaries restricted to the flop
street (including combo breakdowns).

## 3. Optional helper script

```bash
examples/4way_postflop/run.sh
```

The script wraps the two commands above and prints the locations of the
generated files.
