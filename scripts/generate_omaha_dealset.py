#!/usr/bin/env python3
"""
Generate random Omaha (high-only) deals for CFR benchmarking.
Format: 4 hole cards P0, 4 hole cards P1, 5 board cards (total 13 cards)
"""

import sys
import random

def generate_omaha_dealset(n_deals, seed=42):
    """Generate n_deals random Omaha deals (52-card deck)."""
    random.seed(seed)
    deals = []

    for _ in range(n_deals):
        deck = list(range(52))
        random.shuffle(deck)

        # 4 hole P0, 4 hole P1, 5 board
        deal = deck[:13]
        deals.append(','.join(map(str, deal)))

    return deals

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 42

    for deal in generate_omaha_dealset(n, seed):
        print(deal)
