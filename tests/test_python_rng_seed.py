"""Seeding the Monte Carlo sampler from Python (issue #301).

Monte Carlo draws through the calling thread's PCG stream, not libc rand(),
so the binding exposes the seed itself: a per-call ``seed`` keyword and a
module-level base seed.
"""
import os
import sys
import threading
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

HOLDEM_POCKETS = [["6d", "3h"], ["7s", "3s"]]
FUSION_POCKETS = [["6d", "3h"], ["7s", "3s"]]
UNKNOWN_BOARD = ["__"] * 5
ITERATIONS = 5000


class TestPokerEvalSeed(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()

    def sample(self, game="holdem", pockets=HOLDEM_POCKETS, seed=None, iterations=ITERATIONS):
        kwargs = dict(game=game, pockets=pockets, board=UNKNOWN_BOARD, iterations=iterations)
        if seed is not None:
            kwargs["seed"] = seed
        return self.pe.poker_eval(**kwargs)

    def test_same_seed_same_result_after_unrelated_calls(self):
        for game, pockets in (("holdem", HOLDEM_POCKETS), ("fusion", FUSION_POCKETS)):
            with self.subTest(game=game):
                first = self.sample(game, pockets, seed=2)
                # Unrelated sampled calls move the thread's stream along.
                self.sample(game, pockets)
                self.sample("holdem", [["As", "Ks"], ["Qd", "Qc"]], seed=99)
                self.sample(game, pockets, seed=7)
                second = self.sample(game, pockets, seed=2)
                self.assertEqual(first, second)

    def test_different_seeds_give_different_samples(self):
        results = [self.sample(seed=s)["eval"] for s in (1, 2, 3, 4)]
        distinct = {repr(r) for r in results}
        self.assertGreater(len(distinct), 1)

    def test_unseeded_calls_keep_advancing_the_stream(self):
        self.sample(seed=11)
        a = self.sample()
        b = self.sample()
        self.assertNotEqual(a["eval"], b["eval"])

    def test_large_and_negative_seeds(self):
        for seed in (-1, 2 ** 64 - 1, 2 ** 70 + 5, 0):
            with self.subTest(seed=seed):
                self.assertEqual(self.sample(seed=seed), self.sample(seed=seed))
        # Reduced modulo 2**64: -1 and 2**64 - 1 are the same stream.
        self.assertEqual(self.sample(seed=-1), self.sample(seed=2 ** 64 - 1))

    def test_bad_seed_type_raises(self):
        for bad in ("2", 2.0, True, [2]):
            with self.subTest(seed=bad):
                with self.assertRaises(TypeError):
                    self.sample(seed=bad)

    def test_seed_none_is_accepted(self):
        result = self.pe.poker_eval(game="holdem", pockets=HOLDEM_POCKETS,
                                    board=UNKNOWN_BOARD, iterations=100, seed=None)
        self.assertEqual(result["info"][0], 100)

    def test_exhaustive_enumeration_is_unaffected(self):
        board = ["Ah", "Kd", "7c", "__", "__"]
        plain = self.pe.poker_eval(game="holdem", pockets=HOLDEM_POCKETS, board=board)
        seeded_a = self.pe.poker_eval(game="holdem", pockets=HOLDEM_POCKETS, board=board, seed=1)
        seeded_b = self.pe.poker_eval(game="holdem", pockets=HOLDEM_POCKETS, board=board, seed=12345)
        self.assertEqual(plain, seeded_a)
        self.assertEqual(plain, seeded_b)

    def test_exhaustive_call_leaves_the_stream_alone(self):
        previous = PokerEval.base_seed()
        try:
            pokereval.seed(5)
            direct = self.sample()
            pokereval.seed(5)
            self.pe.poker_eval(game="holdem", pockets=HOLDEM_POCKETS,
                               board=["Ah", "Kd", "7c", "2s", "__"], seed=999)
            self.pe.calculate_range_equity(
                "holdem", [["AsKs"], ["QcQh"]],
                board_card_strings=["2c", "3d", "4h", "5s", "9c"], seed=999)
            self.pe.calculate_multiway_equity(
                "holdem", [["AsKs"], ["QcQh"]], [100.0, 100.0],
                board_card_strings=["2c", "3d", "4h", "5s", "9c"], seed=999)
            self.assertEqual(direct, self.sample())
        finally:
            pokereval.seed(previous)

    def test_threads_seeded_identically_agree(self):
        expected = self.sample(seed=42)
        results = [None] * 4
        barrier = threading.Barrier(len(results))

        def worker(index):
            pe = PokerEval()
            barrier.wait()
            # Move this thread's own stream first: the seed must still win.
            pe.poker_eval(game="holdem", pockets=HOLDEM_POCKETS, board=UNKNOWN_BOARD,
                          iterations=100 * (index + 1))
            results[index] = pe.poker_eval(game="holdem", pockets=HOLDEM_POCKETS,
                                           board=UNKNOWN_BOARD, iterations=ITERATIONS, seed=42)

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(len(results))]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        for r in results:
            self.assertEqual(r, expected)

    def test_base_seed(self):
        previous = PokerEval.base_seed()
        try:
            pokereval.seed(5)
            self.assertEqual(PokerEval.base_seed(), 5)
            a = self.sample()
            PokerEval.seed(5)
            b = self.sample()
            self.assertEqual(a, b)
            # A per-call seed equal to the base seed starts the same stream.
            self.assertEqual(a, self.sample(seed=5))
            with self.assertRaises(TypeError):
                pokereval.seed(None)
        finally:
            pokereval.seed(previous)


class TestRangeEquitySeed(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()

    def test_range_equity_seed(self):
        def run(seed):
            return self.pe.calculate_range_equity(
                "holdem", [["AsKs", "AdKd"], ["QcQh"]],
                num_board_cards_to_deal=5, use_montecarlo=True,
                iterations=2000, seed=seed)

        first = run(3)
        run(None)
        run(8)
        self.assertEqual(first, run(3))
        self.assertNotEqual(run(3)["eval"], run(4)["eval"])

    def test_multiway_equity_seed(self):
        def run(seed):
            return self.pe.calculate_multiway_equity(
                # The Python wrapper only expands specific hold'em hands.
                "holdem", [["AsAh", "KcKd"], ["QsQh"], ["JcJd"]],
                [100.0, 80.0, 60.0], use_montecarlo=True,
                iterations=2000, seed=seed)

        first = run(3)
        run(None)
        run(8)
        self.assertEqual(first, run(3))
        self.assertNotEqual(run(3)["players"], run(4)["players"])


if __name__ == "__main__":
    unittest.main()
