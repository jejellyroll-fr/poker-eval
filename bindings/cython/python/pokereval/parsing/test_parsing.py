import unittest
from datetime import datetime
from pokereval.parsing import (
    WinamaxParser, WinamaxSummaryParser, PokerStarsParser,
    WinningParser, PartyPokerParser, IPokerParser, PacificParser
)

class TestWinamaxParser(unittest.TestCase):
    def test_summary_parsing(self):
        content = """Winamax Poker - Tournament "Super Stack" buyIn: 10€ + 1€ level: 5 - HandId: #123456789

        You finished the tournament in 1st place. You won 50.00€."""

        parser = WinamaxSummaryParser()
        result = parser.parse_content(content)

        self.assertEqual(len(result.summaries), 1)
        summary = result.summaries[0]
        self.assertEqual(summary.buyin, 10.0)
        self.assertEqual(summary.rake, 1.0)
        self.assertEqual(summary.rank, 1)
        self.assertEqual(summary.winnings, 50.0)
        self.assertEqual(summary.poker_site, "Winamax")

    def test_hand_parsing(self):
        content = """Winamax Poker - Tournament "Super Stack" buyIn: 10€ + 1€ level: 5 - HandId: #123456789 - Holdem no limit (20/40) - 2023/10/27 20:00:00 UTC
Table: 'Super Stack' 9-max (real money) Seat #1 is the button
Seat 1: PlayerOne (1 000)
Seat 2: PlayerTwo (2000)
*** ANTE/BLINDS ***
PlayerOne posts small blind 20
PlayerTwo posts big blind 40
*** PRE-FLOP ***
PlayerOne calls 20
PlayerTwo checks
*** FLOP *** [As Ks 2d]
PlayerTwo checks
PlayerOne bets 40
PlayerTwo folds
PlayerOne collects 120 from pot
PlayerOne shows [Ah Ad] (One pair : Aces)
*** SUMMARY ***
Total pot 120 | No rake
Board: [As Ks 2d]
Seat 1: PlayerOne won 120
Seat 2: PlayerTwo folded on the Flop"""

        parser = WinamaxParser()
        result = parser.parse_content(content)

        self.assertEqual(len(result.hands), 1)
        hand = result.hands[0]

        self.assertEqual(hand.game_id, "123456789")
        self.assertEqual(hand.game_type, "Holdem no limit")
        self.assertEqual(len(hand.players), 2)
        self.assertEqual(hand.players[0].name, "PlayerOne")
        self.assertEqual(hand.players[0].stack, 1000.0) # Check stack with space parsing

        # Check actions
        self.assertTrue(any(a.action_type == "posts" and a.amount == 20 for a in hand.actions))
        self.assertTrue(any(a.action_type == "bets" and a.amount == 40 for a in hand.actions))
        self.assertTrue(any(a.action_type == "folds" for a in hand.actions))

        # Check revealed cards
        show_action = next((a for a in hand.actions if a.action_type == "shows"), None)
        self.assertIsNotNone(show_action)
        self.assertEqual(show_action.cards, ["Ah", "Ad"])

        # Check linked cards to player
        player_one = next(p for p in hand.players if p.name == "PlayerOne")
        self.assertEqual(player_one.hole_cards, ["Ah", "Ad"])

