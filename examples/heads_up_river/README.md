# Heads-Up River Example

This scenario is a two-player river spot with a simple betting tree:

```
P0 (bets or checks) → P1 (folds or calls) → showdown
```

Each player has a small weighted range so the solver can mix between
actions.  The helper script below runs several thousand iterations,
captures metrics, exports the full node summaries (JSON) and a
river-only CSV view, and produces a node→state map for later reuse.

## Prerequisites

```bash
cmake -S . -B build
cmake --build build --target mpf_run_with_metrics mpf_dump_results
```

## Run the demo

```bash
examples/heads_up_river/run.sh
```

The script writes:

- `metrics.jsonl` – JSONL snapshots every 250 iterations.
- `run.chk` – CFR checkpoint (storage) after 5 000 iterations.
- `node_map.csv` – node → state-key mapping for the tree.
- `results.json` – full node summaries (all streets, combo breakdowns).
- `results_river.csv` – CSV restricted to the river decision.

Inspect the JSON/CSV files or adjust the filters/iterations in the
script to explore different views.
