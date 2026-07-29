#!/usr/bin/env python3
"""
analyze_bench_results.py - Analyze reproducible benchmark results

Reads CSV from bench_reproducible_suite and generates:
- Variance reduction summary
- Performance comparison tables
- Trend charts
"""

import sys
import csv
from collections import defaultdict

try:
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("Warning: matplotlib not available, skipping plots")

def load_results(csv_file):
    """Load benchmark results from CSV"""
    results = []

    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            results.append({
                'scenario': row['scenario'],
                'method': row['method'],
                'mean_value': float(row['mean_value']),
                'variance': float(row['variance']),
                'std_dev': float(row['std_dev']),
                'time_ms': float(row['time_ms']),
                'samples': int(row['samples'])
            })

    return results

def analyze_variance_reduction(results):
    """Analyze variance reduction for sampling methods"""
    print("\n" + "="*60)
    print("   Variance Reduction Analysis")
    print("="*60)

    # Group by scenario
    by_scenario = defaultdict(list)
    for r in results:
        by_scenario[r['scenario']].append(r)

    # Calculate reduction factors
    for scenario, methods in by_scenario.items():
        mc_variance = None
        qmc_variance = None
        importance_variance = None

        for m in methods:
            if m['method'] == 'MC':
                mc_variance = m['variance']
            elif m['method'] == 'QMC':
                qmc_variance = m['variance']
            elif m['method'] == 'Importance':
                importance_variance = m['variance']

        if mc_variance and mc_variance > 0:
            print(f"\n{scenario}:")
            print(f"  MC variance: {mc_variance:.6f}")

            if qmc_variance is not None:
                reduction = mc_variance / qmc_variance if qmc_variance > 0 else float('inf')
                print(f"  QMC variance: {qmc_variance:.6f} ({reduction:.2f}x reduction)")

            if importance_variance is not None:
                reduction = mc_variance / importance_variance if importance_variance > 0 else float('inf')
                print(f"  Importance variance: {importance_variance:.6f} ({reduction:.2f}x reduction)")

def analyze_cfr_convergence(results):
    """Analyze CFR convergence rates"""
    print("\n" + "="*60)
    print("   CFR Convergence Analysis")
    print("="*60)

    cfr_results = [r for r in results if 'CFR' in r['method']]

    # Group by scenario
    by_scenario = defaultdict(list)
    for r in cfr_results:
        by_scenario[r['scenario']].append(r)

    for scenario, methods in by_scenario.items():
        print(f"\n{scenario}:")
        for m in sorted(methods, key=lambda x: x['mean_value']):
            print(f"  {m['method']:12s}: exploitability={m['mean_value']:.4f}, time={m['time_ms']:.1f}ms")

def analyze_ofc_quality(results):
    """Analyze OFC placement quality"""
    print("\n" + "="*60)
    print("   OFC Placement Quality Analysis")
    print("="*60)

    ofc_results = [r for r in results if 'OFC' in r['scenario'] or 'game' in r['scenario'].lower()]

    # Group by scenario
    by_scenario = defaultdict(list)
    for r in ofc_results:
        by_scenario[r['scenario']].append(r)

    for scenario, methods in by_scenario.items():
        print(f"\n{scenario}:")
        for m in sorted(methods, key=lambda x: x['mean_value'], reverse=True):
            print(f"  {m['method']:15s}: score={m['mean_value']:.2f} (\u03c3={m['std_dev']:.2f}), time={m['time_ms']:.1f}ms")

def generate_summary_table(results):
    """Generate summary table"""
    print("\n" + "="*60)
    print("   Performance Summary Table")
    print("="*60)

    print("\n{:<30s} {:<15s} {:>12s} {:>12s} {:>10s}".format(
        "Scenario", "Method", "Mean", "Variance", "Time (ms)"))
    print("-" * 80)

    for r in results:
        print("{:<30s} {:<15s} {:>12.6f} {:>12.6f} {:>10.1f}".format(
            r['scenario'][:30],
            r['method'],
            r['mean_value'],
            r['variance'],
            r['time_ms']))

