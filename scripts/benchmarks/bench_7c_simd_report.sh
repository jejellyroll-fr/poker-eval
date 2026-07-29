#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR="${ROOT_DIR}/build_local"
BIN="${BUILD_DIR}/bin/bench_7c_simd_micro"
OUT_DIR="${ROOT_DIR}/docs/reports"
CSV="${OUT_DIR}/7c_simd_report.csv"

mkdir -p "${OUT_DIR}"

if [ ! -x "${BIN}" ]; then
  echo "Building bench_7c_simd_micro..."
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}" -j 8 --target bench_7c_simd_micro
fi

echo "mode,samples,iters,evals,seconds,evals_per_sec,nfs_ratio,speedup,set_type" > "${CSV}"
REPEATS=5
SIZES=(2000 5000 10000 20000)
for MODE in random nfs-heavy sf-heavy; do
  for S in "${SIZES[@]}"; do
    for R in $(seq 1 ${REPEATS}); do
      SEED=$((12345 + R*97 + S))
      "${BIN}" --samples "$S" --iters 20 --seed ${SEED} --mode ${MODE} --csv "${CSV}" >/dev/null
    done
  done
done

echo "Report written: ${CSV}"
