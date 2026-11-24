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


class TestSokoDescription(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()

    def test_four_card_flush_is_named(self):
        description = self.pe.describe_soko_hand(FOUR_FLUSH)
        self.assertEqual(description["category"], "FourCardFlush")
        # J, T, 9, 3: the off-suit deuce does not take part in the comparison.
        self.assertEqual(description["ranks"], [11, 10, 9, 3])
        self.assertEqual(description["rank_names"], ["J", "T", "9", "3"])

    def test_four_card_straight_ranks_by_high_card(self):
        description = self.pe.describe_soko_hand(["9h", "Ts", "Jd", "Qc", "2h"])
        self.assertEqual(description["category"], "FourCardStraight")
        self.assertEqual(description["ranks"], [12])

    def test_describe_orders_categories_officially(self):
        two_pair = self.pe.describe_soko_hand(TWO_PAIR)
        four_flush = self.pe.describe_soko_hand(FOUR_FLUSH)
        four_straight = self.pe.describe_soko_hand(["9h", "Ts", "Jd", "Qc", "2h"])
        pair = self.pe.describe_soko_hand(ONE_PAIR)

        self.assertGreater(two_pair["category_rank"], four_flush["category_rank"])
        self.assertGreater(four_flush["category_rank"], four_straight["category_rank"])
        self.assertGreater(four_straight["category_rank"], pair["category_rank"])

    def test_best_still_reports_the_standard_category(self):
        # The documented limitation, and the reason describe_soko_hand exists.
        self.assertEqual(self.pe.best("hi", FOUR_FLUSH)[1][0], "NoPair")

    def test_value_orders_like_the_official_categories(self):
        def value(cards):
            return self.pe.describe_soko_hand(cards)["value"]

        two_pair = value(TWO_PAIR)
        four_flush = value(FOUR_FLUSH)
        four_straight = value(["9h", "Ts", "Jd", "Qc", "2h"])
        pair = value(ONE_PAIR)

        self.assertGreater(two_pair, four_flush)
        self.assertGreater(four_flush, four_straight)
        self.assertGreater(four_straight, pair)

    def test_value_ignores_the_off_suit_fifth_card(self):
        hearts = self.pe.describe_soko_hand(FOUR_FLUSH)["value"]
        diamonds = self.pe.describe_soko_hand(
            ["Jd", "Td", "9d", "3d", "5c"]
        )["value"]
        self.assertEqual(hearts, diamonds)

    def test_describe_requires_exactly_five_cards(self):
        with self.assertRaises(ValueError):
            self.pe.describe_soko_hand(["Ah", "Ad"])


class TestSokoStreetEquity(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()

    def test_fifth_street_is_exact(self):
        result = self.pe.soko_street_equity(
            [
                {"up": ["Kh", "Qh", "Jh", "2s"], "down": ["Ah"]},
                {"up": ["9d", "8d", "7d", "6d"], "down": ["Kc"]},
            ]
        )
        self.assertEqual(result["exact"], 1)
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["hi"][0], 1.0)
        self.assertEqual(result["hi"][1], 0.0)

    def test_third_street_with_unknown_hole_card(self):
        result = self.pe.soko_street_equity(
            [
                {"up": ["Kh", "Qh"], "down": ["Ah"]},
                {"up": ["9d", "8d"]},
            ],
            iterations=300,
        )
        self.assertEqual(result["exact"], 0)
        # "samples" is the number of evaluated range matchups.
        self.assertEqual(result["samples"], 1)
        self.assertAlmostEqual(sum(result["hi"]), 1.0, places=6)
        self.assertGreater(result["hi"][0], 0.5)

    def test_duplicate_card_is_rejected(self):
        with self.assertRaises(ValueError):
            self.pe.soko_street_equity(
                [
                    {"up": ["Kh", "Qh"], "down": ["Ah"]},
                    {"up": ["Kh", "Qd"], "down": ["Ad"]},
                ]
            )

    def test_four_way_fifth_street_is_exact(self):
        result = self.pe.soko_street_equity(
            [
                {"up": ["Kh", "Qh", "Jh", "2s"], "down": ["Ah"]},
                {"up": ["9d", "8d", "7d", "6d"], "down": ["Kc"]},
                {"up": ["As", "Ad", "Ks", "Kd"], "down": ["2h"]},
                {"up": ["Qs", "Qd", "Js", "Jd"], "down": ["3h"]},
            ]
        )
        self.assertEqual(result["exact"], 1)
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["hi"][2], 1.0)
        self.assertAlmostEqual(sum(result["hi"]), 1.0, places=9)

    def test_seed_reproduces_sampling(self):
        players = [
            {"up": ["Kh", "Qh"], "down": ["Ah"]},
            {"up": ["9d", "8d"]},
            {"up": ["7c", "6c"]},
        ]
        first = self.pe.soko_street_equity(players, iterations=400, seed=11)
        second = self.pe.soko_street_equity(players, iterations=400, seed=11)
        other = self.pe.soko_street_equity(players, iterations=400, seed=12)

        self.assertEqual(first["hi"], second["hi"])
        self.assertNotEqual(first["hi"], other["hi"])
        self.assertAlmostEqual(sum(first["hi"]), 1.0, places=6)

    def test_dead_cards_are_respected(self):
        result = self.pe.soko_street_equity(
            [
                {"up": ["Kh", "Qh", "Jh"], "down": ["Ah"]},
                {"up": ["9d", "8d", "7d"], "down": ["5c"]},
            ],
            dead=["Th"],
            iterations=200,
        )
        self.assertAlmostEqual(sum(result["hi"]), 1.0, places=6)


if __name__ == "__main__":
    unittest.main()
