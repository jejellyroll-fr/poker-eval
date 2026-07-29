#!/usr/bin/env python3
"""
aggregate_variance.py - Agrège variance_raw.csv en variance_summary.csv

Entrée (variance_raw.csv):
  policy,scenario,samples,repeat,estimate_p1,error,time_sec

Sortie (variance_summary.csv):
  policy,scenario,samples,err_mean,err_std,time_mean,time_std

Usage:
  python scripts/aggregate_variance.py \
      --raw variance_raw.csv --out variance_summary.csv
"""

import argparse
import csv
import math
from collections import defaultdict


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', default='variance_raw.csv', help='CSV brut en entrée')
    ap.add_argument('--out', default='variance_summary.csv', help='CSV agrégé en sortie')
    return ap.parse_args()


def main():
    args = parse_args()

    groups = defaultdict(list)  # (policy, scenario, samples) -> [(error, time_sec)]
    with open(args.raw, newline='') as f:
        rd = csv.DictReader(f)
        for r in rd:
            try:
                pol = r['policy'].strip()
                scen = r['scenario'].strip()
                smp = int(r['samples'])
                err = float(r['error'])
                t = float(r.get('time_sec', 'nan'))
            except Exception:
                continue
            if err < 0:
                continue
            groups[(pol, scen, smp)].append((err, t))

    rows = []
    for (pol, scen, smp), arr in groups.items():
        if not arr:
            continue
        n = len(arr)
        errs = [e for e, _t in arr]
        times = [t for _e, t in arr if not (t != t)]  # filter NaN
        mean = sum(errs) / n
        if n > 1:
            var = sum((x - mean) ** 2 for x in errs) / (n - 1)
            std = math.sqrt(var)
        else:
            std = 0.0
        if times:
            tn = len(times)
            tmean = sum(times) / tn
            if tn > 1:
                tvar = sum((x - tmean) ** 2 for x in times) / (tn - 1)
                tstd = math.sqrt(tvar)
            else:
                tstd = 0.0
        else:
            tmean = float('nan')
            tstd = float('nan')
        rows.append((pol, scen, smp, mean, std, tmean, tstd))

    rows.sort(key=lambda x: (x[0], x[1], x[2]))

    with open(args.out, 'w', newline='') as f:
        wr = csv.writer(f)
        wr.writerow(['policy', 'scenario', 'samples', 'err_mean', 'err_std', 'time_mean', 'time_std'])
        for r in rows:
            wr.writerow(r)

    print(f"[info] écrit {args.out} ({len(rows)} lignes)")


if __name__ == '__main__':
    raise SystemExit(main())
