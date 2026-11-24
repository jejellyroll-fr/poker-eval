"""Draw games through the Python binding (issue #303).

The binding routes the draw games to the core enumerator, which deals each
pocket up to its full size.  ``5draw*`` are the joker variants (53-card deck);
``5draw*_std`` deal from the standard 52-card deck.
"""
import os
import re
import sys
import unittest

try:
    # An installed wheel ships pokereval.py next to the extension.
    import pokereval
except ImportError:
    # A CMake build only has pypokereval on PYTHONPATH; take the wrapper
    # from the source tree.
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "bindings", "python"))
    import pokereval
from pokereval import PokerEval  # noqa: E402

# Ace-king-queen high against ace-king-queen-eight high: the second hand wins
# the high side; the first, 7-high below the eight, wins any low side.
HI_LOSER = ["As", "Ks", "Qd", "7c", "2h"]
HI_WINNER = ["Ah", "Kh", "Qc", "8d", "3s"]
FIVE_CARD_DRAW = ("5draw", "5draw8", "5drawnsq", "5draw_std", "5draw8_std", "5drawnsq_std")


def accepted_games(test, pe):
    """The game names the binding accepts, read from its own error message."""
    with test.assertRaises(RuntimeError) as ctx:
        pe.poker_eval(game="no-such-game", pockets=[["As", "Ks"]], board=[])
    names = re.search(r"\((.*)\)", str(ctx.exception)).group(1)
    return [name.strip() for name in names.split(",")]


