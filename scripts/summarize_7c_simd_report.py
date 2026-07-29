#!/usr/bin/env python3
import csv, sys, math
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def read_rows(path):
    rows=[]
    with open(path,'r') as f:
        r=csv.DictReader(f)
        for row in r: rows.append(row)
    return rows

def group_by_samples(rows):
    g=defaultdict(lambda:{'SIMD':[], 'SCALAR':[], 'nfs':[],'speedup':[]})
    for row in rows:
        s=int(row['samples'])
        mode=row['mode']
        eps=float(row['evals_per_sec'])
        nfs=float(row.get('nfs_ratio','0') or 0)
        if mode=='SIMD':
            sp=float(row.get('speedup','0') or 0)
            g[s]['speedup'].append(sp)
        g[s][mode].append(eps)
        g[s]['nfs'].append(nfs)
    return g

def mean_std(v):
    if not v: return (float('nan'), float('nan'))
    m=sum(v)/len(v)
    var=sum((x-m)**2 for x in v)/len(v)
    return (m, math.sqrt(var))

def write_summary(rows, out_csv):
    g=group_by_samples(rows)
    with open(out_csv,'w',newline='') as f:
        w=csv.writer(f)
        w.writerow(['samples','set_type','simd_eps_mean','simd_eps_std','scalar_eps_mean','scalar_eps_std','speedup_mean','speedup_std','nfs_mean'])
        # regroup by samples and set_type
        by = defaultdict(lambda: defaultdict(list))
        for row in rows:
            s=int(row['samples']); t=row.get('set_type','')
            by[(s,t)]['mode'].append(row['mode'])
            by[(s,t)]['eps'].append(float(row['evals_per_sec']))
            by[(s,t)]['nfs'].append(float(row.get('nfs_ratio','0') or 0))
            if row['mode']=='SIMD': by[(s,t)]['speed'].append(float(row.get('speedup','0') or 0))
        for (s,t), d in sorted(by.items()):
            # split eps by mode
            simd_eps=[d['eps'][i] for i,m in enumerate(d['mode']) if m=='SIMD']
            sca_eps=[d['eps'][i] for i,m in enumerate(d['mode']) if m=='SCALAR']
            simd_m, simd_sd = mean_std(simd_eps)
            sca_m, sca_sd = mean_std(sca_eps)
            sp_m, sp_sd = mean_std(d.get('speed',[]))
            nfs_m, _ = mean_std(d.get('nfs',[]))
            w.writerow([s, t, f"{simd_m:.0f}", f"{simd_sd:.0f}", f"{sca_m:.0f}", f"{sca_sd:.0f}", f"{sp_m:.2f}", f"{sp_sd:.2f}", f"{nfs_m:.4f}"])

def scatter_speedup_vs_nfs(rows, out_png):
    X=[]; Y=[]
    for row in rows:
        if row['mode']!='SIMD': continue
        nfs=float(row.get('nfs_ratio','0') or 0)
        sp=float(row.get('speedup','0') or 0)
        if sp>0:
            X.append(nfs)
            Y.append(sp)
    plt.figure(figsize=(6,4))
    plt.scatter(X,Y,alpha=0.6,s=12)
    plt.xlabel('NFS ratio (share of 5-card combos)')
    plt.ylabel('Speedup (SIMD/Scalar)')
    plt.title('Speedup vs NFS ratio')
    plt.tight_layout()
    plt.savefig(out_png)

def main():
    if len(sys.argv)<4:
        print('Usage: summarize_7c_simd_report.py <input.csv> <summary.csv> <scatter.png>'); sys.exit(1)
    rows=read_rows(sys.argv[1])
    write_summary(rows, sys.argv[2])
    scatter_speedup_vs_nfs(rows, sys.argv[3])
    print('Wrote', sys.argv[2], 'and', sys.argv[3])

if __name__=='__main__':
    main()
