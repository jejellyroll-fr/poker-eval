#!/usr/bin/env python3
"""
export_postproc_markdown.py - Exporte CSV postproc AVX2 en résumé Markdown

Entrée CSV:
  players,niter,off_ms,on_ms,speedup

Sortie par défaut:
  docs/reports/postproc_avx2_summary.md
"""

import argparse
import csv
import os


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', default='docs/reports/postproc_avx2.csv')
    ap.add_argument('--out', default='docs/reports/postproc_avx2_summary.md')
    args = ap.parse_args()

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
        print('[warn] rien à exporter')
        return 0

    rows.sort(key=lambda x: x['players'])
    avg_speedup = sum(r['speedup'] for r in rows) / len(rows)

    os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
    out = []
    out.append('# Post‑traitement AVX2 — Résumé\n')
    out.append(f'Speedup moyen: {avg_speedup:.2f}x\n')
    out.append('## Détail par N joueurs\n')
    out.append('| Players | Niter | SIMD OFF (ms) | SIMD ON (ms) | Speedup |')
    out.append('|---:|---:|---:|---:|---:|')
    for r in rows:
        out.append(f"| {r['players']} | {r['niter']} | {r['off_ms']:.2f} | {r['on_ms']:.2f} | {r['speedup']:.2f} |")
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))
    print(f'[info] écrit {args.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

