#!/usr/bin/env python3
"""
Generate random Short Deck Hold'em deals for CFR benchmarking.
Short Deck uses 36 cards (6-A, ranks 4-12 in 0-indexed, cards 16-51).
Format: 2 hole P0, 2 hole P1, 5 board (total 9 cards)
"""

import sys
import random

def generate_shortdeck_dealset(n_deals, seed=42):
    """Generate n_deals random Short Deck deals (36-card deck, ranks 6-A)."""
    random.seed(seed)
    deals = []

    # Short deck: 36 cards starting at rank 4 (index 16)
    # Cards 16-51 represent 6,7,8,9,T,J,Q,K,A in 4 suits

    for _ in range(n_deals):
        deck = list(range(16, 52))  # 36 cards
        random.shuffle(deck)

        # 2 hole P0, 2 hole P1, 5 board
        deal = deck[:9]
        deals.append(','.join(map(str, deal)))

    return deals

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 42

    for deal in generate_shortdeck_dealset(n, seed):
        print(deal)
