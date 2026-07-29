import re
from datetime import datetime
from typing import List, Optional
from .base import HandHistoryParser
from .dtos import ParserResult, CreateHandDTO, HandPlayerDTO, HandActionDTO

class PartyPokerParser(HandHistoryParser):
    """
    Parser for PartyPoker.
    Based on legacy PartyPokerToFpdb.py logic.
    """

    def parse_content(self, content: str) -> ParserResult:
        result = ParserResult()
        # PartyPoker hands start with "#Game No"
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

        header_line = lines[0]
        # #Game No : 123456789 ***** Hand History for Game 123456789 *****

        match = re.search(r"#Game No : (\d+)", header_line)
        if not match: return None
        hand_id = match.group(1)

        game_date = datetime.now() # Parsing PP date is tricky, simplified

        players = []
        actions = []
        street = "preflop"
        button_seat = 0
        max_players = 0

        # Parse Seats
        # Seat 1: PlayerName ( $100 USD )
        seat_regex = r"Seat (\d+): (.*?) \( \$(.*?) \w+ \)"

        for line in lines:
            if "button" in line:
                btn_match = re.search(r"Seat #(\d+) is the button", line)
                if btn_match: button_seat = int(btn_match.group(1))

            seat_match = re.search(seat_regex, line)
            if seat_match:
                seat = int(seat_match.group(1))
                name = seat_match.group(2)
                stack_str = seat_match.group(3).replace(',', '')
                players.append(HandPlayerDTO(name=name, seat=seat, stack=float(stack_str)))
                if seat > max_players: max_players = seat

            if "** Dealing Flop **" in line: street = "flop"
            elif "** Dealing Turn **" in line: street = "turn"
            elif "** Dealing River **" in line: street = "river"

            # Actions
            # PlayerName folds
            # PlayerName calls [$10]
            # PlayerName bets [$10]

            action_dto = None
            if " folds" in line:
                player = line.split(" folds")[0]
                action_dto = HandActionDTO(player_name=player, action_type="folds", street=street)
            elif " bets " in line:
                match = re.search(r"(.*?) bets \[\$([\d\.,]+)\]", line)
                if match:
                    action_dto = HandActionDTO(player_name=match.group(1), action_type="bets", street=street, amount=float(match.group(2).replace(',','')))

            if action_dto:
                actions.append(action_dto)

        return CreateHandDTO(
            poker_site="PartyPoker",
            game_id=hand_id,
            date=game_date,
            game_type="Holdem",
            table_name="Unknown",
            max_players=max_players,
            players=players,
            button_seat=button_seat,
            hero_seat=None,
            actions=actions
        )
