#!/usr/bin/env python3
"""
plot_postproc_avx2.py - Trace le speedup SIMD OFF vs ON à partir de docs/reports/postproc_avx2.csv

Entrée CSV:
  players,niter,off_ms,on_ms,speedup

Sorties:
  docs/reports/postproc_avx2_speedup.png (barres)
  docs/reports/postproc_avx2_times.png (lignes OFF/ON)
"""

import argparse
import csv
import os


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', default='docs/reports/postproc_avx2.csv')
    ap.add_argument('--outdir', default='docs/reports')
    args = ap.parse_args()

    try:
        import matplotlib.pyplot as plt
    except Exception:
        print('[error] matplotlib requis: pip install matplotlib')
        return 2

    rows = []
    with open(args.csv, newline='') as f:
        rd = csv.DictReader(f)
        for r in rd:
            try:
                rows.append({
                    'players': int(r['players']),
                    'niter': int(r['niter']),
                    'off_ms': float(r['off_ms']),
                    'on_ms': float(r['on_ms']),
                    'speedup': float(r['speedup']),
                })
            except Exception:
                continue

    if not rows:
        print('[warn] CSV vide ou invalide')
        return 0

    rows.sort(key=lambda x: x['players'])
    os.makedirs(args.outdir, exist_ok=True)

    # Barres speedup
    plt.figure(figsize=(5.5, 3.8))
    xs = [str(r['players']) for r in rows]
    ys = [r['speedup'] for r in rows]
    plt.bar(xs, ys, color='#2a9d8f')
    plt.xlabel('Players (N)')
    plt.ylabel('Speedup (OFF/ON)')
    plt.title('Post-traitement AVX2: Speedup vs N')
    plt.grid(True, axis='y', alpha=0.3)
    out1 = os.path.join(args.outdir, 'postproc_avx2_speedup.png')
    plt.tight_layout(); plt.savefig(out1, dpi=150); plt.close()
    print(f'[info] écrit {out1}')

    # Lignes temps OFF/ON
    plt.figure(figsize=(5.5, 3.8))
    xsn = [r['players'] for r in rows]
    off = [r['off_ms'] for r in rows]
    on = [r['on_ms'] for r in rows]
    plt.plot(xsn, off, marker='o', label='SIMD OFF (ms)')
    plt.plot(xsn, on, marker='o', label='SIMD ON (ms)')
    plt.xlabel('Players (N)')
    plt.ylabel('Temps (ms)')
    plt.title('Post-traitement AVX2: Temps vs N')
    plt.legend(); plt.grid(True, alpha=0.3)
    out2 = os.path.join(args.outdir, 'postproc_avx2_times.png')
    plt.tight_layout(); plt.savefig(out2, dpi=150); plt.close()
    print(f'[info] écrit {out2}')

    return 0


if __name__ == '__main__':
    raise SystemExit(main())

