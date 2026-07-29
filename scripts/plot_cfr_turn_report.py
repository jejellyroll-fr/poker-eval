#!/usr/bin/env python3
import csv, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    if len(sys.argv)<3:
        print('Usage: plot_cfr_turn_report.py <input.csv> <out.png>'); sys.exit(1)
    x=[]; t=[]
    with open(sys.argv[1],'r') as f:
        r=csv.DictReader(f)
        for row in r:
            x.append(int(row['deal_idx']))
            t.append(float(row['time_sec']))
    plt.figure(figsize=(7,4))
    plt.plot(x,t,marker='o',linewidth=1,color='#e15759')
    plt.xlabel('Deal index'); plt.ylabel('Time (s)'); plt.title('CFR Turn per-deal time (sampled river)')
    plt.tight_layout(); plt.savefig(sys.argv[2])
    print('Wrote', sys.argv[2])

if __name__=='__main__':
    main()

