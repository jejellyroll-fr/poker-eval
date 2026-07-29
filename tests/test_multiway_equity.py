import unittest
from pokereval import PokerEval

class TestMultiwayEquityPython(unittest.TestCase):
    def setUp(self):
        self.pe = PokerEval()
        import random
        random.seed(12345)

    def test_simple_sidepot(self):
        res = self.pe.calculate_multiway_equity(
            "holdem",
            [
                ["AsAh"],
                ["KcKd"],
                ["QsQh"],
            ],
            [100.0, 80.0, 60.0],
            board_card_strings=["2c", "3d", "4h", "5s", "9c"],
        )
        self.assertEqual(res['matchups'], 1)
        self.assertAlmostEqual(res['players'][0]['ev'], 220.0, places=2)
        self.assertAlmostEqual(res['players'][1]['ev'], 0.0, places=1)
        self.assertAlmostEqual(res['players'][2]['ev'], 0.0, places=1)

    def test_weighted_range(self):
        res = self.pe.calculate_multiway_equity(
            "holdem",
            [
                ["AA{75%}", "KK{25%}"],
                ["QQ"],
            ],
            [100.0, 100.0],
            use_montecarlo=True,
            iterations=1000,
        )
        eq_sum = sum(player['equity'] for player in res['players'])
        self.assertTrue(0.7 <= eq_sum <= 1.3)
        total_ev = sum(player['ev'] for player in res['players'])
        self.assertGreater(total_ev, 0.0)
        self.assertLessEqual(total_ev, 200.0)

    def test_tie_distribution(self):
        res = self.pe.calculate_multiway_equity(
            "holdem",
            [
                ["AhKh{0.5}", "AsKs{0.5}"],
                ["AdKd"],
            ],
            [150.0, 150.0],
            board_card_strings=["2h", "3h"],
            use_montecarlo=True,
            iterations=2000,
        )
        self.assertIn('players', res)
        win_plus_tie = res['players'][0]['win_prob'] + res['players'][0]['tie_prob']
        self.assertTrue(0.0 <= win_plus_tie <= 1.5)

if __name__ == '__main__':
    unittest.main()
