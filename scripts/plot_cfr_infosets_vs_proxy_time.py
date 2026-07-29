#!/usr/bin/env python3
import sys, csv, statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load_rows(paths):
    rows = []
    for p in paths:
        with open(p, 'r') as f:
            r = csv.DictReader(f)
            for row in r:
                try:
                    row2 = {
                        'bucket_mode': int(row.get('bucket_mode', -1)),
                        'infosets': float(row.get('infosets', 'nan')),
                        'proxy': float(row.get('proxy', 'nan')),
                        'time_sec': float(row.get('time_sec', 'nan')),
                    }
                except Exception:
                    continue
                if row2['bucket_mode'] >= 0:
                    rows.append(row2)
    return rows

def aggregate_by_mode(rows):
    agg = {}
    for r in rows:
        m = r['bucket_mode']
        agg.setdefault(m, {'infosets': [], 'proxy': [], 'time_sec': []})
        if r['infosets'] == r['infosets']:
            agg[m]['infosets'].append(r['infosets'])
        if r['proxy'] == r['proxy']:
            agg[m]['proxy'].append(r['proxy'])
        if r['time_sec'] == r['time_sec']:
            agg[m]['time_sec'].append(r['time_sec'])
    out = {}
    for m, vals in agg.items():
        def mean_or_nan(lst):
            return statistics.mean(lst) if lst else float('nan')
        out[m] = {
            'infosets': mean_or_nan(vals['infosets']),
            'proxy': mean_or_nan(vals['proxy']),
            'time_sec': mean_or_nan(vals['time_sec']),
        }
    return out

def plot(agg, out_proxy_png, out_time_png):
    modes = sorted(agg.keys())
    x_infos = [agg[m]['infosets'] for m in modes]
    y_proxy = [agg[m]['proxy'] for m in modes]
    y_time = [agg[m]['time_sec'] for m in modes]

    # Proxy plot
    plt.figure(figsize=(6,4))
    for i, m in enumerate(modes):
        plt.scatter(x_infos[i], y_proxy[i], label=f"mode {m}")
    plt.xlabel('Infosets (avg)')
    plt.ylabel('Proxy (avg)')
    plt.title('Infosets vs Exploitability Proxy')
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_proxy_png)
    print('Wrote', out_proxy_png)

    # Time plot
    plt.figure(figsize=(6,4))
    for i, m in enumerate(modes):
        plt.scatter(x_infos[i], y_time[i], label=f"mode {m}")
    plt.xlabel('Infosets (avg)')
    plt.ylabel('Time per deal (avg s)')
    plt.title('Infosets vs Time')
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_time_png)
    print('Wrote', out_time_png)

def main():
    if len(sys.argv) < 4:
        print('Usage: plot_cfr_infosets_vs_proxy_time.py <input.csv> [<input2.csv> ...] <out_proxy.png> <out_time.png>')
        sys.exit(1)
    *csvs, out_proxy_png, out_time_png = sys.argv[1:]
    rows = load_rows(csvs)
    agg = aggregate_by_mode(rows)
    if not agg:
        print('No data found (need columns: bucket_mode, infosets, proxy, time_sec).')
        sys.exit(2)
    plot(agg, out_proxy_png, out_time_png)

if __name__ == '__main__':
    main()

