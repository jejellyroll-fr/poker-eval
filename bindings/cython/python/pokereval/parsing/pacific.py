import re
from datetime import datetime
from typing import List, Optional
from .base import HandHistoryParser
from .dtos import ParserResult, CreateHandDTO, HandPlayerDTO, HandActionDTO

class PacificParser(HandHistoryParser):
    """
    Parser for 888 Poker (Pacific).
    Based on legacy 888ToFpdb.py logic.
    """

    def parse_content(self, content: str) -> ParserResult:
        result = ParserResult()
        # 888 hands start with "#Game No"
        raw_hands = re.split(r"(?=#Game No)", content)

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
        if not lines: return None

        # #Game No : 123456789
        match = re.search(r"#Game No : (\d+)", lines[0])
        if not match: return None
        hand_id = match.group(1)

        players = []
        actions = []
        street = "preflop"
        max_players = 0

        # Seat 1: PlayerName ( $100 )
        seat_regex = r"Seat (\d+): (.*?) \( \$([\d\.]+) \)"

        for line in lines:
            seat_match = re.search(seat_regex, line)
            if seat_match:
                seat = int(seat_match.group(1))
                name = seat_match.group(2)
                stack = float(seat_match.group(3))
                players.append(HandPlayerDTO(name=name, seat=seat, stack=stack))
                if seat > max_players: max_players = seat

            if "** Dealing flop **" in line: street = "flop"

            # 888 Actions
            # PlayerName folds
            # PlayerName calls [$10]
            if " folds" in line:
                player = line.split(" folds")[0]
                actions.append(HandActionDTO(player_name=player, action_type="folds", street=street))

        return CreateHandDTO(
            poker_site="888Poker",
            game_id=hand_id,
            date=datetime.now(),
            game_type="Holdem",
            table_name="Unknown",
            max_players=max_players,
            players=players,
            button_seat=0,
            hero_seat=None,
            actions=actions
        )
