#!/usr/bin/env python3
"""Confidence-guided best-response benchmark (issue #257).

Best-response measurement never touches training, so for a fixed seed and
iteration count every run below solves the same strategy; only the final
empirical best response differs. Each spot is measured four ways:

  one rollout  the historical estimate: one rollout per action, the maximum
               taken (biased upward by that maximum)
  fixed N      N rollouts per action, every decision (the reference)
  adaptive     up to N per action, stopping once the best action is
               separated from the rest at the configured confidence
  adaptive+tol the same, also stopping once the remaining gap is within an
               absolute tolerance

and reports the measurement's terminal evaluations, draws per decision, the
share of decisions stopped before the cap, and the NashConv each measurement
reports. The comparison with the fixed-N reference is paired: for each seed
the four measurements see the same solved strategy, so the mean and spread of
their per-seed difference separate a systematic shift from the measurement's
own sampling noise.

    scripts/benchmarks/bench_br_sampling.py --build build
"""

import argparse
import json
import statistics
import os
import re
import subprocess
import sys
import tempfile
import time

SPOTS = [
    # name, game, players (full private ranges)
    ("holdem-hu", "holdem", 2),
    ("plo4-hu", "plo4", 2),
    ("plo5-hu", "plo5", 2),
    ("plo4-3way", "plo4", 3),
]


def measure(binary, spot, args, seed, br_flags, out_json):
    _, game, players = spot
    cmd = [binary, "--game", game, "--players", str(players),
           "--algorithm", "external-mccfr",
           "--iterations", str(args.iterations),
           "--samples", str(args.samples),
           "--br-samples", str(args.br_samples),
           "--exploitability-interval", str(args.iterations),
           "--seed", str(seed), "--output", out_json] + br_flags
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.perf_counter() - start
    out = proc.stdout + proc.stderr
    if proc.returncode != 0:
        sys.exit(f"solve failed: {' '.join(cmd)}\n{out[-2000:]}")
    with open(out_json) as f:
        metrics = json.load(f)["metrics"]
    evals = re.findall(
        r"br_sampling (?:estimator=(\S+) )?terminal_evaluations=(\d+)", out)
    decisions = re.findall(
        r"br_decisions decisions=(\d+) samples=(\d+) avg_samples=([\d.]+) "
        r"early_stop_pct=([\d.]+)", out)
    last = decisions[-1] if decisions else None
    return {
        "wall": wall,
        # Issue #274: the estimator is the field before terminal_evaluations.
        # Older solvers omit it, so the group is optional and None means
        # "unknown", never a guess.
        "estimator": evals[-1][0] if evals else None,
        "evals": int(evals[-1][1]) if evals else 0,
        "nash_conv": float(metrics["nash_conv_mbb_per_game"]),
        "avg_draws": float(last[2]) if last else None,
        "early": float(last[3]) if last else None,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True, help="CMake build directory")
    ap.add_argument("--iterations", type=int, default=500)
    ap.add_argument("--samples", type=int, default=4,
                    help="showdown boards per called terminal")
    ap.add_argument("--br-samples", type=int, default=2000,
                    help="best-response trajectories per player")
    ap.add_argument("--max", type=int, default=64,
                    help="rollouts per action: the fixed budget and the cap")
    ap.add_argument("--tolerance", type=float, default=5.0,
                    help="absolute tolerance of adaptive+tol (chips)")
    ap.add_argument("--seeds", type=int, default=5)
    ap.add_argument("--spot", action="append", help="only these spots")
    args = ap.parse_args()

    binary = os.path.join(args.build, "tools", "pe-preflop-solve")
    n = str(args.max)
    # Issue #274: the sampled BR defaults to the confidence-guided estimator,
    # so the historical one has to be asked for explicitly -- otherwise this
    # row silently becomes a second adaptive measurement and the published
    # baseline is mislabeled. Each variant also names the estimator it
    # expects, so a future change to the default fails loudly here rather
    # than in the table.
    variants = [
        ("one rollout", ["--br-max-samples", "0"], "one-rollout"),
        (f"fixed {n}", ["--br-min-samples", n, "--br-max-samples", n],
         "confidence-guided"),
        ("adaptive", ["--br-min-samples", "4", "--br-max-samples", n],
         "confidence-guided"),
        ("adaptive+tol", ["--br-min-samples", "4", "--br-max-samples", n,
                          "--br-absolute-tolerance", str(args.tolerance)],
         "confidence-guided"),
    ]
    spots = [s for s in SPOTS if not args.spot or s[0] in args.spot]

    print(f"pe-preflop-solve, external MCCFR, {args.iterations} iterations, "
          f"{args.br_samples} BR trajectories per player, {args.seeds} seeds; "
          f"per seed, every measurement sees the same solved strategy")
    print(f"{'spot':<10} {'measurement':<13} {'wall s':>7} "
          f"{'BR terminal evals':>18} {'draws/dec':>10} {'early %':>8} "
          f"{'NashConv mBB':>13} {'paired diff vs fixed':>22}")
    with tempfile.TemporaryDirectory() as tmp:
        out_json = os.path.join(tmp, "run.json")
        for spot in spots:
            runs = {label: [measure(binary, spot, args, 100 + k, flags,
                                    out_json)
                            for k in range(args.seeds)]
                    for label, flags, _ in variants}
            # The row label is a claim about which estimator produced the
            # numbers; refuse to publish it when the solver says otherwise.
            for label, _, expected in variants:
                for r in runs[label]:
                    if r["estimator"] != expected:
                        sys.exit(f"{spot[0]} {label}: expected the {expected} "
                                 f"estimator, the solver reported "
                                 f"{r['estimator']!r}")
            reference = runs[variants[1][0]]
            for label, _, _ in variants:
                rs = runs[label]
                diffs = [r["nash_conv"] - f["nash_conv"]
                         for r, f in zip(rs, reference)]
                spread = statistics.stdev(diffs) if len(diffs) > 1 else 0.0
                draws = [r["avg_draws"] for r in rs if r["avg_draws"] is not None]
                early = [r["early"] for r in rs if r["early"] is not None]
                print(f"{spot[0]:<10} {label:<13} "
                      f"{statistics.mean(r['wall'] for r in rs):>7.2f} "
                      f"{statistics.mean(r['evals'] for r in rs):>18,.0f} "
                      f"{(f'{statistics.mean(draws):.1f}' if draws else '-'):>10} "
                      f"{(f'{statistics.mean(early):.1f}' if early else '-'):>8} "
                      f"{statistics.mean(r['nash_conv'] for r in rs):>13.1f} "
                      f"{statistics.mean(diffs):>+12.1f} +- {spread:>6.1f}")
                sys.stdout.flush()


if __name__ == "__main__":
    main()
