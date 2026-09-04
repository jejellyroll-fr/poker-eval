#!/usr/bin/env bash
# Solve the heads-up PLO5/PLO6 trees in this directory.
#
#   ./run.sh              # both variants, all four streets, one run each
#   ./run.sh plo6 turn    # one variant, one street
#   ./run.sh plo5 full    # the single preflop..river tree, one run
#
# Two shapes are provided.  The *_hu_full tree spans preflop through river in
# one file, the way Monker Solver does it: the round-closing action of each
# street wires straight to the first node of the next, so one run solves the
# whole thing.  The per-street trees remain useful when you want to work one
# street at a time with a chosen board and pot.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/../../build}"
SOLVER="${SOLVER:-${BUILD_DIR}/tools/pe-preflop-solve}"
ITERATIONS="${ITERATIONS:-2000}"

if [[ ! -x "${SOLVER}" ]]; then
    echo "Build it first: cmake --build build --target pe-preflop-solve" >&2
    echo "(or point SOLVER= at another pe-preflop-solve)" >&2
    exit 1
fi

# Board and pot per street, matching the snapshots inside the trees: one
# 100bb line where a pot bet is called on every street.
board_for() { case "$1" in
    flop)  echo "Ks9d4c" ;;
    turn)  echo "Ks9d4c2h" ;;
    river) echo "Ks9d4c2hTs" ;;
esac; }
pot_for() { case "$1" in
    flop)  echo 7 ;;
    turn)  echo 21 ;;
    river) echo 63 ;;
esac; }

solve_one() {
    local variant="$1" street="$2"
    local tree="${ROOT_DIR}/${variant}_hu_${street}.tree.json"
    local -a args=(
        --game "${variant}" --players 2 --tree "${tree}"
        --iterations "${ITERATIONS}" --samples 1 --br-samples 32
        --exploitability-interval 256
        --algorithm external-mccfr --backend cpu_ref --precision f64 --threads 1
        --range0 "100%" --range1 "100%"
    )
    # The full tree is rooted preflop and deals its own boards, so it takes
    # neither --street nor --board nor --pot.
    if [[ "${street}" != "preflop" && "${street}" != "full" ]]; then
        args+=(--street "${street}" --board "$(board_for "${street}")" --pot "$(pot_for "${street}")")
    fi
    echo "=== ${variant} ${street} ==="
    "${SOLVER}" "${args[@]}" 2>/dev/null | grep -E '^tree_step|^iterations=|^guarantee'
    echo
}

variants=("${1:-plo5 plo6}")
streets=("${2:-preflop flop turn river}")
for variant in ${variants[@]}; do
    for street in ${streets[@]}; do
        solve_one "${variant}" "${street}"
    done
done
