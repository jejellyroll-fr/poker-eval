#!/usr/bin/env python3
"""
plot_variance_summary.py - Trace err_mean/err_std à partir de variance_summary.csv

Entrée CSV (par défaut: variance_summary.csv):
  policy,scenario,samples,err_mean,err_std

Exemples:
  python scripts/plot_variance_summary.py --csv variance_summary.csv \
      --outdir docs/reports --per_scenario

  python scripts/plot_variance_summary.py --csv variance_summary.csv \
      --outdir docs/reports --scenario flop_QhJd_vs_9c9d
"""

import argparse
import csv
import os
from collections import defaultdict

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', default='variance_summary.csv', help='Chemin du CSV agrégé')
    ap.add_argument('--outdir', default='docs/reports', help='Répertoire de sortie des PNG')
    ap.add_argument('--scenario', help='Filtrer un scénario spécifique')
    ap.add_argument('--per_scenario', action='store_true', help='Tracer un plot par scénario')
    ap.add_argument('--plot_ratio', action='store_true', help='Tracer err_mean/err_std en plus')
    ap.add_argument('--title', default='Variance vs Samples', help='Titre du graphe')
    ap.add_argument('--plot_time', action='store_true', help='Tracer err_mean vs time_mean')
    args = ap.parse_args()

    try:
        import matplotlib.pyplot as plt
    except Exception as e:
        print('[error] matplotlib requis: pip install matplotlib')
        return 2

    data = defaultdict(list)  # key: (scenario, policy) -> list of (samples, err_mean, err_std, time_mean, time_std)
    with open(args.csv, newline='') as f:
        rd = csv.DictReader(f)
        for r in rd:
            try:
                pol = r['policy'].strip()
                scen = r['scenario'].strip()
                smp = int(r['samples'])
                em = float(r['err_mean'])
                es = float(r['err_std'])
                tm = float(r.get('time_mean', 'nan'))
                ts = float(r.get('time_std', 'nan'))
            except Exception:
                continue
            if args.scenario and scen != args.scenario:
                continue
            data[(scen, pol)].append((smp, em, es, tm, ts))

    if not data:
        print('[warn] Pas de données après filtrage')
        return 0

    os.makedirs(args.outdir, exist_ok=True)

    def plot_one(scen):
        by_pol = {pol: sorted(vals, key=lambda x: x[0])
                  for (sc, pol), vals in data.items() if sc == scen}
        if not by_pol:
            return
        plt.figure(figsize=(6.5, 4.5))
        for pol, arr in by_pol.items():
            xs = [x[0] for x in arr]
            ys = [x[1] for x in arr]
            es = [x[2] for x in arr]
            plt.errorbar(xs, ys, yerr=es, label=pol, marker='o', capsize=4)
        plt.xlabel('samples')
        plt.ylabel('err_mean ± err_std')
        plt.title(f"{args.title} — {scen}")
        plt.legend()
        plt.grid(True, alpha=0.3)
        out = os.path.join(args.outdir, f"variance_{scen}.png")
        plt.tight_layout()
        plt.savefig(out, dpi=150)
        print(f"[info] écrit {out}")
        plt.close()

        if args.plot_ratio:
            plt.figure(figsize=(6.5, 4.5))
            for pol, arr in by_pol.items():
                xs = [x[0] for x in arr]
                rs = []
                for _smp, em, es, _tm, _ts in arr:
                    if es > 0:
                        rs.append(em / es)
                    else:
                        rs.append(float('nan'))
                plt.plot(xs, rs, label=pol, marker='s')
            plt.xlabel('samples')
            plt.ylabel('err_mean/err_std')
            plt.title(f"Ratio erreur {scen}")
            plt.legend()
            plt.grid(True, alpha=0.3)
            out2 = os.path.join(args.outdir, f"variance_ratio_{scen}.png")
            plt.tight_layout()
            plt.savefig(out2, dpi=150)
            print(f"[info] écrit {out2}")
            plt.close()

        if args.plot_time:
            plt.figure(figsize=(6.5, 4.5))
            for pol, arr in by_pol.items():
                xs = [x[3] for x in arr]  # time_mean
                ys = [x[1] for x in arr]  # err_mean
                # Optionally we could use xerr for time_std, but keep clean
                plt.plot(xs, ys, marker='o', label=pol)
            plt.xlabel('time_mean (s)')
            plt.ylabel('err_mean')
            plt.title(f"Erreur vs Temps — {scen}")
            plt.legend()
            plt.grid(True, alpha=0.3)
            out3 = os.path.join(args.outdir, f"variance_time_{scen}.png")
            plt.tight_layout()
            plt.savefig(out3, dpi=150)
            print(f"[info] écrit {out3}")
            plt.close()

    if args.per_scenario or args.scenario:
        scenarios = set(sc for (sc, _pol) in data.keys())
        if args.scenario:
            scenarios = {args.scenario}
        for scen in sorted(scenarios):
            plot_one(scen)
    else:
        # Tracer un seul graphe avec tous les scénarios (légendes longues)
        plt.figure(figsize=(7, 5))
        for (scen, pol), arr in data.items():
            arr = sorted(arr, key=lambda x: x[0])
            xs = [x[0] for x in arr]
            ys = [x[1] for x in arr]
            es = [x[2] for x in arr]
            plt.errorbar(xs, ys, yerr=es, label=f"{pol} — {scen}", marker='o', capsize=4)
        plt.xlabel('samples')
        plt.ylabel('err_mean ± err_std')
        plt.title(args.title)
        plt.legend(fontsize='x-small', ncol=1)
        plt.grid(True, alpha=0.3)
        out = os.path.join(args.outdir, f"variance_all.png")
        plt.tight_layout()
        plt.savefig(out, dpi=150)
        print(f"[info] écrit {out}")
        plt.close()

    return 0

if __name__ == '__main__':
    raise SystemExit(main())
