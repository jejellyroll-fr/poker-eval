#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Locate binary
BIN=""
for cand in \
  "${ROOT_DIR}/build_cli/examples/bench_holdem_canonicalize_7" \
  "${ROOT_DIR}/build/legacy/examples/bench_holdem_canonicalize_7" \
  "${ROOT_DIR}/build_local/examples/bench_holdem_canonicalize_7" \
  "${ROOT_DIR}/build_x64/examples/bench_holdem_canonicalize_7" \
  "${ROOT_DIR}/examples/bench_holdem_canonicalize_7"; do
  [[ -x "$cand" ]] && BIN="$cand" && break
done

if [[ -z "$BIN" ]]; then
  echo "[error] bench_holdem_canonicalize_7 not found. Build examples first." >&2
  exit 1
fi

OUTDIR="${ROOT_DIR}/docs/reports"
CSV="${OUTDIR}/holdem_canon7.csv"
mkdir -p "$OUTDIR"

echo "stage,unique_classes,time_sec" > "$CSV"

for STAGE in preflop flop turn river; do
  echo "-- stage=${STAGE}"
  # bench prints: stage,samples,unique_classes,time_sec (with header)
  # Extract the data row, keep stage,unique,time
  "$BIN" "$STAGE" | awk -F, 'NR==2{printf "%s,%s,%s\n", $1, $3, $4}' >> "$CSV"
done

echo "[info] wrote $CSV"

# Export Markdown summary
if command -v python3 >/dev/null 2>&1; then
  python3 "${ROOT_DIR}/scripts/export_holdem_canon7_md.py" \
    --csv "$CSV" --out "${OUTDIR}/holdem_canon7_summary.md" || true
fi

echo "[info] Summary in ${OUTDIR}/holdem_canon7_summary.md"

