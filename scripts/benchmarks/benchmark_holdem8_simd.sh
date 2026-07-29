#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

BIN=""
for cand in \
  "${ROOT_DIR}/build_cli/examples/bench_holdem8_simd" \
  "${ROOT_DIR}/build/legacy/examples/bench_holdem8_simd" \
  "${ROOT_DIR}/build_local/examples/bench_holdem8_simd" \
  "${ROOT_DIR}/build_x64/examples/bench_holdem8_simd" \
  "${ROOT_DIR}/examples/bench_holdem8_simd"; do
  [[ -x "$cand" ]] && BIN="$cand" && break
done

if [[ -z "$BIN" ]]; then
  echo "[error] bench_holdem8_simd not found. Build examples first." >&2
  exit 1
fi

OUTDIR="${ROOT_DIR}/docs/reports"
CSV="${OUTDIR}/holdem8_simd.csv"
NITER="${NITER:-200000}"
PLAYERS_LIST=(2 4 6)

mkdir -p "$OUTDIR"
echo "players,niter,off_ms,on_ms,speedup" > "$CSV"

for P in "${PLAYERS_LIST[@]}"; do
  OUT="$($BIN "$P" "$NITER")"
  echo "$OUT" | sed -n '1,3p' | sed 's/^/    /'
  OFF=$(echo "$OUT" | awk '/SIMD OFF:/ {print $3}')
  ON=$(echo "$OUT" | awk '/SIMD ON/ {print $3}')
  SPD=$(echo "$OUT" | awk '/Speedup:/ {print $2}' | sed 's/x//')
  if [[ -n "$OFF" && -n "$ON" && -n "$SPD" ]]; then
    echo "${P},${NITER},${OFF},${ON},${SPD}" >> "$CSV"
  fi
done

echo "[info] wrote $CSV"

