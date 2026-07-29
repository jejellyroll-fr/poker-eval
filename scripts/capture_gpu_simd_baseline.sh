#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
LOG_FILE="${LOG_FILE:-${ROOT_DIR}/tmp/gpu_simd_baseline.log}"

mkdir -p "${ROOT_DIR}/tmp"

exec > >(tee "${LOG_FILE}") 2>&1

echo "==== GPU/SIMD baseline capture: $(date) ===="
echo
find_bench_eedc_quick() {
    if [[ -x "${BUILD_DIR}/src/examples/bench_eedc_quick" ]]; then
        printf '%s' "${BUILD_DIR}/src/examples/bench_eedc_quick"
        return 0
    fi
    find "${ROOT_DIR}/build" -type f -name bench_eedc_quick -perm /111 -print -quit
}

BENCH_EEDC="$(find_bench_eedc_quick)"
if [[ -z "${BENCH_EEDC}" ]]; then
    echo "bench_eedc_quick binary not found; run cmake --build --target bench_eedc_quick" >&2
    exit 1
fi

echo ">>> Bench EEDC tuning (prefilter + combo trace)"
BENCH_BIN="${BENCH_EEDC}" \
PE_RANGE_BUFFER_MODE=prefilter PE_RANGE_COMBO_TRACE=1 \
scripts/bench_eedc_tuning.sh
echo
echo ">>> Multiway equity CLI baseline"
find_executable() {
    local name="$1"
    shift
    if [[ -n "${BUILD_DIR}" ]] && [[ -x "${BUILD_DIR}/${name}" ]]; then
        printf '%s' "${BUILD_DIR}/${name}"
        return 0
    fi
    find "${ROOT_DIR}/build" -type f -name "${name}" -perm /111 -print -quit
}

MULTIWAY_CLI="$(find_executable "src/examples/multiway_equity_cli")"
if [[ -z "${MULTIWAY_CLI}" ]]; then
    MULTIWAY_CLI="$(find_executable "multiway_equity_cli")"
fi
if [[ -z "${MULTIWAY_CLI}" ]]; then
    echo "multiway_equity_cli executable not found; run cmake --build for this configuration" >&2
    exit 1
fi

PE_RANGE_BUFFER_MODE=prefilter PE_RANGE_COMBO_TRACE=1 \
PE_EEDC_THREADS=4 PE_EEDC_CHUNK_SIZE=64 PE_EEDC_PARALLEL_THRESHOLD=400 \
"${MULTIWAY_CLI}" \
    --range AA --range KK --range QQ --invested 120,80,60 --method exhaustive
echo
echo "Logs stored in ${LOG_FILE}"