class TestPokerStarsParser(unittest.TestCase):
    def test_hand_parsing(self):
        content = """PokerStars Hand #22222222222: Tournament #11111111, $10+$1 USD Hold'em No Limit - Level I (10/20) - 2023/10/27 20:00:00 ET
Table '11111111 1' 9-max Seat #1 is the button
Seat 1: PlayerA (1500 in chips)
Seat 2: PlayerB (1500 in chips)
PlayerA: posts small blind 10
PlayerB: posts big blind 20
*** HOLE CARDS ***
Dealt to PlayerA [Ah Kh]
PlayerA: calls 10
PlayerB: raises 20 to 40
PlayerA: calls 20
*** FLOP *** [2s 3s 4s]
PlayerB: bets 40
PlayerA: raises 40 to 80
PlayerB: calls 40
*** TURN *** [2s 3s 4s] [5s]
PlayerB: checks
PlayerA: bets 100
PlayerB: folds
Uncalled bet (100) returned to PlayerA
PlayerA: collects 240 from pot
PlayerA: doesn't show hand
*** SUMMARY ***
Total pot 240 | Rake 0
Board [2s 3s 4s 5s]
"""
        parser = PokerStarsParser()
        result = parser.parse_content(content)

        self.assertEqual(len(result.hands), 1)
        hand = result.hands[0]
        self.assertEqual(hand.game_id, "22222222222")
        self.assertEqual(hand.poker_site, "PokerStars")

        # Check players
        player_a = next(p for p in hand.players if p.name == "PlayerA")
        self.assertTrue(player_a.is_hero)
        self.assertEqual(player_a.hole_cards, ["Ah", "Kh"])

        # Check parsed actions
        actions = hand.actions
        self.assertTrue(any(a.player_name == "PlayerA" and a.action_type == "calls" and a.amount == 10 for a in actions))
        self.assertTrue(any(a.player_name == "PlayerB" and a.action_type == "raises" and a.amount == 40 for a in actions))
        self.assertTrue(any(a.player_name == "PlayerB" and a.action_type == "bets" and a.amount == 40 for a in actions))
        self.assertTrue(any(a.player_name == "PlayerB" and a.action_type == "folds" for a in actions))
        self.assertTrue(any(a.player_name == "PlayerA" and a.action_type == "collects" and a.amount == 240 for a in actions))

class TestWinningParser(unittest.TestCase):
    def test_hand_parsing(self):
        content = """Game ID 123456789 - $0.10/$0.25 No Limit Hold'em - 2023/10/27 20:00:00 UTC
Table 'Test' 6-max Seat #1 is the button
Seat 1: PlayerW1 ($100.00)
Seat 2: PlayerW2 ($50.00)
*** HOLE CARDS ***
Dealt to PlayerW1 [Ah Kh]
PlayerW1 calls $0.25
PlayerW2 folds
"""
        parser = WinningParser()
        result = parser.parse_content(content)
        self.assertEqual(len(result.hands), 1)
        hand = result.hands[0]
        self.assertEqual(hand.poker_site, "Winning")
        self.assertEqual(hand.players[0].name, "PlayerW1")
        self.assertEqual(hand.players[0].stack, 100.0)
        self.assertTrue(hand.players[0].is_hero)

class TestPartyPokerParser(unittest.TestCase):
    def test_hand_parsing(self):
        content = """#Game No : 987654321 ***** Hand History for Game 987654321 *****
$1/$2 USD NL Texas Holdem - Wednesday, October 27, 20:00:00 EST 2023
Table Table 123 (Real Money)
Seat 1: PlayerP1 ( $200.00 USD )
Seat 2: PlayerP2 ( $150.50 USD )
Seat 1 is the button
Total number of players : 2
Seat 1: PlayerP1 ( $200.00 USD )
Seat 2: PlayerP2 ( $150.50 USD )
** Dealing down cards **
PlayerP1 folds
"""
        parser = PartyPokerParser()
        result = parser.parse_content(content)
        self.assertEqual(len(result.hands), 1)
        hand = result.hands[0]
        self.assertEqual(hand.poker_site, "PartyPoker")
        self.assertEqual(hand.game_id, "987654321")
        self.assertEqual(hand.players[0].name, "PlayerP1")
        self.assertEqual(hand.players[0].stack, 200.0)
        self.assertTrue(any(a.action_type == "folds" for a in hand.actions))

class TestIPokerParser(unittest.TestCase):
    def test_hand_parsing(self):
        content = """<game gamecode="333333">
 <general>
  <startdate>2023-10-27 20:00:00</startdate>
  <players>
   <player seat="1" name="PlayerI1" chips="100.00" dealer="1"/>
   <player seat="2" name="PlayerI2" chips="200.00"/>
  </players>
 </general>
 <round no="1" name="Preflop">
  <action no="1" player="PlayerI1" type="Call" sum="€2.00"/>
  <action no="2" player="PlayerI2" type="Check" sum="€0.00"/>
 </round>
</game>"""
        parser = IPokerParser()
        result = parser.parse_content(content)
        self.assertEqual(len(result.hands), 1)
        hand = result.hands[0]
        self.assertEqual(hand.poker_site, "iPoker")
        self.assertEqual(hand.game_id, "333333")
        self.assertEqual(len(hand.players), 2)
        self.assertEqual(hand.players[0].name, "PlayerI1")
        self.assertEqual(hand.actions[0].action_type, "calls")
        self.assertEqual(hand.actions[0].amount, 2.0)

if __name__ == '__main__':
    unittest.main()
