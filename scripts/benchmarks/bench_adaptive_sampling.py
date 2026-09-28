#!/usr/bin/env python3
"""Adaptive-variance sampling benchmark (issue #256).

Solves four sampled spots with pe-preflop-solve (Lane B external sampling)
under three sampling policies, at the same iteration count, over several
seeds:

  standard   one draw per chance visit
  fixed      R draws per visit: the adaptive machinery with min = max = R, so
             the estimator and its 1/R weighting are the same as adaptive
  adaptive   R chosen per visit group from the measured variance

For each run it reports wall-clock, terminal evaluations, the adaptive group's
average draws, and the solution quality as the empirical NashConv of the
final average strategy (sampled best response, mBB per game). Quality and
cost are averaged over the seeds; the spread shows the noise of both the
solve and the sampled best response.

The spots are dealt by the preflop game, whose chance draws are the private
deals; boards are sampled at showdown (--samples). The multiway postflop
adapter is not used: its tree-node state cache is not re-entrant, and
replicated chance draws there hit that pre-existing bug (see
docs/cfr/guides/adaptive_variance_sampling.md).

    scripts/benchmarks/bench_adaptive_sampling.py --build build
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

# Full private ranges: every hand is possible, so the spots have genuinely
# mixed strategies and the NashConv of the result reflects the sampling. (With
# narrow premium ranges the strategies saturate and every policy scores the
# same.)
SPOTS = [
    # name, game, players
    ("holdem-hu", "holdem", 2),
    ("plo4-hu", "plo4", 2),
    ("plo5-hu", "plo5", 2),
    ("plo4-3way", "plo4", 3),
]


def solve(binary, spot, iterations, seed, samples, br_samples, policy,
          adaptive, out_json):
    _, game, players = spot
    cmd = [binary, "--game", game, "--players", str(players),
           "--algorithm", "external-mccfr", "--iterations", str(iterations),
           "--samples", str(samples), "--br-samples", str(br_samples),
           "--exploitability-interval", str(iterations),
           "--seed", str(seed), "--sampling-policy", policy,
           "--output", out_json]
    for key, value in (adaptive or {}).items():
        cmd += [f"--adaptive-{key}", str(value)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.perf_counter() - start
    out = proc.stdout + proc.stderr
    if proc.returncode != 0:
        sys.exit(f"solve failed: {' '.join(cmd)}\n{out[-2000:]}")
    with open(out_json) as f:
        metrics = json.load(f)["metrics"]
    evals = re.search(r"terminal_evaluations=(\d+)", out)
    draws = re.findall(r"adaptive_stats group=\S+ .*?avg_samples=([\d.]+)", out)
    variance = re.findall(r"adaptive_stats group=\S+ .*? variance=(\S+)", out)
    return {
        "variance": float(variance[0]) if variance else 0.0,
        "wall": wall,
        "evals": int(evals.group(1)) if evals else 0,
        "nash_conv": float(metrics["nash_conv_mbb_per_game"]),
        "draws": float(draws[0]) if draws else 1.0,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True, help="CMake build directory")
    ap.add_argument("--iterations", type=int, default=2000)
    ap.add_argument("--seeds", type=int, default=5)
    ap.add_argument("--samples", type=int, default=4,
                    help="showdown boards per called terminal")
    ap.add_argument("--br-samples", type=int, default=4000)
    ap.add_argument("--replicates", type=int, default=8,
                    help="R of the fixed policy, and the adaptive maximum")
    ap.add_argument("--tolerance", type=float, default=0.0,
                    help="adaptive absolute tolerance; 0 calibrates it per "
                         "spot from a pilot run (see --target-draws)")
    ap.add_argument("--target-draws", type=float, default=4.0,
                    help="calibration: the tolerance a pilot's measured "
                         "variance would meet with this many draws")
    ap.add_argument("--spot", action="append", help="only these spots")
    args = ap.parse_args()

    binary = os.path.join(args.build, "tools", "pe-preflop-solve")

    def policies_for(tolerance):
        return [
            ("standard", "standard", None),
            (f"fixed R={args.replicates}", "adaptive-variance",
             {"min-samples": args.replicates, "max-samples": args.replicates}),
            ("adaptive", "adaptive-variance",
             {"min-samples": 2, "max-samples": args.replicates,
              "absolute-tolerance": f"{tolerance:.6g}",
              "check-interval": 64}),
        ]
    spots = [s for s in SPOTS if not args.spot or s[0] in args.spot]

    print(f"pe-preflop-solve, external MCCFR, {args.iterations} iterations, "
          f"{args.seeds} seeds, {args.samples} showdown boards per terminal, "
          f"{args.br_samples} best-response samples")
    print(f"{'spot':<10} {'policy':<11} {'wall s':>7} {'terminal evals':>15} "
          f"{'avg draws':>9} {'NashConv mBB/game':>22}")
    with tempfile.TemporaryDirectory() as tmp:
        out_json = os.path.join(tmp, "run.json")
        for spot in spots:
            tolerance = args.tolerance
            if tolerance <= 0.0:
                # The sampled values carry the deal sampler's importance
                # weights, whose scale is the game's own: calibrate the
                # tolerance on it. z = 1.96 at the default 95% confidence.
                pilot = solve(binary, spot, 300, 999, args.samples, 10,
                              "adaptive-variance",
                              {"min-samples": 2, "max-samples": 2}, out_json)
                tolerance = 1.96 * (pilot["variance"] / args.target_draws) ** 0.5
            print(f"{spot[0]:<10} adaptive absolute tolerance {tolerance:.3g}")
            for label, policy, adaptive in policies_for(tolerance):
                runs = [solve(binary, spot, args.iterations, 1000 + s,
                              args.samples, args.br_samples, policy, adaptive,
                              out_json)
                        for s in range(args.seeds)]
                nash = [r["nash_conv"] for r in runs]
                spread = statistics.stdev(nash) if len(nash) > 1 else 0.0
                print(f"{spot[0]:<10} {label:<11} "
                      f"{statistics.mean(r['wall'] for r in runs):>7.2f} "
                      f"{statistics.mean(r['evals'] for r in runs):>15,.0f} "
                      f"{statistics.mean(r['draws'] for r in runs):>9.2f} "
                      f"{statistics.mean(nash):>14.1f} +- {spread:>5.1f}")
                sys.stdout.flush()


if __name__ == "__main__":
    main()
