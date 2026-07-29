#!/usr/bin/env python3
"""
Generate heatmaps from STRATIFIED weights grid-search results.
"""

import sys
import csv
import numpy as np
import matplotlib
matplotlib.use('Agg')  # Headless backend
import matplotlib.pyplot as plt
from collections import defaultdict

def load_grid_results(csv_path):
    """Load grid search results from CSV."""
    results = []
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            results.append({
                'w0': float(row['w0']),
                'w1': float(row['w1']),
                'w2': float(row['w2']),
                'variance_ratio': float(row['variance_ratio']),
                'strat_var': float(row['strat_var']),
                'mc_var': float(row['mc_var'])
            })
    return results

def plot_heatmap_2d(results, out_path, fix_param='w0', title_prefix=''):
    """
    Generate 2D heatmap fixing one parameter.
    fix_param: which parameter to fix ('w0', 'w1', or 'w2')
    """
    # Group by fixed parameter value
    grouped = defaultdict(list)
    for r in results:
        key = r[fix_param]
        grouped[key].append(r)

    n_groups = len(grouped)
    if n_groups == 0:
        print(f"No data for heatmap with fixed {fix_param}", file=sys.stderr)
        return

    # Create subplots for each fixed value
    fig, axes = plt.subplots(1, n_groups, figsize=(6*n_groups, 5))
    if n_groups == 1:
        axes = [axes]

    param_names = {'w0': 'w_distinct', 'w1': 'w_onepair', 'w2': 'w_trips'}
    fixed_name = param_names[fix_param]

    for idx, (fixed_val, group) in enumerate(sorted(grouped.items())):
        # Determine the two varying parameters
        if fix_param == 'w0':
            x_param, y_param = 'w1', 'w2'
        elif fix_param == 'w1':
            x_param, y_param = 'w0', 'w2'
        else:  # fix_param == 'w2'
            x_param, y_param = 'w0', 'w1'

        x_name = param_names[x_param]
        y_name = param_names[y_param]

        # Get unique x and y values
        x_vals = sorted(set(r[x_param] for r in group))
        y_vals = sorted(set(r[y_param] for r in group))

        # Build 2D grid
        grid = np.full((len(y_vals), len(x_vals)), np.nan)
        for r in group:
            xi = x_vals.index(r[x_param])
            yi = y_vals.index(r[y_param])
            grid[yi, xi] = r['variance_ratio']

        # Plot heatmap
        ax = axes[idx]
        im = ax.imshow(grid, aspect='auto', origin='lower', cmap='RdYlGn_r', vmin=0, vmax=1.5)
        ax.set_xticks(range(len(x_vals)))
        ax.set_yticks(range(len(y_vals)))
        ax.set_xticklabels([f'{v:.2f}' for v in x_vals])
        ax.set_yticklabels([f'{v:.2f}' for v in y_vals])
        ax.set_xlabel(x_name)
        ax.set_ylabel(y_name)
        ax.set_title(f'{fixed_name}={fixed_val:.2f}')

        # Add colorbar
        cbar = plt.colorbar(im, ax=ax)
        cbar.set_label('Variance Ratio (STRAT/MC)')

        # Annotate cells with values
        for yi in range(len(y_vals)):
            for xi in range(len(x_vals)):
                if not np.isnan(grid[yi, xi]):
                    text = ax.text(xi, yi, f'{grid[yi, xi]:.3f}',
                                 ha="center", va="center", color="black", fontsize=8)

    fig.suptitle(f'{title_prefix}Variance Ratio Heatmap', fontsize=14)
    plt.tight_layout()
    plt.savefig(out_path, dpi=150, bbox_inches='tight')
    print(f"Saved heatmap: {out_path}", file=sys.stderr)
    plt.close()

def plot_best_weights_summary(results, out_path, title_prefix=''):
    """Plot bar chart of variance ratio for all weight combinations, highlighting best."""
    fig, ax = plt.subplots(figsize=(12, 6))

    # Sort by variance ratio
    sorted_results = sorted(results, key=lambda r: r['variance_ratio'])

    labels = [f"({r['w0']:.1f},{r['w1']:.1f},{r['w2']:.1f})" for r in sorted_results]
    ratios = [r['variance_ratio'] for r in sorted_results]

    # Color best result differently
    colors = ['green' if i == 0 else 'steelblue' for i in range(len(ratios))]

    x = np.arange(len(labels))
    ax.bar(x, ratios, color=colors, alpha=0.7)
    ax.set_xlabel('Weight Combination (w0, w1, w2)')
    ax.set_ylabel('Variance Ratio (STRAT/MC)')
    ax.set_title(f'{title_prefix}Variance Ratio by Weight Combination')
    ax.axhline(1.0, color='red', linestyle='--', alpha=0.5, label='MC baseline')
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=45, ha='right', fontsize=8)
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    plt.savefig(out_path, dpi=150, bbox_inches='tight')
    print(f"Saved summary plot: {out_path}", file=sys.stderr)
    plt.close()

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Generate heatmaps from STRATIFIED weights grid-search')
    parser.add_argument('input_csv', help='Input CSV from grid_search_strat_weights.py')
    parser.add_argument('--out-heatmap-w0', default='strat_heatmap_fix_w0.png', help='Output heatmap (fix w0)')
    parser.add_argument('--out-heatmap-w1', default='strat_heatmap_fix_w1.png', help='Output heatmap (fix w1)')
    parser.add_argument('--out-heatmap-w2', default='strat_heatmap_fix_w2.png', help='Output heatmap (fix w2)')
    parser.add_argument('--out-summary', default='strat_weights_summary.png', help='Output summary bar chart')
    parser.add_argument('--title-prefix', default='', help='Title prefix for plots')

    args = parser.parse_args()

    print(f"Loading results from {args.input_csv}...", file=sys.stderr)
    results = load_grid_results(args.input_csv)
    print(f"Loaded {len(results)} results", file=sys.stderr)

    if not results:
        print("No results to plot!", file=sys.stderr)
        return 1

    # Find best weights
    best = min(results, key=lambda r: r['variance_ratio'])
    print(f"\n✓ Best weights: w=({best['w0']:.2f},{best['w1']:.2f},{best['w2']:.2f})", file=sys.stderr)
    print(f"  Variance ratio: {best['variance_ratio']:.4f}", file=sys.stderr)
    print(f"  Speedup: {1.0/best['variance_ratio']:.2f}× variance reduction vs MC\n", file=sys.stderr)

    # Generate heatmaps
    title = f"{args.title_prefix} " if args.title_prefix else ""

    # Try each fixed parameter if data allows
    for fix_param, out_path in [('w0', args.out_heatmap_w0), ('w1', args.out_heatmap_w1), ('w2', args.out_heatmap_w2)]:
        unique_vals = set(r[fix_param] for r in results)
        if len(unique_vals) > 0:
            plot_heatmap_2d(results, out_path, fix_param, title)

    # Summary plot
    plot_best_weights_summary(results, args.out_summary, title)

    print(f"\n✓ Heatmap generation complete", file=sys.stderr)
    return 0

if __name__ == '__main__':
    sys.exit(main())
