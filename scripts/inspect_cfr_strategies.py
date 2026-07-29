#!/usr/bin/env python3
import csv, sys, argparse

def parse_args():
    ap = argparse.ArgumentParser(description='Inspect CFR strategy dumps (readable CSV).')
    ap.add_argument('inputs', nargs='+', help='Input CSV files (from --strat-readable)')
    ap.add_argument('--filter-bclass', default=None, help='Filter board class name (exact)')
    ap.add_argument('--filter-pclass', default=None, help='Filter private class name (exact)')
    ap.add_argument('--to-call', type=int, choices=[0,1], default=None, help='Filter by to_call (0 or 1)')
    ap.add_argument('--sort', choices=['mass','entropy','delta'], default='mass', help='Sort key (supports delta)')
    ap.add_argument('--order', choices=['desc','asc'], default='desc', help='Sort order')
    ap.add_argument('--top', type=int, default=20, help='Show top N rows')
    ap.add_argument('--filter-turn-feats', default=None, help='Filter by turn features (comma-separated: pair,fdraw,sdraw,open,closed,trips,quads) or numeric bitmask (0..255)')
    ap.add_argument('--filter-priv-feats', default=None, help='Filter by private blocker feats (comma-separated: blk_flush,blk_straight) or numeric bitmask (0..3)')
    ap.add_argument('--delta-min', type=float, default=None, help='Keep rows with |ev_local - ev_root| >= DELTA')
    ap.add_argument('--out', default=None, help='Output CSV path (optional)')
    return ap.parse_args()

def load_rows(paths):
    rows=[]
    for p in paths:
        with open(p,'r') as f:
            r=csv.DictReader(f)
            for row in r:
                # Expect mass/entropy present; if not, try to compute from actions
                mass=row.get('mass','')
                entropy=row.get('entropy','')
                try:
                    mass=float(mass) if mass!='' else None
                except:
                    mass=None
                try:
                    entropy=float(entropy) if entropy!='' else None
                except:
                    entropy=None
                rows.append({
                    'key': row.get('key'),
                    'player': row.get('player'),
                    'board_cls': row.get('board_cls'),
                    'private_cls': row.get('private_cls'),
                    'coarse_bin': row.get('coarse_bin'),
                    'turn_feats': row.get('turn_feats'),
                    'turn_feats_labels': row.get('turn_feats_labels'),
                    'priv_feats': row.get('priv_feats') or row.get('turn_feats'),
                    'priv_feats_labels': row.get('priv_feats_labels'),
                    'hist_hex': row.get('hist_hex'),
                    'to_call': row.get('to_call'),
                    'raises_left': row.get('raises_left'),
                    'n_actions': row.get('n_actions'),
                    'actions': row.get('actions'),
                    'mass': mass,
                    'entropy': entropy,
                    'ev_root': row.get('ev_root'),
                    'ev_local': row.get('ev_local'),
                    'delta': None,
                })
    return rows

def filter_rows(rows, bclass, pclass, to_call, turn_feats, priv_feats, delta_min):
    out=[]
    for r in rows:
        if bclass and (r['board_cls']!=bclass):
            continue
        if pclass and (r['private_cls']!=pclass):
            continue
        if to_call is not None and str(to_call)!=str(r['to_call']):
            continue
        if turn_feats:
            tf_lbl = r.get('turn_feats_labels') or ''
            if turn_feats.isdigit():
                try:
                    need = int(turn_feats)
                    have = int(r.get('turn_feats') or '0')
                    # require all bits in need to be present in have
                    if (have & need) != need:
                        continue
                except Exception:
                    continue
            else:
                ok = True
                for tok in [t.strip() for t in turn_feats.split(',') if t.strip()]:
                    if tok and tok not in tf_lbl.split('|'):
                        ok = False; break
                if not ok:
                    continue
        if priv_feats:
            pfl = r.get('priv_feats_labels') or ''
            if priv_feats.isdigit():
                try:
                    need = int(priv_feats)
                    have = int(r.get('priv_feats') or '0')
                    if (have & need) != need:
                        continue
                except Exception:
                    continue
            else:
                ok = True
                for tok in [t.strip() for t in priv_feats.split(',') if t.strip()]:
                    if tok and tok not in pfl.split('|'):
                        ok = False; break
                if not ok:
                    continue
        # compute delta if fields exist
        try:
            evr = float(r.get('ev_root')) if r.get('ev_root') not in (None, '') else None
            evl = float(r.get('ev_local')) if r.get('ev_local') not in (None, '') else None
            if evr is not None and evl is not None:
                r['delta'] = abs(evl - evr)
                if delta_min is not None and r['delta'] < float(delta_min):
                    continue
        except Exception:
            if delta_min is not None:
                # If we cannot compute delta but a threshold is requested, skip
                continue
        out.append(r)
    return out

def sort_rows(rows, key, order):
    reverse=(order=='desc')
    return sorted(rows, key=lambda r: (r.get(key) if r.get(key) is not None else float('-inf')), reverse=reverse)

def write_rows(rows, out_path, top):
    cols=['key','player','board_cls','private_cls','coarse_bin','hist_hex','to_call','raises_left','n_actions','mass','entropy','actions']
    if out_path:
        with open(out_path,'w',newline='') as f:
            w=csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            for r in rows[:top]:
                w.writerow({c:r.get(c) for c in cols})
        print('Wrote', out_path)
    else:
        w=csv.DictWriter(sys.stdout, fieldnames=cols)
        w.writeheader()
        for r in rows[:top]:
            w.writerow({c:r.get(c) for c in cols})

def main():
    a=parse_args()
    rows=load_rows(a.inputs)
    rows=filter_rows(rows, a.filter_bclass, a.filter_pclass, a.to_call, a.filter_turn_feats, a.filter_priv_feats, a.delta_min)
    rows=sort_rows(rows, a.sort, a.order)
    write_rows(rows, a.out, a.top)

if __name__=='__main__':
    main()
