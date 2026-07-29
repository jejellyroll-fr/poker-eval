#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/../../build}"

RUN_TOOL="${BUILD_DIR}/tools/mpf_run_with_metrics"
DUMP_TOOL="${BUILD_DIR}/tools/mpf_dump_results"

TREE_FILE="${ROOT_DIR}/tree.json"
CHECKPOINT_FILE="${ROOT_DIR}/run.chk"
NODE_MAP_FILE="${ROOT_DIR}/node_map.csv"
METRICS_FILE="${ROOT_DIR}/metrics.jsonl"
JSON_RESULTS="${ROOT_DIR}/results.json"
CSV_RESULTS="${ROOT_DIR}/results_river.csv"

ITERATIONS=${ITERATIONS:-5000}
METRICS_INTERVAL=${METRICS_INTERVAL:-250}

if [[ ! -x "${RUN_TOOL}" || ! -x "${DUMP_TOOL}" ]]; then
    echo "Run 'cmake --build build --target mpf_run_with_metrics mpf_dump_results' first." >&2
    exit 1
fi

echo "Running CFR with metrics (${ITERATIONS} iterations)..."
"${RUN_TOOL}" \
    --tree "${TREE_FILE}" \
    --iterations "${ITERATIONS}" \
    --metrics-interval "${METRICS_INTERVAL}" \
    --metrics-file "${METRICS_FILE}" \
    --checkpoint "${CHECKPOINT_FILE}" \
    --node-map "${NODE_MAP_FILE}"

echo "Exporting full JSON results..."
"${DUMP_TOOL}" \
    --tree "${TREE_FILE}" \
    --storage "${CHECKPOINT_FILE}" \
    --node-map "${NODE_MAP_FILE}" \
    --output "${JSON_RESULTS}" \
    --format json

echo "Exporting river CSV view..."
"${DUMP_TOOL}" \
    --tree "${TREE_FILE}" \
    --storage "${CHECKPOINT_FILE}" \
    --node-map "${NODE_MAP_FILE}" \
    --output "${CSV_RESULTS}" \
    --format csv \
    --street river

echo "Metrics written to ${METRICS_FILE}"
echo "Checkpoint stored at ${CHECKPOINT_FILE}"
echo "Node map saved to ${NODE_MAP_FILE}"
echo "JSON export available at ${JSON_RESULTS}"
echo "River CSV export available at ${CSV_RESULTS}"
