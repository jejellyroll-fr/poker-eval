#!/usr/bin/env python3
import argparse, csv, os

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', required=True)
    ap.add_argument('--out', default='docs/reports/holdem_canon7_summary.md')
    args = ap.parse_args()

    rows = []
    with open(args.csv, newline='') as f:
        rd = csv.DictReader(f)
        for r in rd:
            try:
                rows.append({
                    'stage': r['stage'].strip(),
                    'unique_classes': int(r['unique_classes']),
                    'time_sec': float(r['time_sec'])
                })
            except Exception:
                pass
    if not rows:
        print('[warn] empty csv')
        return 0
    order = ['preflop','flop','turn','river']
    rows.sort(key=lambda x: order.index(x['stage']) if x['stage'] in order else 99)

    lines = []
    lines.append('# Hold\'em — Classes d\'équivalence 7‑cartes (poche+board)\n')
    lines.append('| Stage | Classes uniques | Temps (s) |')
    lines.append('|---|---:|---:|')
    for r in rows:
        lines.append(f"| {r['stage']} | {r['unique_classes']} | {r['time_sec']:.3f} |")

    os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f"[info] wrote {args.out}")
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