class TestFiveCardDraw(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()

    def test_complete_hands_exhaustive(self):
        for game in FIVE_CARD_DRAW:
            with self.subTest(game=game):
                pockets = [HI_LOSER, HI_WINNER]
                result = self.pe.poker_eval(game=game, pockets=pockets, board=[])
                self.assertEqual(result["info"][0], 1)
                self.assertEqual(result["eval"][1]["winhi"], 1)
                self.assertEqual(result["eval"][0]["losehi"], 1)
                winners = self.pe.winners(game=game, pockets=pockets, board=[])
                self.assertEqual(winners["hi"], [1])
                if game.startswith("5drawnsq"):
                    # No qualifier: the 7-high hand takes the low side.
                    self.assertEqual(winners["low"], [0])
                else:
                    self.assertNotIn("low", winners)

    def test_complete_hands_sampled(self):
        for game in FIVE_CARD_DRAW:
            with self.subTest(game=game):
                result = self.pe.poker_eval(game=game, pockets=[HI_LOSER, HI_WINNER],
                                            board=[], iterations=50, seed=1)
                self.assertEqual(result["info"][0], 50)
                self.assertEqual(result["eval"][1]["winhi"], 50)

    def test_eight_or_better_low(self):
        low = ["Ah", "2d", "3c", "5s", "8h"]
        high = ["Ks", "Kd", "9c", "9h", "Qs"]
        for game in ("5draw8", "5draw8_std"):
            with self.subTest(game=game):
                winners = self.pe.winners(game=game, pockets=[low, high], board=[])
                self.assertEqual(winners, {"hi": [1], "low": [0]})

    def test_standard_deck_never_deals_the_joker(self):
        # Nine known cards: 43 cards left in the 52-card deck, 44 with the
        # joker.  One card is dealt, so the outcome count is the deck size.
        pockets = [["2s", "3d", "4c", "5h", "__"], ["7s", "8d", "9c", "Th", "Jh"]]
        std = self.pe.poker_eval(game="5draw_std", pockets=pockets, board=[])
        joker = self.pe.poker_eval(game="5draw", pockets=pockets, board=[])
        self.assertEqual(std["info"][0], 43)
        self.assertEqual(joker["info"][0], 44)
        for game in ("5draw8_std", "5drawnsq_std"):
            with self.subTest(game=game):
                self.assertEqual(self.pe.poker_eval(game=game, pockets=pockets, board=[])["info"][0], 43)

    def test_incomplete_hand_equity(self):
        # 2-3-4-5 draws to a straight against a made jack-high straight.
        pockets = [["2s", "3d", "4c", "5h", "__"], ["7s", "8d", "9c", "Th", "Jh"]]
        result = self.pe.poker_eval(game="5draw_std", pockets=pockets, board=[])
        # A 6 makes a 6-high straight, an ace a 5-high one: both lose to the
        # jack-high straight, so the first hand never wins.
        self.assertEqual(result["eval"][0]["winhi"], 0)
        self.assertEqual(result["eval"][1]["winhi"], 43)

    def test_sampled_incomplete_hand_is_seeded(self):
        pockets = [["As", "Ad", "Kc", "__", "__"], ["Qs", "Qd", "Jc", "Jh", "2s"]]
        first = self.pe.poker_eval(game="5draw_std", pockets=pockets, board=[],
                                   iterations=2000, seed=303)
        self.pe.poker_eval(game="5draw_std", pockets=pockets, board=[], iterations=500)
        second = self.pe.poker_eval(game="5draw_std", pockets=pockets, board=[],
                                    iterations=2000, seed=303)
        self.assertEqual(first, second)
        self.assertEqual(first["info"][0], 2000)


class TestOtherDrawGames(unittest.TestCase):
    """One known showdown per newly exposed draw game."""

    def setUp(self):
        self.pe = PokerEval()

    def showdown(self, game, pockets, board=()):
        return self.pe.winners(game=game, pockets=pockets, board=list(board))

    def test_27_triple_draw(self):
        # 2-7: 7-5-4-3-2 is the nuts; a pair loses to any no-pair hand.
        self.assertEqual(self.showdown("27_triple_draw", [["Kd", "Kc", "2s", "3d", "4c"],
                                                          ["7s", "5d", "4h", "3h", "2c"]]),
                         {"low": [1]})

    def test_a5_triple_draw(self):
        # A-5: the wheel is the nuts, straights do not count.
        self.assertEqual(self.showdown("a5_triple_draw", [["As", "2d", "3c", "4h", "5s"],
                                                          ["2s", "3d", "4c", "6h", "7s"]]),
                         {"low": [0]})

    def test_archie(self):
        # High needs a pair of sixes or better; low is 8-or-better.
        self.assertEqual(self.showdown("archie", [["As", "2d", "3c", "4h", "6s"],
                                                  ["Ks", "Kd", "9c", "9h", "Qs"]]),
                         {"hi": [1], "low": [0]})

    def test_badugi(self):
        self.assertEqual(self.showdown("badugi", [["Ks", "Qd", "Jc", "Th"],
                                                  ["As", "2d", "3c", "4h"]]),
                         {"low": [1]})
        # A suited pair of cards spoils the four-card badugi.
        self.assertEqual(self.showdown("badugi", [["As", "2s", "3c", "4h"],
                                                  ["Ks", "Qd", "Jc", "Th"]]),
                         {"low": [1]})

    def test_badugi_incomplete_hand(self):
        # 45 cards left; only the nine hearts below the king other than the
        # ace, two, three and ten make the better badugi.
        result = self.pe.poker_eval(game="badugi", pockets=[["As", "2d", "3c", "__"],
                                                            ["Ks", "Qd", "Jc", "Th"]],
                                    board=[])
        self.assertEqual(result["info"][0], 45)
        self.assertEqual(result["eval"][0]["winlo"], 9)

    def test_badugi_hilo(self):
        winners = self.showdown("badugi_hilo", [["As", "2d", "3c", "4h"],
                                                ["Ks", "Qd", "Jc", "Th"]])
        self.assertEqual(winners, {"hi": [0], "low": [0]})

    def test_drawmaha(self):
        # Omaha half: quad aces.  Draw half: the pair of aces beats ten-high.
        pockets = [["As", "Ah", "Kd", "Qc", "2h"], ["7s", "8s", "9d", "Tc", "3h"]]
        board = ["Ad", "Ac", "5s", "6d", "Jc"]
        self.assertEqual(self.showdown("drawmaha", pockets, board), {"hi": [0], "low": [0]})
        # Drawmaha 49: 29 points against 37, the lower total wins.
        self.assertEqual(self.showdown("drawmaha49", pockets, board), {"hi": [0], "low": [0]})
        # Zero: A-5 low of the five-card hand, the pair of aces loses.
        self.assertEqual(self.showdown("drawmaha_zero", pockets, board), {"hi": [0], "low": [1]})
        # Low-Dugi: T-9-7-3 badugi beats K-Q-2-A.
        self.assertEqual(self.showdown("drawmaha_dugi", pockets, board), {"hi": [0], "low": [1]})

    def test_drawmaha_incomplete_pocket(self):
        # One card to come in the first pocket: 14 cards known, 38 left.
        pockets = [["As", "Ah", "Kd", "Qc", "__"], ["7s", "8s", "9d", "Tc", "3h"]]
        river = ["Ad", "Ac", "5s", "6d", "Jc"]
        for game in ("drawmaha", "drawmaha49", "drawmaha_zero", "drawmaha_dugi"):
            with self.subTest(game=game):
                result = self.pe.poker_eval(game=game, pockets=pockets, board=river)
                self.assertEqual(result["info"][0], 38)
                # Quad aces take the Omaha half whatever comes.
                self.assertEqual(result["eval"][0]["winhi"], 38)
                # Pocket and board cards are dealt together: 40 cards left
                # after the flop, one to the pocket, then two to the board.
                flop = self.pe.poker_eval(game=game, pockets=pockets, board=river[:3] + ["__", "__"])
                self.assertEqual(flop["info"][0], 40 * 39 * 38 // 2)
                sampled = self.pe.poker_eval(game=game, pockets=pockets, board=river,
                                             iterations=200, seed=303)
                self.assertEqual(sampled["info"][0], 200)
        # Drawmaha 49: A-A-K-Q is 27 points, so any card up to a nine keeps
        # the first hand under the second's 37.  No ace is left: the 26 cards
        # from two to nine not already dealt win the low side.
        result = self.pe.poker_eval(game="drawmaha49", pockets=pockets, board=river)
        self.assertEqual(result["eval"][0]["winlo"], 26)

    def test_triple_draw_never_redeals_pocket_cards(self):
        pockets = [["2s", "3d", "4c", "5h", "__"], ["7s", "8d", "9c", "Th", "Jh"]]
        for game in ("27_triple_draw", "a5_triple_draw", "archie"):
            with self.subTest(game=game):
                result = self.pe.poker_eval(game=game, pockets=pockets, board=[])
                self.assertEqual(result["info"][0], 43)


# Complete pockets and board for each accepted game, so the enumeration has
# exactly one outcome.  A game missing here fails the routing test below.
COMPLETE_DEALS = {
    "holdem": ([["As", "Ks"], ["Qd", "Qc"]], ["2h", "7d", "9c", "Ts", "3h"]),
    "holdem8": ([["As", "2s"], ["Qd", "Qc"]], ["3h", "7d", "9c", "Ts", "4h"]),
    "omaha": ([["As", "Ks", "Qs", "Js"], ["Qd", "Qc", "2c", "3d"]], ["2h", "7d", "9c", "Ts", "3h"]),
    "omaha8": ([["As", "2s", "Qs", "Js"], ["Qd", "Qc", "Kc", "Kd"]], ["3h", "7d", "9c", "Ts", "4h"]),
    "7stud": ([["As", "Ks", "Qs", "Js", "2d", "3d", "4d"],
               ["Qd", "Qc", "2c", "3c", "5h", "6h", "8h"]], []),
    "7stud8": ([["As", "Ks", "Qs", "Js", "2d", "3d", "4d"],
                ["Qd", "Qc", "2c", "3c", "5h", "6h", "8h"]], []),
    "7studnsq": ([["As", "Ks", "Qs", "Js", "2d", "3d", "4d"],
                  ["Qd", "Qc", "2c", "3c", "5h", "6h", "8h"]], []),
    "razz": ([["As", "Ks", "Qs", "Js", "2d", "3d", "4d"],
              ["Qd", "Qc", "2c", "3c", "5h", "6h", "8h"]], []),
    "soko": ([HI_LOSER, HI_WINNER], []),
    "lowball": ([HI_LOSER, HI_WINNER], []),
    "lowball27": ([HI_LOSER, HI_WINNER], []),
    "fusion": ([["As", "Ks", "Qs", "Js"], ["Qd", "Qc", "2c", "3d"]], ["2h", "7d", "9c", "Ts", "3h"]),
    "27_triple_draw": ([HI_LOSER, HI_WINNER], []),
    "a5_triple_draw": ([HI_LOSER, HI_WINNER], []),
    "archie": ([HI_LOSER, HI_WINNER], []),
    "badugi": ([["As", "2d", "3c", "4h"], ["Ks", "Qd", "Jc", "Th"]], []),
    "badugi_hilo": ([["As", "2d", "3c", "4h"], ["Ks", "Qd", "Jc", "Th"]], []),
    "drawmaha": ([["As", "Ah", "Kd", "Qc", "2h"], ["7s", "8s", "9d", "Tc", "3h"]],
                 ["Ad", "Ac", "5s", "6d", "Jc"]),
    "drawmaha49": ([["As", "Ah", "Kd", "Qc", "2h"], ["7s", "8s", "9d", "Tc", "3h"]],
                   ["Ad", "Ac", "5s", "6d", "Jc"]),
    "drawmaha_zero": ([["As", "Ah", "Kd", "Qc", "2h"], ["7s", "8s", "9d", "Tc", "3h"]],
                      ["Ad", "Ac", "5s", "6d", "Jc"]),
    "drawmaha_dugi": ([["As", "Ah", "Kd", "Qc", "2h"], ["7s", "8s", "9d", "Tc", "3h"]],
                      ["Ad", "Ac", "5s", "6d", "Jc"]),
}
for _game in FIVE_CARD_DRAW:
    COMPLETE_DEALS[_game] = ([HI_LOSER, HI_WINNER], [])


class TestEveryNameIsRouted(unittest.TestCase):
    def test_every_accepted_name_enumerates(self):
        pe = PokerEval()
        games = accepted_games(self, pe)
        self.assertIn("5draw", games)
        for game in games:
            with self.subTest(game=game):
                self.assertIn(game, COMPLETE_DEALS, "no deal for %s: add one" % game)
                pockets, board = COMPLETE_DEALS[game]
                # Raises "pyenum returned error code" if the name is listed
                # without being routed to an enumerator.
                result = pe.poker_eval(game=game, pockets=pockets, board=board)
                self.assertEqual(result["info"][0], 1)
                sampled = pe.poker_eval(game=game, pockets=pockets, board=board,
                                        iterations=10, seed=1)
                self.assertEqual(sampled["info"][0], 10)


if __name__ == "__main__":
    unittest.main()
