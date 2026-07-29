#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Locate binary
BIN=""
for cand in \
  "${ROOT_DIR}/build_cli/examples/bench_batched_simd_cache" \
  "${ROOT_DIR}/build/legacy/examples/bench_batched_simd_cache" \
  "${ROOT_DIR}/build_local/examples/bench_batched_simd_cache" \
  "${ROOT_DIR}/build_x64/examples/bench_batched_simd_cache" \
  "${ROOT_DIR}/examples/bench_batched_simd_cache"; do
  [[ -x "$cand" ]] && BIN="$cand" && break
done

if [[ -z "$BIN" ]]; then
  echo "[error] bench_batched_simd_cache not found. Build examples first." >&2
  exit 1
fi

OUTDIR="${ROOT_DIR}/docs/reports"
CSV="${OUTDIR}/batched_simd_cache.csv"
NITER="${NITER:-500000}"
PLAYERS_LIST=(2 4 6)

mkdir -p "$OUTDIR"
echo "players,niter,simd,cache,time_ms" > "$CSV"

for P in "${PLAYERS_LIST[@]}"; do
  echo "-- N=${P}, niter=${NITER}"
  "$BIN" "$P" "$NITER" >> "$CSV"
done

echo "[info] wrote $CSV"

# Plots
if command -v python3 >/dev/null 2>&1; then
  export MPLBACKEND=Agg
  python3 "${ROOT_DIR}/scripts/plot_batched_simd_cache.py" \
    --csv "$CSV" --outdir "$OUTDIR" || true
  python3 "${ROOT_DIR}/scripts/export_batched_simd_cache_md.py" \
    --csv "$CSV" --out "${OUTDIR}/batched_simd_cache_summary.md" || true
fi

echo "[info] Plots + summary in $OUTDIR"

