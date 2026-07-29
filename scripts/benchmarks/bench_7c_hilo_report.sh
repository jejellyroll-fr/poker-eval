#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR="${ROOT_DIR}/build_local"
BIN="${BUILD_DIR}/bin/bench_7c_hilo_singlepass"
OUT_DIR="${ROOT_DIR}/docs/reports"
CSV="${OUT_DIR}/7c_hilo_report.csv"

mkdir -p "${OUT_DIR}"

if [ ! -x "${BIN}" ]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}" -j 8 --target bench_7c_hilo_singlepass
fi

echo "samples,mode,simd,single_pass_sec,naive_sec,speedup" > "${CSV}"

for MODE in low-friendly random; do
  for S in 2000 5000 10000 20000; do
    OUT=$(${BIN} --samples ${S} --mode ${MODE})
    SP=$(echo "$OUT" | awk '/Single-pass/{print $2}')
    NA=$(echo "$OUT" | awk '/Naive split/{print $3}')
    SD=$(echo "$OUT" | awk -F'[()= ,]+' '/samples/{print $(NF-1)}')
    SPU=$(echo "$OUT" | awk '/Speedup/{print $2}')
    echo "${S},${MODE},${SD},${SP},${NA},${SPU}" >> "${CSV}"
  done
done

echo "Wrote ${CSV}"

