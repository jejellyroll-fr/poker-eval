import re
from datetime import datetime
from typing import Any, List, Optional
from defusedxml.ElementTree import fromstring
from .base import HandHistoryParser
from .dtos import ParserResult, CreateHandDTO, HandPlayerDTO, HandActionDTO

class IPokerParser(HandHistoryParser):
    """
    Parser for iPoker (XML based).
    """

    def parse_content(self, content: str) -> ParserResult:
        # iPoker uses XML. We might need to wrap content in a root if it's multiple hands concatenated.
        # But usually iPoker files are valid XML with <session> root.
        result = ParserResult()
        try:
            # If content doesn't start with <session>, it might be fragments.
            # Simplified: Try to parse <game> blocks.
            raw_games = re.findall(r"(<game.*?</game>)", content, re.DOTALL)

            for raw_game in raw_games:
                try:
                    root = fromstring(raw_game)
                    hand_dto = self._parse_single_hand_xml(root)
                    if hand_dto:
                        result.hands.append(hand_dto)
                except Exception as e:
                    result.errors.append(f"Error parsing hand XML: {e}")
        except Exception as e:
            result.errors.append(f"Error parsing iPoker content: {e}")

        return result

    def parse_file(self, filepath: str) -> ParserResult:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        return self.parse_content(content)

    def _parse_single_hand_xml(self, game_el: Any) -> Optional[CreateHandDTO]:
        hand_id = game_el.get("gamecode")

        # Date
        start_date = game_el.find("startdate")
        game_date = datetime.now()
        if start_date is not None:
             try:
                 game_date = datetime.strptime(start_date.text, "%Y-%m-%d %H:%M:%S")
             except (TypeError, ValueError):
                 pass

        # Players
        players = []
        max_players = 0
        general = game_el.find("general")
        if general is not None:
             players_el = general.find("players")
             if players_el is not None:
                 for p_el in players_el.findall("player"):
                     seat = int(p_el.get("seat"))
                     name = p_el.get("name")
                     chips = float(p_el.get("chips"))
                     players.append(HandPlayerDTO(name=name, seat=seat, stack=chips))
                     if seat > max_players: max_players = seat

        actions = []
        # Rounds
        rounds = game_el.findall("round")
        street_map = { "Preflop": "preflop", "Flop": "flop", "Turn": "turn", "River": "river" }

        for rnd in rounds:
            r_no = rnd.get("no")
            r_name = rnd.get("name")
            street = street_map.get(r_name, "unknown")

            for act in rnd.findall("action"):
                p_name = act.get("player")
                a_type = act.get("type") # "Call", "Fold", "Bet", "Raise"
                amount_str = act.get("sum")
                amount = float(amount_str.replace('€','').replace('$','')) if amount_str else 0.0

                dto_type = "unknown"
                if a_type == "Fold": dto_type = "folds"
                elif a_type == "Call": dto_type = "calls"
                elif a_type == "Check": dto_type = "checks"
                elif a_type == "Bet": dto_type = "bets"
                elif a_type == "Raise": dto_type = "raises"

                if dto_type != "unknown":
                    actions.append(HandActionDTO(player_name=p_name, action_type=dto_type, street=street, amount=amount))

        return CreateHandDTO(
            poker_site="iPoker",
            game_id=hand_id,
            date=game_date,
            game_type="Holdem", # Simplified
            table_name="Unknown",
            max_players=max_players,
            players=players,
            button_seat=0, # Need to parse start info
            hero_seat=None,
            actions=actions
        )
