#!/usr/bin/env bash
set -euo pipefail

# Exécute le bench de variance multi‑scénarios puis agrège et trace

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# Try common build output paths for the example binary
if [[ -x "${ROOT_DIR}/build_cli/examples/benchmark_variance_qmc" ]]; then
  BIN="${ROOT_DIR}/build_cli/examples/benchmark_variance_qmc"
elif [[ -x "${ROOT_DIR}/build/legacy/examples/benchmark_variance_qmc" ]]; then
  BIN="${ROOT_DIR}/build/legacy/examples/benchmark_variance_qmc"
elif [[ -x "${ROOT_DIR}/build_local/examples/benchmark_variance_qmc" ]]; then
  BIN="${ROOT_DIR}/build_local/examples/benchmark_variance_qmc"
elif [[ -x "${ROOT_DIR}/build_x64/examples/benchmark_variance_qmc" ]]; then
  BIN="${ROOT_DIR}/build_x64/examples/benchmark_variance_qmc"
elif [[ -x "${ROOT_DIR}/examples/benchmark_variance_qmc" ]]; then
  BIN="${ROOT_DIR}/examples/benchmark_variance_qmc"
else
  BIN=""
fi
RAW="${ROOT_DIR}/variance_raw.csv"
SUMMARY="${ROOT_DIR}/variance_summary.csv"
OUTDIR="${ROOT_DIR}/docs/reports"

echo "=== Bench variance QMC/MC (multi‑scénarios) ==="
if [[ -z "${BIN}" ]]; then
  echo "[error] Binaire introuvable: ${BIN} — veuillez construire les examples (make / cmake)."
  exit 1
fi

"${BIN}" --cases "${ROOT_DIR}/examples/cases.csv" >"${RAW}"
echo "[info] écrit ${RAW}"

python3 "${ROOT_DIR}/scripts/aggregate_variance.py" --raw "${RAW}" --out "${SUMMARY}"

export MPLBACKEND=Agg
python3 "${ROOT_DIR}/scripts/plot_variance_summary.py" \
  --csv "${SUMMARY}" \
  --outdir "${OUTDIR}" \
  --per_scenario --plot_time \
  --title "Erreur absolue vs échantillons"

python3 "${ROOT_DIR}/scripts/plot_variance_summary.py" \
  --csv "${SUMMARY}" \
  --outdir "${OUTDIR}" \
  --title "Erreur absolue vs échantillons (global)"

echo "[info] Plots disponibles dans ${OUTDIR}"

# Export Markdown résumé
python3 "${ROOT_DIR}/scripts/export_variance_markdown.py" \
  --csv "${SUMMARY}" \
  --out "${OUTDIR}/variance_summary.md"
