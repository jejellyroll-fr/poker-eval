#!/usr/bin/env python3
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(path):
    rows=[]
    with open(path,'r') as f:
        r=csv.DictReader(f)
        for row in r: rows.append(row)
    return rows

def plot_speedup(rows, out_png):
    sizes=sorted({int(r['samples']) for r in rows})
    modes=['low-friendly','random']
    data={m:[] for m in modes}
    for s in sizes:
        for m in modes:
            vals=[float(r['speedup']) for r in rows if int(r['samples'])==s and r['mode']==m]
            data[m].append(vals[0] if vals else 0.0)
    x=range(len(sizes)); width=0.35
    plt.figure(figsize=(6,4))
    for i,m in enumerate(modes):
        plt.bar([xx+(i-0.5)*width for xx in x], data[m], width=width, label=m)
    plt.xticks([xx for xx in x],[str(s) for s in sizes])
    plt.ylabel('Speedup (single-pass / naïf)')
    plt.xlabel('Samples')
    plt.title('7c Hi/Lo Single-pass Speedup')
    plt.legend()
    plt.tight_layout(); plt.savefig(out_png)

def main():
    if len(sys.argv)<3:
        print('Usage: plot_7c_hilo_report.py <input.csv> <out.png>'); sys.exit(1)
    rows=load(sys.argv[1])
    plot_speedup(rows, sys.argv[2])
    print('Wrote', sys.argv[2])

if __name__=='__main__':
    main()

