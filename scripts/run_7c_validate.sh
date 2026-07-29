#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR="${ROOT_DIR}/build_local"
BIN="${BUILD_DIR}/bin/bench_7c_validate"
OUT_DIR="${ROOT_DIR}/docs/reports"
mkdir -p "${OUT_DIR}"

if [ ! -x "${BIN}" ]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}" -j 8 --target bench_7c_validate
fi

for MODE in random nfs-heavy sf-heavy; do
  CSV="${OUT_DIR}/7c_validate_${MODE}.csv"
  "${BIN}" --samples 20000 --seed 12345 --mode ${MODE} --csv "${CSV}"
done

echo "Validation CSVs written in ${OUT_DIR}"

