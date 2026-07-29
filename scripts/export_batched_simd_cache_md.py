#!/usr/bin/env python3
import argparse, csv, os

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', required=True)
    ap.add_argument('--out', default='docs/reports/batched_simd_cache_summary.md')
    args = ap.parse_args()

    rows = []
    with open(args.csv, newline='') as f:
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
    if not rows:
        print('[warn] empty csv')
        return 0
    players = sorted({r['players'] for r in rows})

    def avg(p, simd, cache):
        m = [r['time_ms'] for r in rows if r['players']==p and r['simd']==simd and r['cache']==cache]
        return (sum(m)/len(m)) if m else float('nan')

    lines = []
    lines.append('# Batched Hold\'em — SIMD vs Scalar, Cache ON/OFF\n')
    for p in players:
        t00 = avg(p,'OFF','OFF'); t01 = avg(p,'OFF','ON'); t10 = avg(p,'ON','OFF'); t11 = avg(p,'ON','ON')
        lines.append(f'## Players N={p}\n')
        lines.append('| Config | Time (ms) | Speedup vs OFF/OFF |')
        lines.append('|---|---:|---:|')
        base = t00
        def sp(t):
            return (base/t) if (t and base and t>0) else float('nan')
        lines.append(f'| SIMD OFF, Cache OFF | {t00:.3f} | 1.00x |')
        lines.append(f'| SIMD OFF, Cache ON  | {t01:.3f} | {sp(t01):.2f}x |')
        lines.append(f'| SIMD ON,  Cache OFF | {t10:.3f} | {sp(t10):.2f}x |')
        lines.append(f'| SIMD ON,  Cache ON  | {t11:.3f} | {sp(t11):.2f}x |')
        lines.append('')

    os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f'[info] wrote {args.out}')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

