#!/usr/bin/env python3
import argparse, csv, os
import math

def load(csv_path):
    rows = []
    with open(csv_path, newline='') as f:
        rd = csv.DictReader(f)
        for r in rd:
            try:
                rows.append({
                    'players': int(r['players']),
                    'niter': int(r['niter']),
                    'simd': r['simd'].strip(),
                    'cache': r['cache'].strip(),
                    'time_ms': float(r['time_ms']),
                })
            except Exception:
                pass
    return rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', required=True)
    ap.add_argument('--outdir', default='docs/reports')
    args = ap.parse_args()

    try:
        import matplotlib.pyplot as plt
    except Exception:
        print('[warn] matplotlib not available')
        return 0

    rows = load(args.csv)
    if not rows:
        print('[warn] empty csv')
        return 0
    os.makedirs(args.outdir, exist_ok=True)

    # Group by players
    players_set = sorted({r['players'] for r in rows})
    configs = [('OFF','OFF'),('OFF','ON'),('ON','OFF'),('ON','ON')]

    # Bar chart times per config
    for p in players_set:
        subset = [r for r in rows if r['players']==p]
        # Map config -> time
        times = {}
        for (s,c) in configs:
            m = [r['time_ms'] for r in subset if r['simd']==s and r['cache']==c]
            times[(s,c)] = sum(m)/len(m) if m else float('nan')
        labels = [f"SIMD {s}\nCACHE {c}" for (s,c) in configs]
        ys = [times[(s,c)] for (s,c) in configs]
        plt.figure(figsize=(6,4))
        plt.bar(range(len(labels)), ys, color=['#d62728','#ff7f0e','#1f77b4','#2ca02c'])
        plt.xticks(range(len(labels)), labels)
        plt.ylabel('time (ms)')
        plt.title(f'Batched Hold\'em — N={p}, niter={subset[0]["niter"]}')
        plt.grid(True, axis='y', alpha=0.3)
        out = os.path.join(args.outdir, f'batched_simd_cache_times_N{p}.png')
        plt.tight_layout(); plt.savefig(out, dpi=150); plt.close()
        print(f'[info] wrote {out}')

    # Speedup chart vs baseline (SIMD OFF/CACHE OFF)
    plt.figure(figsize=(6,4))
    x = []
    y_simd = []
    y_cache = []
    y_both = []
    for p in players_set:
        subset = [r for r in rows if r['players']==p]
        def avg(simd,cache):
            m = [r['time_ms'] for r in subset if r['simd']==simd and r['cache']==cache]
            return sum(m)/len(m) if m else float('nan')
        t00 = avg('OFF','OFF')
        t10 = avg('ON','OFF')
        t01 = avg('OFF','ON')
        t11 = avg('ON','ON')
        if t00 and t00>0:
            x.append(str(p))
            y_simd.append(t00/t10 if t10 and t10>0 else float('nan'))
            y_cache.append(t00/t01 if t01 and t01>0 else float('nan'))
            y_both.append(t00/t11 if t11 and t11>0 else float('nan'))
    width=0.25
    idx=range(len(x))
    import numpy as np
    idx=np.arange(len(x))
    plt.bar(idx-width, y_simd, width, label='SIMD only', color='#1f77b4')
    plt.bar(idx, y_cache, width, label='Cache only', color='#ff7f0e')
    plt.bar(idx+width, y_both, width, label='SIMD+Cache', color='#2ca02c')
    plt.xticks(idx, x)
    plt.ylabel('speedup vs baseline')
    plt.xlabel('Players (N)')
    plt.title('Batched Hold\'em — Speedup vs OFF/OFF')
    plt.legend()
    plt.grid(True, axis='y', alpha=0.3)
    out2 = os.path.join(args.outdir, 'batched_simd_cache_speedup.png')
    plt.tight_layout(); plt.savefig(out2, dpi=150); plt.close()
    print(f'[info] wrote {out2}')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

