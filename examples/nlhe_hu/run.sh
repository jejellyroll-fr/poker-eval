#!/usr/bin/env bash
# Heads-up No-Limit Hold'em trees -- the fast ones to test with.
#
#   ./run.sh              # the full preflop..river tree
#   ./run.sh boards       # the same flop spot on three textures, side by side
#   ITERATIONS=50000 ./run.sh boards
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/../../build}"
SOLVER="${SOLVER:-${BUILD_DIR}/tools/pe-preflop-solve}"
ITERATIONS="${ITERATIONS:-20000}"
RANGE="${RANGE:-AA,KK,QQ,JJ,TT,AKs,AQs,76s,65s,54s}"

[[ -x "${SOLVER}" ]] || { echo "Build pe-preflop-solve first." >&2; exit 1; }

common=(--game holdem --players 2 --samples 1 --br-samples 16
        --algorithm external-mccfr --backend cpu_ref --precision f64 --threads 1)

if [[ "${1:-full}" == "boards" ]]; then
    # Same tree shape, same ranges, three flop textures: the only thing that
    # changes is the board, so any difference in the output is the board's.
    for spec in dry_K72r:Ks7d2c wet_JT9ss:JhTh9s paired_882:8s8d2h; do
        name="${spec%%:*}"; board="${spec##*:}"
        echo "=== flop ${board} (${name}) ==="
        "${SOLVER}" "${common[@]}" \
            --tree "${ROOT_DIR}/nlhe_hu_flop_${name}.tree.json" \
            --street flop --board "${board}" --pot 6 \
            --iterations "${ITERATIONS}" \
            --range0 "${RANGE}" --range1 "${RANGE}" 2>/dev/null \
            | grep -E '^iterations=|^guarantee'
        echo
    done
else
    echo "=== preflop..river, one tree ==="
    "${SOLVER}" "${common[@]}" \
        --tree "${ROOT_DIR}/nlhe_hu_full.tree.json" \
        --iterations "${ITERATIONS}" --range0 "100%" --range1 "100%" 2>/dev/null \
        | grep -E '^tree_streets=|^iterations=|^guarantee'
fi
