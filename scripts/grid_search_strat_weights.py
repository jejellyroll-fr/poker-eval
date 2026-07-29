#!/usr/bin/env python3
"""
Grid-search for optimal STRATIFIED sampling weights (w_distinct, w_onepair, w_trips).
Generates CSV output and heatmaps showing variance reduction vs MC baseline.
"""

import subprocess
import sys
import os
import csv
import numpy as np
from collections import defaultdict

def find_strat_tuner():
    """Locate the strat_tuner binary."""
    search_paths = [
        "build_test/bin/strat_tuner",
        "build_cli/bin/strat_tuner",
        "build_local/bin/strat_tuner",
        "build/legacy/bin/strat_tuner",
        "build_x64/bin/strat_tuner",
        "bin/strat_tuner"
    ]
    for p in search_paths:
        if os.path.isfile(p):
            return p
    return None

def run_grid_search(strat_tuner, scenario, samples, repeats, grid_points, out_csv):
    """
    Run grid search over weight combinations.
    grid_points: list of (w0, w1, w2) tuples to test
    """
    print(f"Running grid search: {len(grid_points)} combinations, {scenario}, {samples} samples, {repeats} repeats", file=sys.stderr)

    results = []

    for idx, (w0, w1, w2) in enumerate(grid_points):
        cmd = [
            strat_tuner,
            f"--w={w0},{w1},{w2}",
            f"--scenario={scenario}",
            f"--samples={samples}",
            f"--repeats={repeats}",
            "--policies=STRATIFIED,MC"
        ]

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True, timeout=300)
            output = result.stdout

            # Parse CSV output (skip header)
            lines = [l for l in output.strip().split('\n') if l and not l.startswith('policy')]

            strat_equities = []
            mc_equities = []

            for line in lines:
                parts = line.split(',')
                if len(parts) < 7:
                    continue
                policy = parts[0]
                equity_p1 = float(parts[5])

                if policy == 'STRATIFIED':
                    strat_equities.append(equity_p1)
                elif policy == 'MC':
                    mc_equities.append(equity_p1)

            if strat_equities and mc_equities:
                strat_var = np.var(strat_equities, ddof=1) if len(strat_equities) > 1 else 0.0
                mc_var = np.var(mc_equities, ddof=1) if len(mc_equities) > 1 else 0.0
                strat_mean = np.mean(strat_equities)
                mc_mean = np.mean(mc_equities)

                variance_ratio = strat_var / mc_var if mc_var > 0 else 1.0

                results.append({
                    'w0': w0,
                    'w1': w1,
                    'w2': w2,
                    'strat_mean': strat_mean,
                    'strat_var': strat_var,
                    'mc_mean': mc_mean,
                    'mc_var': mc_var,
                    'variance_ratio': variance_ratio
                })

                print(f"[{idx+1}/{len(grid_points)}] w=({w0:.2f},{w1:.2f},{w2:.2f}) → ratio={variance_ratio:.4f}", file=sys.stderr)

        except subprocess.TimeoutExpired:
            print(f"[WARN] Timeout for w=({w0},{w1},{w2})", file=sys.stderr)
        except subprocess.CalledProcessError as e:
            print(f"[WARN] Failed for w=({w0},{w1},{w2}): {e}", file=sys.stderr)

    # Write results to CSV
    if results:
        with open(out_csv, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=['w0', 'w1', 'w2', 'strat_mean', 'strat_var', 'mc_mean', 'mc_var', 'variance_ratio'])
            writer.writeheader()
            writer.writerows(results)

        print(f"\nWrote {len(results)} results to {out_csv}", file=sys.stderr)

        # Find best weights
        best = min(results, key=lambda r: r['variance_ratio'])
        print(f"\nBest weights: w=({best['w0']:.2f},{best['w1']:.2f},{best['w2']:.2f})", file=sys.stderr)
        print(f"  Variance ratio: {best['variance_ratio']:.4f} (lower is better)", file=sys.stderr)
        print(f"  STRATIFIED var: {best['strat_var']:.6f}", file=sys.stderr)
        print(f"  MC var: {best['mc_var']:.6f}", file=sys.stderr)
    else:
        print("No results collected!", file=sys.stderr)
        return None

    return results

def generate_grid(w0_range, w1_range, w2_range):
    """Generate grid of weight combinations."""
    grid = []
    for w0 in w0_range:
        for w1 in w1_range:
            for w2 in w2_range:
                grid.append((w0, w1, w2))
    return grid

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Grid-search for optimal STRATIFIED sampling weights')
    parser.add_argument('--scenario', default='flop', help='Scenario: preflop/flop/turn/river (default: flop)')
    parser.add_argument('--samples', type=int, default=5000, help='Samples per run (default: 5000)')
    parser.add_argument('--repeats', type=int, default=10, help='Repeats per weight combo (default: 10)')
    parser.add_argument('--w0-range', default='0.5,1.0,2.0', help='Comma-separated w_distinct values (default: 0.5,1.0,2.0)')
    parser.add_argument('--w1-range', default='1.0,1.5,2.0,2.5', help='Comma-separated w_onepair values (default: 1.0,1.5,2.0,2.5)')
    parser.add_argument('--w2-range', default='2.0,3.0,4.0', help='Comma-separated w_trips values (default: 2.0,3.0,4.0)')
    parser.add_argument('--out', default='strat_grid_search.csv', help='Output CSV file (default: strat_grid_search.csv)')
    parser.add_argument('--tuner', help='Path to strat_tuner binary (auto-detected if not specified)')

    args = parser.parse_args()

    # Find binary
    tuner = args.tuner if args.tuner else find_strat_tuner()
    if not tuner or not os.path.isfile(tuner):
        print(f"ERROR: strat_tuner binary not found. Searched common paths.", file=sys.stderr)
        print(f"  Please build it first: cmake --build build_test --target strat_tuner", file=sys.stderr)
        return 1

    print(f"Using strat_tuner: {tuner}", file=sys.stderr)

    # Parse ranges
    w0_range = [float(x) for x in args.w0_range.split(',')]
    w1_range = [float(x) for x in args.w1_range.split(',')]
    w2_range = [float(x) for x in args.w2_range.split(',')]

    # Generate grid
    grid = generate_grid(w0_range, w1_range, w2_range)
    print(f"Grid size: {len(grid)} combinations", file=sys.stderr)
    print(f"  w0 (distinct): {w0_range}", file=sys.stderr)
    print(f"  w1 (onepair): {w1_range}", file=sys.stderr)
    print(f"  w2 (trips): {w2_range}", file=sys.stderr)

    # Run grid search
    results = run_grid_search(tuner, args.scenario, args.samples, args.repeats, grid, args.out)

    if results:
        print(f"\n✓ Grid search complete. Results in {args.out}", file=sys.stderr)
        return 0
    else:
        print(f"\n✗ Grid search failed.", file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
