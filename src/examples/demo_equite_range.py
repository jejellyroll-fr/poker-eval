#!/usr/bin/env python3
"""
Démonstration des équités de range pour Hold'em et Omaha
"""

import subprocess
import re

def run_pokenum(args):
    """Exécute pokenum et retourne la sortie"""
    cmd = ['./build/pokenum'] + args
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode()
        return output
    except subprocess.CalledProcessError as e:
        return f"Erreur: {e.output.decode()}"

def extract_equities(output):
    """Extrait les équités de la sortie de pokenum"""
    equities = []
    lines = output.strip().split('\n')
    
    for line in lines:
        parts = line.split()
        if len(parts) >= 9 and parts[0] != 'cards' and not line.startswith('Holdem') and not line.startswith('Omaha'):
            try:
                ev = float(parts[-1])
                equities.append(ev)
            except ValueError:
                continue
    
    return equities

def print_section(title):
    """Affiche un titre de section"""
    print(f"\n{'='*60}")
    print(f"{title}")
    print(f"{'='*60}\n")

def test_holdem_ranges():
    """Teste les équités de range au Hold'em"""
    print_section("ÉQUITÉS DE RANGE AU HOLD'EM")
    
    # Test 1: Mains spécifiques
    print("1. MAINS SPÉCIFIQUES")
    print("-" * 40)
    
    # AA vs KK
    print("AA vs KK preflop:")
    output = run_pokenum(['-h', 'As', 'Ah', '-', 'Ks', 'Kh'])
    print(output)
    
    # AK vs QQ
    print("\nAK vs QQ preflop:")
    output = run_pokenum(['-h', 'As', 'Kh', '-', 'Qc', 'Qd'])
    print(output)
    
    # Test 2: Simulation de range
    print("\n2. SIMULATION DE RANGE: Paires hautes (AA-QQ) vs AK")
    print("-" * 40)
    print("Calcul de l'équité moyenne de AA, KK, QQ contre AK:\n")
    
    pairs = [
        ('AA', ['As', 'Ah']),
        ('KK', ['Ks', 'Kh']),
        ('QQ', ['Qc', 'Qd'])
    ]
    
    total_ev_pairs = 0
    total_ev_ak = 0
    
    for name, cards in pairs:
        output = run_pokenum(['-h'] + cards + ['-', 'Ac', 'Kd'])
        equities = extract_equities(output)
        if len(equities) >= 2:
            print(f"{name} vs AK: {name} = {equities[0]:.3f}, AK = {equities[1]:.3f}")
            total_ev_pairs += equities[0]
            total_ev_ak += equities[1]
    
    print(f"\n��quité moyenne des paires (AA-QQ): {total_ev_pairs/3:.3f}")
    print(f"Équité moyenne de AK: {total_ev_ak/3:.3f}")
    
    # Test 3: Situation postflop
    print("\n3. ÉQUITÉS POSTFLOP")
    print("-" * 40)
    print("AA vs Flush Draw + Overcards")
    print("Board: 9s 8s 2h\n")
    output = run_pokenum(['-h', 'As', 'Ah', '-', 'Ks', 'Qs', '--', '9s', '8s', '2h'])
    print(output)
    
    # Test 4: Multiway
    print("\n4. POT MULTIWAY")
    print("-" * 40)
    print("AA vs KK vs AK:\n")
    output = run_pokenum(['-h', 'As', 'Ah', '-', 'Ks', 'Kh', '-', 'Ac', 'Kd'])
    print(output)

def test_omaha_ranges():
    """Teste les équités de range à l'Omaha"""
    print_section("ÉQUITÉS DE RANGE À L'OMAHA")
    
    # Test 1: Mains premium
    print("1. MAINS PREMIUM OMAHA")
    print("-" * 40)
    
    # AAxx double suited vs KKxx rainbow
    print("AAxx double suited vs KKxx rainbow:")
    output = run_pokenum(['-o', 'As', 'Ah', 'Ks', 'Kh', '-', 'Kc', 'Kd', 'Qc', 'Jd'])
    print(output)
    
    # Test 2: Simulation de range Omaha
    print("\n2. SIMULATION DE RANGE OMAHA: AAxx variations")
    print("-" * 40)
    print("Différentes mains AAxx contre KKxx:\n")
    
    aa_hands = [
        ('AA double suited', ['As', 'Ah', 'Ks', 'Kh']),
        ('AA rainbow', ['As', 'Ac', 'Kd', 'Qh']),
        ('AA avec connecteurs', ['As', 'Ah', 'Tc', '9c'])
    ]
    
    kk_hand = ['Kc', 'Kd', 'Qc', 'Jd']
    
    total_ev_aa = 0
    
    for name, cards in aa_hands:
        output = run_pokenum(['-o'] + cards + ['-'] + kk_hand)
        equities = extract_equities(output)
        if len(equities) >= 2:
            print(f"{name}: AA = {equities[0]:.3f}, KK = {equities[1]:.3f}")
            total_ev_aa += equities[0]
    
    print(f"\nÉquité moyenne des mains AAxx: {total_ev_aa/3:.3f}")
    
    # Test 3: Draws vs made hands
    print("\n3. DRAWS VS MADE HANDS")
    print("-" * 40)
    print("Wrap draw vs Set")
    print("Board: 9s 8h 7c\n")
    output = run_pokenum(['-o', 'Jh', 'Tc', '6d', '5s', '-', '9c', '9d', 'As', 'Kh', '--', '9s', '8h', '7c'])
    print(output)
    
    # Test 4: PLO5
    print("\n4. PLO5 (5-CARD OMAHA)")
    print("-" * 40)
    print("AAKKx vs QQJJx (Monte Carlo 50k):\n")
    output = run_pokenum(['-o5', '-mc', '50000', 'As', 'Ah', 'Ks', 'Kh', 'Tc', '-', 'Qc', 'Qd', 'Jc', 'Jd', '9s'])
    print(output)

def main():
    print("\n" + "="*60)
    print("DÉMONSTRATION DES ÉQUITÉS DE RANGE")
    print("POKER-EVAL")
    print("="*60)
    
    # Tester Hold'em
    test_holdem_ranges()
    
    # Tester Omaha
    test_omaha_ranges()
    
    # Résumé
    print_section("RÉSUMÉ")
    print("Cette démonstration montre comment calculer:")
    print("• Les équités entre mains spécifiques")
    print("• Les équités moyennes pour simuler des ranges")
    print("• Les équités preflop et postflop")
    print("• Les pots multiway")
    print("• Hold'em et Omaha (4 et 5 cartes)")
    print("\nPour de vraies ranges complexes (ex: 'top 20%'), il faudrait:")
    print("• Énumérer toutes les combinaisons de la range")
    print("• Calculer l'équité contre chaque combinaison adverse")
    print("• Faire la moyenne pondérée des résultats")

if __name__ == "__main__":
    main()