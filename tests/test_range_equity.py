import unittest
import sys
import os

# Adjust path to import PokerEval from bindings/python/pokereval.py
# This might need adjustment based on actual directory structure and how tests are run
# For example, if tests/ is at the same level as bindings/
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'bindings', 'python'))
from pokereval import PokerEval

class TestRangeEquity(unittest.TestCase):

    def setUp(self):
        self.pe = PokerEval()

    def assert_equity_results_valid(self, results, expected_num_players, expected_total_matchups=None):
        self.assertIsNotNone(results)
        self.assertIn('info', results)
        self.assertIn('eval', results)
        
        info = results['info']
        evals = results['eval']
        
        self.assertIsInstance(info, tuple)
        self.assertEqual(len(info), 3) # (total_matchups, haslopot, hashipot)
        if expected_total_matchups is not None:
            self.assertEqual(info[0], expected_total_matchups, "Mismatch in total matchups evaluated.")

        self.assertIsInstance(evals, list)
        self.assertEqual(len(evals), expected_num_players, "Mismatch in number of player evaluations.")

        for player_eval in evals:
            self.assertIsInstance(player_eval, dict)
            self.assertIn('ev', player_eval)
            self.assertIsInstance(player_eval['ev'], float)
            self.assertTrue(0.0 <= player_eval['ev'] <= 1.0, f"EV out of bounds: {player_eval['ev']}")
            # Check other keys if necessary, e.g., 'nwinhi'
            self.assertIn('nwinhi', player_eval)


    def test_01_hand_vs_hand_holdem_exact(self):
        print("\nRunning test_01_hand_vs_hand_holdem_exact...")
        # AsAh vs KcKd, Board: 2c 3c 4d
        # Using existing poker_eval to get baseline
        baseline_results = self.pe.poker_eval(
            game="holdem",
            pockets=[["As", "Ah"], ["Kc", "Kd"]],
            board=["2c", "3c", "4d"]
        )
        # print("Baseline poker_eval:", baseline_results)
        # EV is scaled 0-1000 in poker_eval, so baseline_ev1 = results['eval'][0]['ev'] / 1000.0
        
        baseline_ev1 = baseline_results['eval'][0]['ev'] / 1000.0
        baseline_ev2 = baseline_results['eval'][1]['ev'] / 1000.0

        range_results = self.pe.calculate_range_equity(
            game_name="holdem",
            list_of_player_range_definitions=[
                [["AsAh"]],  # Player 1 range (one specific hand string)
                [["KcKd"]]   # Player 2 range (one specific hand string)
            ],
            board_card_strings=["2c", "3c", "4d"],
            num_board_cards_to_deal=2 # 5 total board cards - 3 known = 2 to deal
        )
        # print("Range equity results:", range_results)
        self.assert_equity_results_valid(range_results, 2, expected_total_matchups=1)
        self.assertAlmostEqual(range_results['eval'][0]['ev'], baseline_ev1, places=5)
        self.assertAlmostEqual(range_results['eval'][1]['ev'], baseline_ev2, places=5)
        print("test_01_hand_vs_hand_holdem_exact PASSED")

    def test_02_hand_vs_range_holdem_simple(self):
        print("\nRunning test_02_hand_vs_range_holdem_simple...")
        # AsAh vs. Range [KcKd, QcQd], Board: 2c 3c 4d
        # Expected: EV should be average of (AA vs KK) and (AA vs QQ)
        
        # AA vs KK
        res_aa_kk = self.pe.poker_eval(game="holdem", pockets=[["As", "Ah"], ["Kc", "Kd"]], board=["2c", "3c", "4d"])
        ev_aa_kk_p1 = res_aa_kk['eval'][0]['ev'] / 1000.0
        ev_aa_kk_p2 = res_aa_kk['eval'][1]['ev'] / 1000.0
        
        # AA vs QQ
        res_aa_qq = self.pe.poker_eval(game="holdem", pockets=[["As", "Ah"], ["Qc", "Qd"]], board=["2c", "3c", "4d"])
        ev_aa_qq_p1 = res_aa_qq['eval'][0]['ev'] / 1000.0
        ev_aa_qq_p2 = res_aa_qq['eval'][1]['ev'] / 1000.0
        
        expected_p1_ev = (ev_aa_kk_p1 + ev_aa_qq_p1) / 2.0
        expected_p2_ev = (ev_aa_kk_p2 + ev_aa_qq_p2) / 2.0


        range_results = self.pe.calculate_range_equity(
            game_name="holdem",
            list_of_player_range_definitions=[
                [["AsAh"]], # Player 1 range strings
                [["KcKd"], ["QcQd"]] # Player 2 range strings
            ],
            board_card_strings=["2c", "3c", "4d"],
            num_board_cards_to_deal=2 
        )
        # print("Range equity results (AA vs [KK,QQ]):", range_results)
        self.assert_equity_results_valid(range_results, 2, expected_total_matchups=2)
        self.assertAlmostEqual(range_results['eval'][0]['ev'], expected_p1_ev, places=5)
        self.assertAlmostEqual(range_results['eval'][1]['ev'], expected_p2_ev, places=5)
        print("test_02_hand_vs_range_holdem_simple PASSED")

    def test_03_omaha_hand_vs_small_range(self):
        print("\nRunning test_03_omaha_hand_vs_small_range...")
        # AsKsQsJs vs Range ["AcAdKhKd", "2c2d3h3d"] (specific hands)
        # Board: 7h 8d 9c
        
        # P1: AsKsQsJs, P2: AcAdKhKd
        res_m1 = self.pe.poker_eval(
            game="omaha", 
            pockets=[["As","Ks","Qs","Js"], ["Ac","Ad","Kh","Kd"]], 
            board=["7h","8d","9c"]
        )
        ev_m1_p1 = res_m1['eval'][0]['ev'] / 1000.0
        ev_m1_p2 = res_m1['eval'][1]['ev'] / 1000.0

        # P1: AsKsQsJs, P2: 2c2d3h3d
        res_m2 = self.pe.poker_eval(
            game="omaha", 
            pockets=[["As","Ks","Qs","Js"], ["2c","2d","3h","3d"]], 
            board=["7h","8d","9c"]
        )
        ev_m2_p1 = res_m2['eval'][0]['ev'] / 1000.0
        ev_m2_p2 = res_m2['eval'][1]['ev'] / 1000.0

        expected_p1_ev = (ev_m1_p1 + ev_m2_p1) / 2.0
        expected_p2_ev = (ev_m1_p2 + ev_m2_p2) / 2.0

        range_results = self.pe.calculate_range_equity(
            game_name="omaha",
            list_of_player_range_definitions=[
                [["AsKsQsJs"]], # Player 1 range strings
                [["AcAdKhKd"], ["2c2d3h3d"]]  # Player 2 range strings
            ],
            board_card_strings=["7h", "8d", "9c"],
            num_board_cards_to_deal=2 # 5 board cards - 3 known = 2 to deal
        )
        # print("Range equity results (Omaha H v R):", range_results)
        self.assert_equity_results_valid(range_results, 2, expected_total_matchups=2)
        self.assertAlmostEqual(range_results['eval'][0]['ev'], expected_p1_ev, places=5)
        self.assertAlmostEqual(range_results['eval'][1]['ev'], expected_p2_ev, places=5)
        print("test_03_omaha_hand_vs_small_range PASSED")

    def test_04_range_vs_range_holdem_simple(self):
        print("\nRunning test_04_range_vs_range_holdem_simple...")
        # P1 Range: [AsAh, KsKh] vs P2 Range: [QcQd, JcJd]
        # Board: 2c 3c 4d, 2 cards to deal
        
        ev_sum_p1 = 0.0
        matchups = [
            (["AsAh"], ["QcQd"]), (["AsAh"], ["JcJd"]),
            (["KsKh"], ["QcQd"]), (["KsKh"], ["JcJd"])
        ]
        
        for p1_hand_str_list, p2_hand_str_list in matchups:
             # poker_eval expects pockets as list of list of card strings
            p1_pockets = [[p1_hand_str_list[0][:2], p1_hand_str_list[0][2:]]]
            p2_pockets = [[p2_hand_str_list[0][:2], p2_hand_str_list[0][2:]]]
            res = self.pe.poker_eval(
                game="holdem", 
                pockets=[p1_pockets[0], p2_pockets[0]], 
                board=["2c", "3c", "4d"]
            )
            ev_sum_p1 += res['eval'][0]['ev'] / 1000.0
        
        expected_p1_ev = ev_sum_p1 / len(matchups)

        range_results = self.pe.calculate_range_equity(
            game_name="holdem",
            list_of_player_range_definitions=[
                [["AsAh"], ["KsKh"]], # P1 range def
                [["QcQd"], ["JcJd"]]  # P2 range def
            ],
            board_card_strings=["2c", "3c", "4d"],
            num_board_cards_to_deal=2
        )
        self.assert_equity_results_valid(range_results, 2, expected_total_matchups=4)
        self.assertAlmostEqual(range_results['eval'][0]['ev'], expected_p1_ev, places=5)
        print("test_04_range_vs_range_holdem_simple PASSED")

    def test_05_omaha_range_expansion(self):
        print("\nRunning test_05_omaha_range_expansion...")
        # P1: "AAxx" (should expand) vs P2: "KKxx" (should expand)
        
        range_results = self.pe.calculate_range_equity(
            game_name="omaha",
            list_of_player_range_definitions=[
                ["AAxx"], 
                ["KKxx"]  
            ],
            board_card_strings=[], 
            num_board_cards_to_deal=5 
        )
        self.assert_equity_results_valid(range_results, 2)
        self.assertTrue(range_results['info'][0] > 1000, 
                        f"Expected many matchups, got {range_results['info'][0]}") 
        print("test_05_omaha_range_expansion PASSED (check for >1000 matchups)")


    def test_06_conflict_handling_holdem(self):
        print("\nRunning test_06_conflict_handling_holdem...")
        # P1 Range: [AsAh], P2 Range: [AsKc, KsKd]
        # Expected: Only AsAh vs KsKd is valid. 1 matchup.
        
        res_aa_kk = self.pe.poker_eval(
            game="holdem", 
            pockets=[["As", "Ah"], ["Ks", "Kd"]], 
            board=[] 
        )
        expected_p1_ev = res_aa_kk['eval'][0]['ev'] / 1000.0

        range_results = self.pe.calculate_range_equity(
            game_name="holdem",
            list_of_player_range_definitions=[
                [["AsAh"]], 
                [["AsKc"], ["KsKd"]] 
            ],
            board_card_strings=[],
            num_board_cards_to_deal=5
        )
        self.assert_equity_results_valid(range_results, 2, expected_total_matchups=1)
        self.assertAlmostEqual(range_results['eval'][0]['ev'], expected_p1_ev, places=5)
        print("test_06_conflict_handling_holdem PASSED")

    def test_07_dead_card_impact_omaha(self):
        print("\nRunning test_07_dead_card_impact_omaha...")
        # P1: "AAAAqs" 
        # P2: "KKKKqs" 
        # Dead: As 
        
        range_results = self.pe.calculate_range_equity(
            game_name="omaha",
            list_of_player_range_definitions=[
                ["AAAAqs"], 
                ["KKKKqs"]
            ],
            board_card_strings=[],
            num_board_cards_to_deal=5,
            dead_card_strings=["As"] 
        )
        self.assert_equity_results_valid(range_results, 2, expected_total_matchups=0)
        if range_results['info'][0] == 0:
            self.assertEqual(range_results['eval'][0]['ev'], 0.0) 
            self.assertEqual(range_results['eval'][1]['ev'], 0.0)
        print("test_07_dead_card_impact_omaha PASSED")

    def test_08_stud_7card_hand_vs_range(self):
        print("\nRunning test_08_stud_7card_hand_vs_range...")
        # P1: (AsKsQs)JsTs9s8s (specific 7-card hand)
        # P2 Range: [(AhKhQh)JhTh9h8h, (AdKdQd)JdTd9d8d] (two specific 7-card hands)
        
        p1_hand_str = "(AsKsQs)JsTs9s8s"
        p2_range_def_strs = ["(AhKhQh)JhTh9h8h", "(AdKdQd)JdTd9d8d"]

        # Manually expand for baseline
        p1_hands_expanded = self.pe.get_stud_hands(p1_hand_str, game_total_cards=7)
        p1_hand_for_eval = p1_hands_expanded[0] # Should be only one

        ev_sum_p1 = 0.0
        num_matchups = 0

        for p2_h_str in p2_range_def_strs:
            p2_hands_expanded = self.pe.get_stud_hands(p2_h_str, game_total_cards=7)
            if p2_hands_expanded: # Should be one hand
                p2_hand_for_eval = p2_hands_expanded[0]
                res_m = self.pe.poker_eval(
                    game="7stud", 
                    pockets=[p1_hand_for_eval, p2_hand_for_eval]
                )
                ev_sum_p1 += res_m['eval'][0]['ev'] / 1000.0
                num_matchups +=1
        
        expected_p1_ev = ev_sum_p1 / num_matchups if num_matchups > 0 else 0.0

        range_results = self.pe.calculate_range_equity(
            game_name="7stud", 
            list_of_player_range_definitions=[
                [p1_hand_str], 
                p2_range_def_strs 
            ],
            # game_total_cards=7 is implicit for get_stud_hands if not passed,
            # but explicit in calculate_range_equity's internal call if is_stud_type
            num_board_cards_to_deal=0 
        )
        self.assert_equity_results_valid(range_results, 2, expected_total_matchups=num_matchups)
        self.assertAlmostEqual(range_results['eval'][0]['ev'], expected_p1_ev, places=5)
        print("test_08_stud_7card_hand_vs_range PASSED")

    def test_09_montecarlo_vs_exhaustive_simple_holdem(self):
        print("\nRunning test_09_montecarlo_vs_exhaustive_simple_holdem...")
        
        exhaustive_results = self.pe.calculate_range_equity(
            game_name="holdem",
            list_of_player_range_definitions=[ [["AsAh"]], [["KsKh"]] ],
            num_board_cards_to_deal=5,
            use_montecarlo=False
        )
        self.assert_equity_results_valid(exhaustive_results, 2, expected_total_matchups=1)
        ev_exhaustive_p1 = exhaustive_results['eval'][0]['ev']

        montecarlo_results = self.pe.calculate_range_equity(
            game_name="holdem",
            list_of_player_range_definitions=[ [["AsAh"]], [["KsKh"]] ],
            num_board_cards_to_deal=5,
            use_montecarlo=True,
            iterations=20000 
        )
        self.assert_equity_results_valid(montecarlo_results, 2, expected_total_matchups=1)
        ev_montecarlo_p1 = montecarlo_results['eval'][0]['ev']
        
        self.assertAlmostEqual(ev_montecarlo_p1, ev_exhaustive_p1, places=2, 
                               msg="Monte Carlo EV should be close to Exhaustive EV")
        print("test_09_montecarlo_vs_exhaustive_simple_holdem PASSED")

if __name__ == '__main__':
    unittest.main()
