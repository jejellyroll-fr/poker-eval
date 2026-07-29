#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR="${ROOT_DIR}/build_local"
BIN="${BUILD_DIR}/bin/bench_cfr_holdem_river"
OUT_DIR="${ROOT_DIR}/docs/reports"
DEALSET="${OUT_DIR}/cfr_river_dealset.txt"
CSV="${OUT_DIR}/cfr_river_report.csv"
BMODE=${CFR_BUCKET_MODE:-3}
BBINS=${CFR_BUCKET_BINS:-8}
BTHRESH=${CFR_BUCKET_THRESH:-}
BPRESET=${CFR_BUCKET_PRESET:-}
BSIZES=${CFR_BET_SIZES:-}
RCAP=${CFR_RAISE_CAP:-}
USE_DCFR=${CFR_DCFR:-}
ALPHA=${CFR_DCFR_ALPHA:-}
BETA=${CFR_DCFR_BETA:-}
GAMMA=${CFR_DCFR_GAMMA:-}
USE_ECFR=${CFR_ECFR:-}
LAMBDA=${CFR_ECFR_LAMBDA:-}
PROGRESS=${CFR_PROGRESS:-}
VERBOSE=${CFR_VERBOSE:-}
TRACE=${CFR_TRACE:-}
TREE_PROFILE=${CFR_TREE_PROFILE:-tight}
CHECKPOINT=${CFR_CHECKPOINT:-}
RESUME=${CFR_RESUME:-}
CHECKPOINT_FINAL=${CFR_CHECKPOINT_FINAL:-}
CHECKPOINT_INTERVAL=${CFR_CHECKPOINT_INTERVAL:-}

mkdir -p "${OUT_DIR}"

if [ ! -x "${BIN}" ]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}" -j 8 --target bench_cfr_holdem_river
fi

python3 "${ROOT_DIR}/scripts/generate_river_dealset.py" 100 "${DEALSET}" 424242
# Default or preset: fine intra-class thresholds (16 bins) unless explicit thresholds provided
if [ -n "${BTHRESH}" ]; then
  : # keep explicit thresholds
else
  case "${BPRESET}" in
    coarse)
      # 7 cut points → 8 bins
      BTHRESH="125000,250000,375000,500000,625000,750000,875000"
      ;;
    ultra)
      # Max supported by 4-bit coarse field is 16 bins → alias to fine (15 cut points)
      BTHRESH="62500,125000,187500,250000,312500,375000,437500,500000,562500,625000,687500,750000,812500,875000,937500"
      ;;
    fine|*)
      # 15 cut points → 16 bins, evenly spaced in [0..999999]
      BTHRESH="62500,125000,187500,250000,312500,375000,437500,500000,562500,625000,687500,750000,812500,875000,937500"
      ;;
  esac
fi
THRESH_ARGS=(--bucket-thresholds "${BTHRESH}")
# Stage-aware defaults if not provided
if [ -z "${BSIZES}" ]; then BSIZES="33,50,75,100"; fi
if [ -z "${RCAP}" ]; then RCAP="2"; fi
CSV_ARGS=(--csv "${CSV}")
if [ "${CFR_CSV_APPEND:-}" != "" ]; then
  CSV_ARGS+=(--csv-append)
fi
DCFR_ARGS=()
if [ -n "${USE_DCFR}" ]; then DCFR_ARGS+=(--dcfr); fi
if [ -n "${ALPHA}" ]; then DCFR_ARGS+=(--dcfr-alpha "${ALPHA}"); fi
if [ -n "${BETA}" ]; then DCFR_ARGS+=(--dcfr-beta "${BETA}"); fi
if [ -n "${GAMMA}" ]; then DCFR_ARGS+=(--dcfr-gamma "${GAMMA}"); fi
ECFR_ARGS=()
if [ -n "${USE_ECFR}" ]; then ECFR_ARGS+=(--ecfr); fi
if [ -n "${LAMBDA}" ]; then ECFR_ARGS+=(--ecfr-lambda "${LAMBDA}"); fi
TREE_ARGS=()
if [ -n "${BSIZES}" ]; then TREE_ARGS+=(--bet-sizes "${BSIZES}"); fi
if [ -n "${RCAP}" ]; then TREE_ARGS+=(--raise-cap "${RCAP}"); fi

if [ -z "${PROGRESS}" ] && [ -n "${VERBOSE}" ]; then
  PROGRESS=100
fi

PROGRESS_ARGS=()
if [ -n "${PROGRESS}" ]; then PROGRESS_ARGS+=(--progress "${PROGRESS}"); fi
if [ -n "${VERBOSE}" ]; then PROGRESS_ARGS+=(--verbose); fi
if [ -n "${TRACE}" ]; then PROGRESS_ARGS+=(--trace-cfr); fi
PROFILE_ARGS=()
if [ -n "${TREE_PROFILE}" ] && [ "${TREE_PROFILE}" != "none" ]; then
  PROFILE_ARGS+=(--tree-profile "${TREE_PROFILE}");
fi
MCCFVFP_ARGS=()
if [ -n "${CFR_MCCFVFP:-}" ]; then MCCFVFP_ARGS+=(--mccfvfp); fi
if [ -n "${CFR_MCCFVFP_FLOW:-}" ]; then MCCFVFP_ARGS+=(--flow-pow "${CFR_MCCFVFP_FLOW}"); fi
CHECKPOINT_ARGS=()
if [ -n "${CHECKPOINT}" ]; then CHECKPOINT_ARGS+=(--checkpoint "${CHECKPOINT}"); fi
if [ -n "${RESUME}" ]; then CHECKPOINT_ARGS+=(--resume "${RESUME}"); fi
if [ -n "${CHECKPOINT_FINAL}" ] && [ "${CHECKPOINT_FINAL}" != "0" ]; then CHECKPOINT_ARGS+=(--checkpoint-final); fi
if [ -n "${CHECKPOINT_INTERVAL}" ]; then CHECKPOINT_ARGS+=(--checkpoint-interval "${CHECKPOINT_INTERVAL}"); fi

CMD=(
  "${BIN}"
  --deals 100
  --iters 1000
  --dealset "${DEALSET}"
  "${CSV_ARGS[@]}"
  --bucket-mode "${BMODE}"
  --bucket-bins "${BBINS}"
  "${THRESH_ARGS[@]}"
)
if [ ${#DCFR_ARGS[@]} -gt 0 ]; then
  CMD+=("${DCFR_ARGS[@]}")
fi
if [ ${#ECFR_ARGS[@]} -gt 0 ]; then
  CMD+=("${ECFR_ARGS[@]}")
fi
if [ ${#TREE_ARGS[@]} -gt 0 ]; then
  CMD+=("${TREE_ARGS[@]}")
fi
if [ ${#PROGRESS_ARGS[@]} -gt 0 ]; then
  CMD+=("${PROGRESS_ARGS[@]}")
fi
if [ ${#PROFILE_ARGS[@]} -gt 0 ]; then
  CMD+=("${PROFILE_ARGS[@]}")
fi
if [ ${#MCCFVFP_ARGS[@]} -gt 0 ]; then
  CMD+=("${MCCFVFP_ARGS[@]}")
fi
if [ ${#CHECKPOINT_ARGS[@]} -gt 0 ]; then
  CMD+=("${CHECKPOINT_ARGS[@]}")
fi

"${CMD[@]}"
echo "Wrote ${CSV} (dealset: ${DEALSET})"
