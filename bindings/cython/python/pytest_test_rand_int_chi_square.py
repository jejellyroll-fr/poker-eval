
import pokereval
from collections import defaultdict
from scipy.stats import chisquare
import pytest

def test_rand_int_chi_square():
    poker_eval = pokereval.PokerEval()
    poker_eval.reset_seed(12345)
    counts = defaultdict(int)
    iterations = 100000
    upper_bound = 10
    for _ in range(iterations):
        r = poker_eval.rand_int(upper_bound)
        counts[r] += 1

    observed = [counts[i] for i in range(upper_bound)]
    expected = [iterations / upper_bound] * upper_bound
    chi2, p = chisquare(observed, expected)
    
    assert p >= 0.05, f"Random integers do not follow uniform distribution: p-value = {p}"

