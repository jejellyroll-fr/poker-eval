#!/usr/bin/env bash
set -euo pipefail

# Benchmark post-traitement Hold'em (AVX2 SIMD vs scalar)
# Sortie CSV: docs/reports/postproc_avx2.csv

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# Try common build output paths for the example binary
if [[ -x "${ROOT_DIR}/build_cli/examples/bench_holdem_postproc" ]]; then
  BIN="${ROOT_DIR}/build_cli/examples/bench_holdem_postproc"
elif [[ -x "${ROOT_DIR}/build/legacy/examples/bench_holdem_postproc" ]]; then
  BIN="${ROOT_DIR}/build/legacy/examples/bench_holdem_postproc"
elif [[ -x "${ROOT_DIR}/build_local/examples/bench_holdem_postproc" ]]; then
  BIN="${ROOT_DIR}/build_local/examples/bench_holdem_postproc"
elif [[ -x "${ROOT_DIR}/build_x64/examples/bench_holdem_postproc" ]]; then
  BIN="${ROOT_DIR}/build_x64/examples/bench_holdem_postproc"
elif [[ -x "${ROOT_DIR}/examples/bench_holdem_postproc" ]]; then
  BIN="${ROOT_DIR}/examples/bench_holdem_postproc"
else
  BIN=""
fi
OUTDIR="${ROOT_DIR}/docs/reports"
CSV="${OUTDIR}/postproc_avx2.csv"

# Defaults (can be overridden by env or CLI)
NITER="${NITER:-200000}"
PLAYERS_LIST_DEFAULT=(4 6)

# CLI args: --players "4 6 8" --niter 300000
PLAYERS_CLI=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --players)
      shift; PLAYERS_CLI=${1:-""}; shift || true ;;
    --niter)
      shift; NITER=${1:-$NITER}; shift || true ;;
    *) echo "[warn] arg inconnu: $1"; shift ;;
  esac
done

if [[ -n "${PLAYERS_CLI}" ]]; then
  # shellcheck disable=SC2206
  PLAYERS_LIST=(${PLAYERS_CLI})
elif [[ -n "${PLAYERS_LIST:-}" ]]; then
  # Allow env override as space-separated list
  # shellcheck disable=SC2206
  PLAYERS_LIST=(${PLAYERS_LIST})
else
  PLAYERS_LIST=(${PLAYERS_LIST_DEFAULT[@]})
fi

echo "=== Benchmark post-traitement (SIMD AVX2 vs OFF) ==="

if [[ -z "${BIN}" ]]; then
  echo "[error] Binaire introuvable: ${BIN} — veuillez construire les examples (make / cmake)."
  exit 1
fi

mkdir -p "${OUTDIR}"
echo "players,niter,off_ms,on_ms,speedup" >"${CSV}"

for P in "${PLAYERS_LIST[@]}"; do
  echo "-- N=${P}, niter=${NITER}"
  # Lancement et capture
  OUTPUT="$(${BIN} "${P}" "${NITER}")"
  echo "${OUTPUT}" | sed 's/^/    /'

  # Parsing simple
  OFF_MS=$(echo "${OUTPUT}" | awk '/SIMD OFF:/ {print $3}')
  ON_MS=$(echo "${OUTPUT}" | awk '/SIMD ON/ {print $3}')
  SPEEDUP=$(echo "${OUTPUT}" | awk '/Speedup:/ {print $2}' | sed 's/x//')

  if [[ -z "${OFF_MS}" || -z "${ON_MS}" || -z "${SPEEDUP}" ]]; then
    echo "[warn] parsing échoué pour N=${P}"
    continue
  fi

  echo "${P},${NITER},${OFF_MS},${ON_MS},${SPEEDUP}" >>"${CSV}"
done

echo "[info] écrit ${CSV}"

# Generate plots
if command -v python3 >/dev/null 2>&1; then
  export MPLBACKEND=Agg
  python3 "${ROOT_DIR}/scripts/plot_postproc_avx2.py" --csv "${CSV}" --outdir "${OUTDIR}" || true
fi

# Export Markdown summary
if command -v python3 >/dev/null 2>&1; then
  python3 "${ROOT_DIR}/scripts/export_postproc_markdown.py" \
    --csv "${CSV}" --out "${OUTDIR}/postproc_avx2_summary.md" || true
fi
