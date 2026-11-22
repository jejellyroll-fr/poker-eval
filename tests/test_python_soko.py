"""Sökö through the Python binding (issue #316).

``calculate_range_equity`` expands a player's range strings and then calls the
native ``calculate_equity_for_ranges``.  For Sökö the expansion has to use the
stud grammar (five-card patterns or plain five-card hands), and the equity
must be ranked with the Sökö rules rather than the standard five-card order.
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


FOUR_FLUSH = ["9h", "Th", "Jh", "3h", "2s"]
ONE_PAIR = ["Ah", "Ad", "Ks", "8c", "3c"]
TWO_PAIR = ["Ah", "Ad", "Ks", "Kd", "2h"]


class TestSokoRangeEquity(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()

    def test_two_pair_beats_four_flush(self):
        # Old wrong order put the four-card flush above two pair.
        result = self.pe.calculate_range_equity(
            "soko", [["AhAdKsKd2h"], ["9hThJh3h2s"]]
        )
        self.assertGreater(result["eval"][0]["ev"], result["eval"][1]["ev"])

    def test_four_flush_beats_one_pair(self):
        # Sökö promotes the four-card flush above one pair; the standard
        # five-card evaluator ranks the pair higher, so a silent fallback
        # would report the opposite winner.
        result = self.pe.calculate_range_equity(
            "soko", [["9hThJh3h2s"], ["AhAdKs8c3c"]]
        )
        self.assertGreater(result["eval"][0]["ev"], result["eval"][1]["ev"])
        self.assertEqual(result["eval"][0]["ev"], 1.0)
        self.assertEqual(result["eval"][1]["ev"], 0.0)

    def test_stud_pattern_ranges(self):
        result = self.pe.calculate_range_equity(
            "soko", [["(9hTh)Jh3h2s"], ["(AhAd)Ks8c3c"]]
        )
        self.assertGreater(result["eval"][0]["ev"], result["eval"][1]["ev"])

    def test_equal_four_flushes_tie(self):
        result = self.pe.calculate_range_equity(
            "soko", [["AhJh8h5h2s"], ["AdJd8d5d3c"]]
        )
        self.assertEqual(result["eval"][0]["ev"], 0.5)
        self.assertEqual(result["eval"][1]["ev"], 0.5)

    def test_unparseable_range_fails_clearly(self):
        with self.assertRaises(RuntimeError) as ctx:
            self.pe.calculate_range_equity("soko", [["not-a-hand"], ["AhAdKsKd2h"]])
        message = str(ctx.exception)
        self.assertIn("Sökö", message)
        self.assertIn("not-a-hand", message)

    def test_masks_route_through_soko_rules(self):
        # The native entry point with explicit masks, bypassing expansion.
        import pypokereval as native

        mask = native.convert_card_strings_to_mask_value
        # Player 0 holds the pair, player 1 the four-card flush.
        pockets = [[mask(ONE_PAIR)], [mask(FOUR_FLUSH)]]
        raw = native.calculate_equity_for_ranges(
            "soko", pockets, [], 0, 0, 0, [], 0, None
        )
        self.assertGreater(raw[2][-1], raw[1][-1])
        self.assertEqual(raw[2][-1], 1.0)
        self.assertEqual(raw[1][-1], 0.0)


if __name__ == "__main__":
    unittest.main()
