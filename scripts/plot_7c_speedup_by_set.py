#!/usr/bin/env python3
import csv, sys, math
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def read_summary(path):
    rows=[]
    with open(path,'r') as f:
        r=csv.DictReader(f)
        for row in r: rows.append(row)
    return rows

def main():
    if len(sys.argv)<3:
        print('Usage: plot_7c_speedup_by_set.py <summary.csv> <out.png>'); sys.exit(1)
    rows=read_summary(sys.argv[1])
    # Expect columns: samples,set_type,speedup_mean
    samples=sorted({int(r['samples']) for r in rows})
    sets=['random','nfs-heavy','sf-heavy']
    data={t:[] for t in sets}
    for s in samples:
        for t in sets:
            m=[float(r['speedup_mean']) for r in rows if int(r['samples'])==s and r['set_type']==t]
            data[t].append(m[0] if m else 0.0)

    x=range(len(samples))
    width=0.25
    plt.figure(figsize=(7,4))
    for i,t in enumerate(sets):
        plt.bar([xx + (i-1)*width for xx in x], data[t], width=width, label=t)
    plt.xticks([xx for xx in x], [str(s) for s in samples])
    plt.ylabel('Speedup (SIMD/Scalar)')
    plt.xlabel('Samples')
    plt.title('7-Card SIMD Speedup by Set Type')
    plt.legend()
    plt.tight_layout()
    plt.savefig(sys.argv[2])

if __name__=='__main__':
    main()

