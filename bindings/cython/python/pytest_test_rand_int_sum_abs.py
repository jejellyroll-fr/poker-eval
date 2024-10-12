
import pokereval
from collections import defaultdict
import numpy as np
import pytest

def test_rand_int_sum_abs():
    """
    Effectue un Test de la Somme des Écarts Absolus pour vérifier l'uniformité de rand_int().
    """
    poker_eval = pokereval.PokerEval()
    poker_eval.reset_seed(12345)
    iterations = 100000
    upper_bound = 10
    counts = defaultdict(int)

    for _ in range(iterations):
        r = poker_eval.rand_int(upper_bound)
        counts[r] += 1

    observed = np.array([counts[i] for i in range(upper_bound)])
    expected = np.array([iterations / upper_bound] * upper_bound)

    abs_diffs = np.abs(observed - expected)
    sum_abs_diff = np.sum(abs_diffs)

    tolerance = 0.01 * iterations  # 1% tolerance
    
    assert sum_abs_diff <= tolerance, f"La distribution n'est pas uniforme: somme des écarts absolus = {sum_abs_diff}"

