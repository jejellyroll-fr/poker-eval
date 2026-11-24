"""Monte-Carlo intervals for range equity in the Python binding.

``calculate_range_equity`` reports the batch-means standard error of each
player's equity, and the 95% confidence half-width derived from it.  Nothing
is sampled when every matchup is enumerated, so the interval is empty then; a
low pot is a second payoff the high-side histogram cannot separate, so hi/lo
games report no interval either.
"""
import os
import sys
import unittest

try:
    # An installed wheel ships pokereval.py next to the extension.
    import pokereval  # noqa: F401
except ImportError:
    # A CMake build only has pypokereval on PYTHONPATH; take the wrapper
    # from the source tree.
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "bindings", "python"))
from pokereval import PokerEval  # noqa: E402


class TestRangeEquityInterval(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()

    def test_exact_range_has_no_interval(self):
        # A complete board leaves nothing to sample: one enumerated matchup.
        result = self.pe.calculate_range_equity(
            "holdem",
            [["AsKs"], ["QdQh"]],
            board_card_strings=["2c", "7d", "9h", "Jc", "3s"],
        )
        self.assertEqual(result["info"][0], 1)
        for player in result["eval"]:
            self.assertEqual(player["stderr"], 0.0)
            self.assertEqual(player["ci95"], 0.0)

    def test_sampled_range_reports_an_interval(self):
        result = self.pe.calculate_range_equity(
            "holdem",
            [["AsKs"], ["QdQh"]],
            board_card_strings=["2c", "7d", "9h"],
            use_montecarlo=True,
            iterations=6000,
            seed=5,
        )
        self.assertEqual(result["info"][0], 1)
        for player in result["eval"]:
            self.assertGreaterEqual(player["ev"], 0.0)
            self.assertLessEqual(player["ev"], 1.0)
            self.assertGreater(player["stderr"], 0.0)
            # 6000 samples of a coin-flip-ish payoff: a missing 1/W^2 in the
            # aggregation would leave an error near the equity itself.
            self.assertLess(player["stderr"], 0.05)
            self.assertAlmostEqual(
                player["ci95"], 1.96 * player["stderr"], places=12
            )

    def test_hi_lo_range_has_no_interval(self):
        # A low pot is correlated with the high one and the aggregation keeps
        # only per-side histograms, so no interval is defined for hi/lo.
        result = self.pe.calculate_range_equity(
            "holdem8",
            [["AsKs"], ["QdQh"]],
            board_card_strings=["2c", "7d", "9h"],
            use_montecarlo=True,
            iterations=6000,
            seed=7,
        )
        self.assertEqual(result["info"][1], 1)  # has a low pot
        for player in result["eval"]:
            self.assertEqual(player["stderr"], 0.0)
            self.assertEqual(player["ci95"], 0.0)


if __name__ == "__main__":
    unittest.main()
