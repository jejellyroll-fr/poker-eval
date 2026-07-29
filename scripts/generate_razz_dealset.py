#!/usr/bin/env python3
"""
Generate random Razz deals for CFR benchmarking.
Format: 7 cards P0, 7 cards P1 (total 14 cards)
"""

import sys
import random

def generate_razz_dealset(n_deals, seed=42):
    """Generate n_deals random Razz deals (same as Stud, 7 cards each)."""
    random.seed(seed)
    deals = []

    for _ in range(n_deals):
        deck = list(range(52))
        random.shuffle(deck)

        # 7 cards P0, 7 cards P1
        deal = deck[:14]
        deals.append(','.join(map(str, deal)))

    return deals

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 42

    for deal in generate_razz_dealset(n, seed):
        print(deal)
