#!/usr/bin/env bash
set -euo pipefail

# tune_strat.sh - Grid search for STRATIFIED/other sampling weights
# Runs examples/strat_tuner across a small weight grid and aggregates results.
#
# Usage:
#   bash scripts/tune_strat.sh [--tuner-bin PATH] [--samples N] [--repeats N] \
#       [--batch N] [--scenarios list] [--policies list] \
#       [--weights-csv FILE] [--out OUT.csv] [--agg AGG.csv]
#
# Defaults:
#   --samples=20000 --repeats=3 --batch=256
#   --scenarios=flop,turn
#   --policies=MC,QMC,IMPORTANCE,STRATIFIED
#   If --weights-csv not provided, uses small grid:
#     w_distinct in {1.0,0.8,1.2}
#     w_onepair  in {1.75,1.5,2.0}
#     w_trips    in {3.0,2.5,3.5}

SAMPLES=20000
REPEATS=3
BATCH=256
SCENARIOS="flop,turn"
POLICIES="MC,QMC,IMPORTANCE,STRATIFIED"
WEIGHTS_CSV=""
OUT_CSV="strat_grid.csv"
AGG_CSV="strat_grid_agg.csv"
TUNER_BIN=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tuner-bin) TUNER_BIN="$2"; shift 2;;
    --samples) SAMPLES="$2"; shift 2;;
    --repeats) REPEATS="$2"; shift 2;;
    --batch) BATCH="$2"; shift 2;;
    --scenarios) SCENARIOS="$2"; shift 2;;
    --policies) POLICIES="$2"; shift 2;;
    --weights-csv) WEIGHTS_CSV="$2"; shift 2;;
    --out) OUT_CSV="$2"; shift 2;;
    --agg) AGG_CSV="$2"; shift 2;;
    -h|--help)
      echo "Usage: $0 [--tuner-bin PATH] [--samples N] [--repeats N] [--batch N] \";
      echo "             [--scenarios list] [--policies list] [--weights-csv FILE] \";
      echo "             [--out OUT.csv] [--agg AGG.csv]";
      exit 0;;
    *) echo "Unknown option: $1"; exit 1;;
  esac
done

# Resolve tuner binary if not provided
if [[ -z "$TUNER_BIN" ]]; then
  for cand in \
    "./examples/strat_tuner" \
    "./build/release/examples/strat_tuner" \
    "./build/default/examples/strat_tuner" \
    "./build/examples/strat_tuner"; do
    if [[ -x "$cand" ]]; then TUNER_BIN="$cand"; break; fi
  done
fi

if [[ -z "$TUNER_BIN" || ! -x "$TUNER_BIN" ]]; then
  echo "[error] strat_tuner binary not found. Build it first (examples/strat_tuner)." >&2
  exit 2
fi

echo "[info] Using tuner: $TUNER_BIN"
echo "[info] Output CSV: $OUT_CSV"
echo "[info] Aggregated CSV: $AGG_CSV"

tmpout="$(mktemp -t strat_grid.XXXXXX)"
trap 'rm -f "$tmpout"' EXIT

# Write header
echo "policy,scenario,samples,repeat,batch_size,equity_p1,equity_p2,tie,time_sec,w_distinct,w_onepair,w_trips" > "$tmpout"

IFS=',' read -r -a SC_ARR <<< "$SCENARIOS"

run_one() {
  local w0="$1" w1="$2" w2="$3" scen="$4"
  "$TUNER_BIN" --w="${w0},${w1},${w2}" --scenario="$scen" \
    --samples="$SAMPLES" --repeats="$REPEATS" --batch="$BATCH" \
    --policy="$POLICIES" >> "$tmpout"
}

if [[ -n "$WEIGHTS_CSV" ]]; then
  echo "[info] Reading weights from: $WEIGHTS_CSV"
  while IFS=, read -r w0 w1 w2; do
    # Skip header/empty
    [[ -z "$w0" || "$w0" =~ ^# ]] && continue
    for s in "${SC_ARR[@]}"; do
      run_one "$w0" "$w1" "$w2" "$s"
    done
  done < "$WEIGHTS_CSV"
else
  echo "[info] Using default small grid"
  W0_LIST=(1.0 0.8 1.2)
  W1_LIST=(1.75 1.5 2.0)
  W2_LIST=(3.0 2.5 3.5)
  for w0 in "${W0_LIST[@]}"; do
    for w1 in "${W1_LIST[@]}"; do
      for w2 in "${W2_LIST[@]}"; do
        for s in "${SC_ARR[@]}"; do
          run_one "$w0" "$w1" "$w2" "$s"
        done
      done
    done
  done
fi

mv "$tmpout" "$OUT_CSV"
echo "[info] Wrote $OUT_CSV"

# Aggregate by (policy,scenario,samples,batch,w0,w1,w2)
awk -F, 'NR==1{next} {
  key=$1","$2","$3","$5","$(NF-2)","$(NF-1)","$NF;
  kcnt[key]++;
  ksum_eq1[key]+=$6; ksum_eq2[key]+=$7; ksum_tie[key]+=$8; ksum_time[key]+=$9;
} END {
  print "policy,scenario,samples,batch,w_distinct,w_onepair,w_trips,eq1_mean,eq2_mean,tie_mean,time_mean,count";
  for (k in kcnt) {
    split(k, a, ",");
    c=kcnt[k];
    printf "%s,%s,%s,%s,%s,%s,%s,%.6f,%.6f,%.6f,%.6f,%d\n",
      a[1],a[2],a[3],a[4],a[5],a[6],a[7],ksum_eq1[k]/c,ksum_eq2[k]/c,ksum_tie[k]/c,ksum_time[k]/c,c;
  }
}' "$OUT_CSV" | sort -t, -k1,1 -k2,2 -k5,5 -k6,6 -k7,7 > "$AGG_CSV"

echo "[info] Wrote $AGG_CSV"
