#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/../../build}"

RUN_TOOL="${BUILD_DIR}/tools/mpf_run_with_metrics"
DUMP_TOOL="${BUILD_DIR}/tools/mpf_dump_results"
TREE_FILE="${ROOT_DIR}/tree.json"
CHECKPOINT_FILE="${ROOT_DIR}/run.chk"
METRICS_FILE="${ROOT_DIR}/metrics.jsonl"
RESULTS_FILE="${ROOT_DIR}/results.json"
NODE_MAP_FILE="${ROOT_DIR}/node_map.csv"

if [[ ! -x "${RUN_TOOL}" || ! -x "${DUMP_TOOL}" ]]; then
    echo "Run 'cmake --build build --target mpf_run_with_metrics mpf_dump_results' first." >&2
    exit 1
fi

echo "Running CFR with metrics..."
"${RUN_TOOL}" \
    --tree "${TREE_FILE}" \
    --iterations 200 \
    --metrics-interval 20 \
    --metrics-file "${METRICS_FILE}" \
    --checkpoint "${CHECKPOINT_FILE}" \
    --node-map "${NODE_MAP_FILE}"

echo "Exporting results..."
"${DUMP_TOOL}" \
    --tree "${TREE_FILE}" \
    --storage "${CHECKPOINT_FILE}" \
    --node-map "${NODE_MAP_FILE}" \
    --output "${RESULTS_FILE}" \
    --format json \
    --street flop

echo "Metrics written to ${METRICS_FILE}"
echo "Checkpoint stored at ${CHECKPOINT_FILE}"
echo "Node map saved to ${NODE_MAP_FILE}"
echo "Results exported to ${RESULTS_FILE}"
