#!/usr/bin/env python3
"""
export_variance_markdown.py - Exporte variance_summary.csv en Markdown lisible

Entrée CSV: variance_summary.csv
 Colonnes: policy,scenario,samples,err_mean,err_std

Sortie par défaut: docs/reports/variance_summary.md

Exemple:
  python scripts/export_variance_markdown.py \
    --csv variance_summary.csv --out docs/reports/variance_summary.md
"""

import argparse
import csv
import os
from collections import defaultdict


def fmt_err(mean, std):
    return f"{mean:.6f} ± {std:.6f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', default='variance_summary.csv')
    ap.add_argument('--out', default='docs/reports/variance_summary.md')
    args = ap.parse_args()

    by_scenario = defaultdict(list)  # scen -> [(policy, samples, err_mean, err_std)]
    by_policy = defaultdict(list)    # policy -> [(scenario, samples, err_mean, err_std)]

    with open(args.csv, newline='') as f:
        rd = csv.DictReader(f)
        for r in rd:
            try:
                pol = r['policy'].strip()
                scen = r['scenario'].strip()
                smp = int(r['samples'])
                em = float(r['err_mean'])
                es = float(r['err_std'])
            except Exception:
                continue
            by_scenario[scen].append((pol, smp, em, es))
            by_policy[pol].append((scen, smp, em, es))

    os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)

    lines = []
    lines.append('# Résumé Variance (MC/QMC/IMPORTANCE/STRATIFIED)\n')
    lines.append('Généré automatiquement depuis variance_summary.csv.\n')

    # Par scénario
    lines.append('## Par scénario\n')
    for scen in sorted(by_scenario.keys()):
        rows = sorted(by_scenario[scen], key=lambda x: (x[0], x[1]))
        lines.append(f'### {scen}\n')
        lines.append('| Policy | Samples | err_mean ± err_std |')
        lines.append('|---|---:|---:|')
        for pol, smp, em, es in rows:
            lines.append(f'| {pol} | {smp} | {fmt_err(em, es)} |')
        lines.append('')

    # Par policy
    lines.append('## Par policy\n')
    for pol in sorted(by_policy.keys()):
        rows = sorted(by_policy[pol], key=lambda x: (x[0], x[1]))
        lines.append(f'### {pol}\n')
        lines.append('| Scénario | Samples | err_mean ± err_std |')
        lines.append('|---|---:|---:|')
        for scen, smp, em, es in rows:
            lines.append(f'| {scen} | {smp} | {fmt_err(em, es)} |')
        lines.append('')

    with open(args.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))

    print(f"[info] écrit {args.out}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

