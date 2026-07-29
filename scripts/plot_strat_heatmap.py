#!/usr/bin/env python3
"""
plot_strat_heatmap.py - Plot heatmap of metric vs. stratified weights

Input CSV is the aggregated output from scripts/tune_strat.sh, with header:
  policy,scenario,samples,batch,w_distinct,w_onepair,w_trips,
  eq1_mean,eq2_mean,tie_mean,time_mean,count

Examples:
  python scripts/plot_strat_heatmap.py \
      --agg strat_grid_agg.csv --policy STRATIFIED --scenario flop \
      --w0 1.0 --metric time_mean --out heatmap_flop.png

  python scripts/plot_strat_heatmap.py \
      --agg strat_grid_agg.csv --policy STRATIFIED --scenario turn \
      --w0 1.0 --metric eq1_mean --show
"""

import argparse
import csv
import sys
from collections import defaultdict

def main():
    ap = argparse.ArgumentParser(description="Plot heatmap for stratified weight tuning")
    ap.add_argument("--agg", required=True, help="Path to strat_grid_agg.csv")
    ap.add_argument("--policy", default="STRATIFIED", help="Policy filter (MC,QMC,IMPORTANCE,STRATIFIED)")
    ap.add_argument("--scenario", default="flop", help="Scenario filter (preflop,flop,turn,river)")
    ap.add_argument("--w0", type=float, required=True, help="Fixed w_distinct value to slice heatmap")
    ap.add_argument("--metric", default="time_mean", choices=["time_mean","eq1_mean","eq2_mean","tie_mean"],
                    help="Metric to plot on heatmap")
    ap.add_argument("--out", help="Output image path (PNG). If not set, uses --show")
    ap.add_argument("--show", action="store_true", help="Show plot window instead of saving")
    ap.add_argument("--title", default="", help="Custom title for the plot")
    ap.add_argument("--tolerance", type=float, default=1e-6, help="Tolerance for matching w_distinct")
    args = ap.parse_args()

    try:
        import matplotlib.pyplot as plt
        import numpy as np
    except Exception as e:
        print("[error] matplotlib and numpy are required to plot: pip install matplotlib numpy", file=sys.stderr)
        return 2

    rows = []
    with open(args.agg, newline="") as f:
        rd = csv.DictReader(f)
        for r in rd:
            try:
                pol = r["policy"].strip()
                scen = r["scenario"].strip()
                w0 = float(r["w_distinct"]) if r["w_distinct"] != "" else None
                w1 = float(r["w_onepair"]) if r["w_onepair"] != "" else None
                w2 = float(r["w_trips"]) if r["w_trips"] != "" else None
                metric = float(r[args.metric])
            except Exception:
                continue
            if pol != args.policy or scen != args.scenario:
                continue
            if w0 is None or w1 is None or w2 is None:
                continue
            if abs(w0 - args.w0) > args.tolerance:
                continue
            rows.append((w1, w2, metric))

    if not rows:
        print("[error] no data after filtering. Check --policy/--scenario/--w0 match.", file=sys.stderr)
        return 1

    # Build grid
    w1_vals = sorted(sorted({r[0] for r in rows}))
    w2_vals = sorted(sorted({r[1] for r in rows}))
    grid = np.full((len(w1_vals), len(w2_vals)), np.nan, dtype=float)
    idx_w1 = {v:i for i,v in enumerate(w1_vals)}
    idx_w2 = {v:i for i,v in enumerate(w2_vals)}
    for w1, w2, m in rows:
        i = idx_w1[w1]; j = idx_w2[w2]
        grid[i,j] = m

    fig, ax = plt.subplots(figsize=(6, 5))
    im = ax.imshow(grid, origin='lower', aspect='auto', cmap='viridis')
    ax.set_xticks(range(len(w2_vals)))
    ax.set_yticks(range(len(w1_vals)))
    ax.set_xticklabels([f"{v:g}" for v in w2_vals])
    ax.set_yticklabels([f"{v:g}" for v in w1_vals])
    ax.set_xlabel("w_trips")
    ax.set_ylabel("w_onepair")
    title = args.title or f"{args.metric} | {args.policy} | {args.scenario} | w_distinct={args.w0:g}"
    ax.set_title(title)
    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label(args.metric)
    fig.tight_layout()

    if args.out:
        fig.savefig(args.out, dpi=150)
        print(f"[info] wrote {args.out}")
    elif args.show:
        plt.show()
    else:
        # default to save PNG next to agg file
        out = (args.agg.rsplit('.',1)[0] + f"_{args.scenario}_{args.metric}_w0_{args.w0:g}.png")
        fig.savefig(out, dpi=150)
        print(f"[info] wrote {out}")

    return 0

if __name__ == "__main__":
    raise SystemExit(main())

