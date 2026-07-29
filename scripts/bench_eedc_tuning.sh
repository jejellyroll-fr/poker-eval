#!/usr/bin/env bash
# Run bench_eedc_quick with several chunk/threshold permutations
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

find_bench_eedc_quick() {
    local candidate
    if [[ -n "${BENCH_BIN:-}" && -x "${BENCH_BIN:-}" ]]; then
        printf '%s' "${BENCH_BIN}"
        return 0
    fi
    if [[ -x "${ROOT_DIR}/build/src/examples/bench_eedc_quick" ]]; then
        printf '%s' "${ROOT_DIR}/build/src/examples/bench_eedc_quick"
        return 0
    fi
    candidate="$(find "${ROOT_DIR}/build" -type f -name bench_eedc_quick -perm /111 -print -quit)"
    if [[ -n "${candidate}" ]]; then
        printf '%s' "${candidate}"
        return 0
    fi
    return 1
}

BIN="$(find_bench_eedc_quick)"
if [[ -z "${BIN}" ]]; then
    echo "Binary bench_eedc_quick not found; run cmake --build build --target bench_eedc_quick"
    exit 1
fi

THREADS=${PE_EEDC_THREADS:-4}
CHUNKS=${PE_EEDC_CHUNK_SIZE_LIST:-"32 64 128"}
THRESHOLDS=${PE_EEDC_PARALLEL_THRESHOLD_LIST:-"200 400 800"}

for chunk in $CHUNKS; do
    for threshold in $THRESHOLDS; do
        echo
        echo "=== chunk=${chunk} threshold=${threshold} threads=${THREADS} ==="
        PE_EEDC_THREADS=$THREADS PE_EEDC_CHUNK_SIZE=$chunk PE_EEDC_PARALLEL_THRESHOLD=$threshold "$BIN"
    done
done
