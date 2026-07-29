import re
from datetime import datetime
from typing import List, Optional
from .base import HandHistoryParser
from .dtos import ParserResult, CreateHandDTO, HandPlayerDTO, HandActionDTO

class WinningParser(HandHistoryParser):
    """
    Parser for Winning Poker Network (WPN) / ACR.
    Based on legacy WinningToFpdb.py logic.
    """

    def parse_content(self, content: str) -> ParserResult:
        result = ParserResult()
        # WPN hands usually start with "Game ID"
        raw_hands = re.split(r"(?=Game ID)", content)

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
        # Example: Game ID 123456789 - $0.10/$0.25 No Limit Hold'em - 2023/10/27 20:00:00 UTC

        match = re.search(r"Game ID (\d+)", header_line)
        if not match:
            return None

        hand_id = match.group(1)
        game_type = "Holdem"
        if "Omaha" in header_line:
            game_type = "Omaha"

        # Date parsing
        date_match = re.search(r"(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2})", header_line)
        game_date = datetime.strptime(date_match.group(1), "%Y/%m/%d %H:%M:%S") if date_match else datetime.now()

        players = []
        button_seat = 0
        max_players = 0

        # Parse button seat
        # "Seat #1 is the button"
        for line in lines:
            if "button" in line:
                btn_match = re.search(r"Seat #(\d+) is the button", line)
                if btn_match:
                    button_seat = int(btn_match.group(1))
                    break

        # Parse Players
        # Seat 1: PlayerName ($100.00)
        seat_regex = r"Seat (\d+): (.*?) \(\$([\d\.]+)\)"

        current_line_idx = 1
        while current_line_idx < len(lines):
            line = lines[current_line_idx]

            seat_match = re.search(seat_regex, line)
            if seat_match:
                seat = int(seat_match.group(1))
                name = seat_match.group(2)
                stack = float(seat_match.group(3))
                players.append(HandPlayerDTO(name=name, seat=seat, stack=stack))
                if seat > max_players:
                    max_players = seat
            elif "*** HOLE CARDS ***" in line:
                break
            current_line_idx += 1

        actions = []
        street = "preflop"

        for i in range(current_line_idx, len(lines)):
            line = lines[i].strip()

            if "*** HOLE CARDS ***" in line:
                street = "preflop"
            elif "*** FLOP ***" in line:
                street = "flop"
            elif "*** TURN ***" in line:
                street = "turn"
            elif "*** RIVER ***" in line:
                street = "river"
            elif "*** SHOW DOWN ***" in line:
                street = "showdown"
            elif "*** SUMMARY ***" in line:
                break

            # Dealt to
            if "Dealt to " in line:
                # Dealt to Hero [Ah Kh]
                hero_match = re.search(r"Dealt to (.*?) \[(.*?)\]", line)
                if hero_match:
                    hero_name = hero_match.group(1)
                    hero_cards = hero_match.group(2).split()
                    for p in players:
                        if p.name == hero_name:
                            p.is_hero = True
                            p.hole_cards = hero_cards
                continue

            # Actions
            # Player folds
            # Player calls $0.10
            # Player raises $0.10 to $0.35
            # Player bets $0.10
            # Player checks

            action_dto = None

            # WPN puts name at start
            if " folds" in line:
                player = line.split(" folds")[0]
                action_dto = HandActionDTO(player_name=player, action_type="folds", street=street)
            elif " checks" in line:
                player = line.split(" checks")[0]
                action_dto = HandActionDTO(player_name=player, action_type="checks", street=street)
            elif " calls " in line:
                match = re.search(r"(.*?) calls \$([\d\.]+)", line)
                if match:
                    action_dto = HandActionDTO(player_name=match.group(1), action_type="calls", street=street, amount=float(match.group(2)))
            elif " bets " in line:
                match = re.search(r"(.*?) bets \$([\d\.]+)", line)
                if match:
                    action_dto = HandActionDTO(player_name=match.group(1), action_type="bets", street=street, amount=float(match.group(2)))
            elif " raises " in line:
                match = re.search(r"(.*?) raises .*?\$([\d\.]+)", line) # raises $X to $Y or just raises to $Y? WPN varies.
                if match:
                    action_dto = HandActionDTO(player_name=match.group(1), action_type="raises", street=street, amount=float(match.group(2)))
            elif " collects " in line:
                 match = re.search(r"(.*?) collects \$([\d\.]+)", line)
                 if match:
                     action_dto = HandActionDTO(player_name=match.group(1), action_type="collects", street=street, amount=float(match.group(2)))

            if action_dto:
                actions.append(action_dto)

        return CreateHandDTO(
            poker_site="Winning",
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
