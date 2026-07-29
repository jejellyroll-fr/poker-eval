#!/usr/bin/env python3
import sys, random

def unique_deal(seed):
    random.seed(seed)
    cards=list(range(52))
    random.shuffle(cards)
    c=cards[:13]  # 4+4+5
    return c

def main():
    if len(sys.argv)<3:
        print("Usage: generate_omaha8_dealset.py <count> <out.txt> [seed]", file=sys.stderr)
        sys.exit(1)
    n=int(sys.argv[1]); out=sys.argv[2]; seed=int(sys.argv[3]) if len(sys.argv)>3 else 12345
    seen=set()
    with open(out,'w') as f:
        i=0; k=0
        while i<n and k<n*100:
            deal=unique_deal(seed+k)
            key=tuple(deal)
            if key in seen: k+=1; continue
            seen.add(key)
            f.write("%s\n" % (" ".join(str(x) for x in deal)))
            i+=1; k+=1
    print("Wrote", out)

if __name__=='__main__':
    main()