def plot_variance_reduction(results, output_file='variance_reduction.png'):
    """Plot variance reduction chart"""
    if not HAS_MATPLOTLIB:
        return

    # Filter equity results
    equity_results = [r for r in results if any(x in r['scenario'] for x in ['vs', 'way', 'Set', 'Flush'])]

    # Group by scenario
    by_scenario = defaultdict(dict)
    for r in equity_results:
        by_scenario[r['scenario']][r['method']] = r['variance']

    # Create bar chart
    scenarios = list(by_scenario.keys())
    methods = ['MC', 'QMC', 'Importance']

    x = np.arange(len(scenarios))
    width = 0.25

    fig, ax = plt.subplots(figsize=(12, 6))

    for i, method in enumerate(methods):
        variances = [by_scenario[s].get(method, 0) for s in scenarios]
        ax.bar(x + i * width, variances, width, label=method)

    ax.set_xlabel('Scenario', fontsize=12)
    ax.set_ylabel('Variance', fontsize=12)
    ax.set_title('Variance Reduction Comparison', fontsize=14, fontweight='bold')
    ax.set_xticks(x + width)
    ax.set_xticklabels([s[:20] for s in scenarios], rotation=45, ha='right')
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    plt.savefig(output_file, dpi=150)
    print(f"\nVariance reduction chart saved to: {output_file}")

def plot_cfr_convergence(results, output_file='cfr_convergence.png'):
    """Plot CFR convergence chart"""
    if not HAS_MATPLOTLIB:
        return

    cfr_results = [r for r in results if 'CFR' in r['method']]

    # Group by game
    by_game = defaultdict(dict)
    for r in cfr_results:
        by_game[r['scenario']][r['method']] = r['mean_value']

    games = list(by_game.keys())
    methods = ['MCCFR', 'DCFR']

    x = np.arange(len(games))
    width = 0.35

    fig, ax = plt.subplots(figsize=(10, 6))

    for i, method in enumerate(methods):
        exploitabilities = [by_game[g].get(method, 0) for g in games]
        ax.bar(x + i * width, exploitabilities, width, label=method)

    ax.set_xlabel('Game', fontsize=12)
    ax.set_ylabel('Exploitability', fontsize=12)
    ax.set_title('CFR Convergence Comparison', fontsize=14, fontweight='bold')
    ax.set_xticks(x + width / 2)
    ax.set_xticklabels(games, rotation=45, ha='right')
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    plt.savefig(output_file, dpi=150)
    print(f"CFR convergence chart saved to: {output_file}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_bench_results.py <csv_file>")
        print("\nExample:")
        print("  python3 analyze_bench_results.py bench_reproducible_results.csv")
        sys.exit(1)

    csv_file = sys.argv[1]

    print("="*60)
    print("   Benchmark Results Analysis")
    print("="*60)
    print(f"\nLoading results from: {csv_file}")

    try:
        results = load_results(csv_file)
    except FileNotFoundError:
        print(f"\nError: File not found: {csv_file}")
        sys.exit(1)
    except Exception as e:
        print(f"\nError loading CSV: {e}")
        sys.exit(1)

    print(f"Loaded {len(results)} benchmark results\n")

    # Analyze results
    analyze_variance_reduction(results)
    analyze_cfr_convergence(results)
    analyze_ofc_quality(results)
    generate_summary_table(results)

    # Generate plots
    if HAS_MATPLOTLIB:
        print("\n" + "="*60)
        print("   Generating Charts")
        print("="*60)

        plot_variance_reduction(results)
        plot_cfr_convergence(results)

    print("\n" + "="*60)
    print("   Analysis Complete")
    print("="*60)
    print()

if __name__ == '__main__':
    main()
