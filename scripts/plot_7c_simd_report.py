#!/usr/bin/env python3
import csv
import sys
from collections import defaultdict
import math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load_csv(path):
    rows = []
    with open(path, 'r', newline='') as f:
        r = csv.DictReader(f)
        for row in r:
            rows.append(row)
    return rows

def summarize(rows):
    by_samples = defaultdict(lambda: {'SIMD': [], 'SCALAR': []})
    for row in rows:
        mode = row['mode']
        samples = int(row['samples'])
        eps = float(row['evals_per_sec'])
        by_samples[samples][mode].append(eps)
    summary = []
    for samples, modes in sorted(by_samples.items()):
        simd = sum(modes['SIMD'])/len(modes['SIMD']) if modes['SIMD'] else float('nan')
        scalar = sum(modes['SCALAR'])/len(modes['SCALAR']) if modes['SCALAR'] else float('nan')
        speedup = (simd / scalar) if (simd > 0 and scalar > 0) else float('nan')
        summary.append((samples, simd, scalar, speedup))
    return summary

def plot_speedup(summary, out_png):
    samples = [s for (s,_,_,_) in summary]
    speedups = [sp for (_,_,_,sp) in summary]
    plt.figure(figsize=(6,4))
    plt.bar([str(s) for s in samples], speedups, color='#4e79a7')
    plt.ylabel('Speedup (SIMD / Scalar)')
    plt.xlabel('Samples')
    plt.title('7-card SIMD Speedup')
    for i, v in enumerate(speedups):
        if not math.isnan(v):
            plt.text(i, v + 0.05, f"{v:.1f}x", ha='center', va='bottom', fontsize=9)
    plt.tight_layout()
    plt.savefig(out_png)

def main():
    if len(sys.argv) < 3:
        print("Usage: plot_7c_simd_report.py <input.csv> <output.png>")
        sys.exit(1)
    rows = load_csv(sys.argv[1])
    summary = summarize(rows)
    plot_speedup(summary, sys.argv[2])
    print(f"Wrote {sys.argv[2]}")

if __name__ == '__main__':
    main()

