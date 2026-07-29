#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR="${ROOT_DIR}/build_local"
BIN_RIVER="${BUILD_DIR}/bin/bench_cfr_holdem_river"
BIN_TURN="${BUILD_DIR}/bin/bench_cfr_holdem_turn"
OUT_DIR="${ROOT_DIR}/docs/reports"

DEALS=${CFR_SUMMARY_DEALS:-50}
ITERS=${CFR_SUMMARY_ITERS:-500}

mkdir -p "${OUT_DIR}"

# Ensure benches are built
if [ ! -x "${BIN_RIVER}" ] || [ ! -x "${BIN_TURN}" ]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}" -j 8 --target bench_cfr_holdem_river bench_cfr_holdem_turn
fi

# Run multi-mode for river and turn
CSV_LIST=()
for MODE in 0 1 2 3; do
  CSV_RIVER="${OUT_DIR}/cfr_infosets_river_m${MODE}.csv"
  CSV_TURN="${OUT_DIR}/cfr_infosets_turn_m${MODE}.csv"
  echo "Running river mode ${MODE} → ${CSV_RIVER}"
  "${BIN_RIVER}" --deals "${DEALS}" --iters "${ITERS}" --csv "${CSV_RIVER}" --bucket-mode "${MODE}"
  echo "Running turn  mode ${MODE} → ${CSV_TURN}"
  "${BIN_TURN}"  --deals "${DEALS}" --iters "${ITERS}" --csv "${CSV_TURN}"  --bucket-mode "${MODE}"
  CSV_LIST+=("${CSV_RIVER}" "${CSV_TURN}")
done

# Plot summary (infosets vs proxy, infosets vs time)
PROXY_PNG="${OUT_DIR}/cfr_infosets_proxy.png"
TIME_PNG="${OUT_DIR}/cfr_infosets_time.png"
python3 "${ROOT_DIR}/scripts/plot_cfr_infosets_vs_proxy_time.py" "${CSV_LIST[@]}" "${PROXY_PNG}" "${TIME_PNG}"
echo "Wrote ${PROXY_PNG} and ${TIME_PNG}"

