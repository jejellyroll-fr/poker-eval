import re
from datetime import datetime
from typing import List, Optional
from .base import HandHistoryParser
from .dtos import ParserResult, CreateHandDTO, HandPlayerDTO, HandActionDTO

class PokerStarsParser(HandHistoryParser):
    """
    Implementation for PokerStars parser.
    """

    def parse_content(self, content: str) -> ParserResult:
        result = ParserResult()
        # PokerStars hands start with "PokerStars Hand #"
        raw_hands = re.split(r"(?=PokerStars Hand #)", content)

        for raw_hand in raw_hands:
            if not raw_hand.strip():
                continue
            try:
                hand_dto = self._parse_single_hand(raw_hand)
                if hand_dto:
                    result.hands.append(hand_dto)
            except Exception as e:
                result.errors.append(f"Error parsing hand: {e}")

        return result

    def parse_file(self, filepath: str) -> ParserResult:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        return self.parse_content(content)

    def _parse_single_hand(self, raw_hand: str) -> Optional[CreateHandDTO]:
        lines = raw_hand.strip().split('\n')
        if not lines:
            return None

        header_line = lines[0]
        # Example: PokerStars Hand #22222222222: Tournament #11111111, $10+$1 USD Hold'em No Limit - Level I (10/20) - 2023/10/27 20:00:00 ET

        match = re.search(r"PokerStars Hand #(\d+):", header_line)
        if not match:
            return None

        hand_id = match.group(1)

        # Determine Game Type, Date, etc. (Simplified)
        game_type = "Holdem"
        if "Omaha" in header_line:
            game_type = "Omaha"

        # Parse date (Stub - extracted from end of line)
        date_match = re.search(r"(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2})", header_line)
        game_date = datetime.strptime(date_match.group(1), "%Y/%m/%d %H:%M:%S") if date_match else datetime.now()

        players = []
        button_seat = 0
        max_players = 0

        # Parse Table line for button
        # Table '123456789 1' 9-max Seat #1 is the button
        for line in lines:
            if line.startswith("Table "):
                btn_match = re.search(r"Seat #(\d+) is the button", line)
                if btn_match:
                    button_seat = int(btn_match.group(1))
                break

        current_line_idx = 1

        # Parse Seats
        # Seat 1: PlayerName (1500 in chips)
        seat_regex = r"Seat (\d+): (.*?) \((\d+) in chips\)"

        while current_line_idx < len(lines):
            line = lines[current_line_idx]

            if "***" in line and "HOLE CARDS" not in line and "SUMMARY" not in line:
                # *** ANTE/BLINDS *** is often omitted in PS regexes or handled implicitly?
                pass

            seat_match = re.search(seat_regex, line)
            if seat_match:
                seat = int(seat_match.group(1))
                name = seat_match.group(2)
                stack = float(seat_match.group(3))
                players.append(HandPlayerDTO(name=name, seat=seat, stack=stack))
                if seat > max_players:
                    max_players = seat
            elif " posts " in line:
                pass # Will parse actions later
            elif "*** HOLE CARDS ***" in line:
                break

            current_line_idx += 1

        actions = []
        street = "preflop"

        # Parse Actions
        for i in range(1, len(lines)):
            line = lines[i].strip()

            if line.startswith("Seat "): continue
            if line.startswith("Table "): continue

            if "*** HOLE CARDS ***" in line:
                street = "preflop"
                continue
            elif "*** FLOP ***" in line:
                street = "flop"
                continue
            elif "*** TURN ***" in line:
                street = "turn"
                continue
            elif "*** RIVER ***" in line:
                street = "river"
                continue
            elif "*** SHOW DOWN ***" in line:
                street = "showdown"
                continue
            elif "*** SUMMARY ***" in line:
                break

            action_dto = None

            # Dealt to
            if "Dealt to " in line:
                match_dealt = re.search(r"Dealt to (.*?) \[(.*?)\]", line)
                if match_dealt:
                    hero_name = match_dealt.group(1)
                    hero_cards = match_dealt.group(2).split()
                    for p in players:
                        if p.name == hero_name:
                            p.is_hero = True
                            p.hole_cards = hero_cards
                continue

            # Check for colon separating player and action
            if ":" not in line:
                continue

            parts = line.split(":", 1)
            player_name = parts[0]
            action_part = parts[1].strip()

            # Parse actions: folds, checks, bets X, calls X, raises X to Y, collects X, shows [X X], mucks
            if "folds" in action_part:
                action_dto = HandActionDTO(player_name=player_name, action_type="folds", street=street)
            elif "checks" in action_part:
                action_dto = HandActionDTO(player_name=player_name, action_type="checks", street=street)
            elif "bets" in action_part:
                match_bet = re.search(r"bets (\d+)", action_part)
                amount = float(match_bet.group(1)) if match_bet else 0.0
                action_dto = HandActionDTO(player_name=player_name, action_type="bets", street=street, amount=amount)
            elif "calls" in action_part:
                match_call = re.search(r"calls (\d+)", action_part)
                amount = float(match_call.group(1)) if match_call else 0.0
                action_dto = HandActionDTO(player_name=player_name, action_type="calls", street=street, amount=amount)
            elif "raises" in action_part:
                # raises 100 to 200
                match_raise = re.search(r"raises (\d+) to (\d+)", action_part)
                if match_raise:
                    amount = float(match_raise.group(2))
                    action_dto = HandActionDTO(player_name=player_name, action_type="raises", street=street, amount=amount)
            elif "collects" in action_part:
                match_col = re.search(r"collects (\d+)", action_part)
                amount = float(match_col.group(1)) if match_col else 0.0
                action_dto = HandActionDTO(player_name=player_name, action_type="collects", street=street, amount=amount)
            elif "shows" in action_part:
                # shows [Ah Kh]
                match_shows = re.search(r"shows \[(.*?)\]", action_part)
                if match_shows:
                    cards = match_shows.group(1).split()
                    action_dto = HandActionDTO(player_name=player_name, action_type="shows", street=street, cards=cards)
                    # Link to hole cards
                    for p in players:
                        if p.name == player_name:
                            p.hole_cards = cards
            elif "mucks" in action_part:
                action_dto = HandActionDTO(player_name=player_name, action_type="mucks", street=street)
            elif "posts" in action_part:
                 match_post = re.search(r"posts (?:small|big|small & big) blind(?:s)? (\d+)", action_part)
                 if match_post:
                     amount = float(match_post.group(1))
                     action_dto = HandActionDTO(player_name=player_name, action_type="posts", street=street, amount=amount)

            if action_dto:
                actions.append(action_dto)

        return CreateHandDTO(
            poker_site="PokerStars",
            game_id=hand_id,
            date=game_date,
            game_type=game_type,
            table_name="Unknown",
            max_players=max_players,
            players=players,
            button_seat=button_seat,
            hero_seat=None,
            actions=actions
        )
