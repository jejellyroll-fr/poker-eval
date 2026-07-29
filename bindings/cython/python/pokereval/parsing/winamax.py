import re
from datetime import datetime
from typing import List, Optional, Tuple
from .base import HandHistoryParser
from .dtos import ParserResult, ParsedTournamentSummaryDTO, CreateHandDTO, HandPlayerDTO, HandActionDTO

class WinamaxSummaryParser(HandHistoryParser):
    def parse_content(self, content: str) -> ParserResult:
        result = ParserResult()
        summary = self._parse_summary(content)
        if summary:
            result.summaries.append(summary)
        return result

    def parse_file(self, filepath: str) -> ParserResult:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        return self.parse_content(content)

    def _parse_summary(self, content: str) -> Optional[ParsedTournamentSummaryDTO]:
        # Regex for Winamax tournament summary
        # Example: "Winamax Poker - Tournament "Super Stack" buyIn: 10€ + 1€ level: 5 - ..."
        # OR summary file content:
        # "You finished the tournament in 1st place. You won 50.00€."

        # We need to extract tournament ID, buyin, rank, winnings.

        # Try to find Tournament ID and Buyin from header if present
        tid_match = re.search(r"Tournament\s+[\"'](.+?)[\"']", content) # Name
        # Actually Winamax summaries often look like:
        # "Winamax Poker - Tournament "Sunday Surprise" buyIn: 10€ + 1€ level: 25 ..."

        buyin = 0.0
        rake = 0.0
        currency = "EUR"
        tournament_id = "Unknown"

        # Parsing header line for BuyIn
        # Winamax Header: Winamax Poker - Tournament "Tie Break" buyIn: 0.90€ + 0.10€ level: 0 - HandId: ...
        header_match = re.search(r"Winamax Poker - Tournament \"(.*?)\" buyIn: ([\d\.]+)[\€\$£] \+ ([\d\.]+)[\€\$£]", content)
        if header_match:
            # This is likely a hand history file, not just a summary, but we can extract summary info
            # But usually Summary files are separate text files in Winamax.
            pass

        # Let's assume this handles the specific "summary" file format or extracts from HH header.
        # If it's a summary file:
        # "Winamax Poker - Tournament "Super Stack" buyIn: 10€ + 1€ level: 5..."

        # Search for "You finished the tournament in Xth place"
        rank_match = re.search(r"You finished the tournament in (\d+)(?:st|nd|rd|th) place", content)
        rank = int(rank_match.group(1)) if rank_match else None

        # Search for winnings "You won X€"
        winnings = 0.0
        win_match = re.search(r"You won ([\d\.]+)[\€\$£]", content)
        if win_match:
            winnings = float(win_match.group(1))

        # If we can't find specific summary markers, maybe return None
        if rank is None and winnings == 0.0:
            # Check if it's just a hand history file being passed to SummaryParser
            pass

        # Try to parse buyin from header
        buyin_match = re.search(r"buyIn: ([\d\.]+)[\€\$£] \+ ([\d\.]+)[\€\$£]", content)
        if buyin_match:
            buyin = float(buyin_match.group(1))
            rake = float(buyin_match.group(2))

        # Tournament ID
        # "Tournament "Name"" -> Name is not ID.
        # Usually file name has ID. But content might have it?
        # Winamax doesn't always put numeric ID in summary text, it uses the Name.

        if rank is not None or buyin > 0:
            return ParsedTournamentSummaryDTO(
                poker_site="Winamax",
                tournament_id=tournament_id,
                buyin=buyin,
                rake=rake,
                rank=rank,
                winnings=winnings,
                currency=currency
            )
        return None

class WinamaxParser(HandHistoryParser):
    def parse_content(self, content: str) -> ParserResult:
        result = ParserResult()
        # Split content by hands. Winamax hands usually start with "Winamax Poker - "
        raw_hands = re.split(r"(?=Winamax Poker - )", content)

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
        # Example: Winamax Poker - Tournament "Super Stack" buyIn: 10€ + 1€ level: 5 - HandId: #123456789 - Holdem no limit (20/40) - 2023/10/27 20:00:00 UTC

        # Regex for Header
        header_regex = r"Winamax Poker - (Tournament|CashGame) \"(.*?)\".*HandId: #(\d+) - (.*?) \((.*?)\) - (\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}) UTC"
        match = re.search(header_regex, header_line)
        if not match:
            return None

        game_category = match.group(1)
        table_name = match.group(2)
        hand_id = match.group(3)
        game_variant = match.group(4) # "Holdem no limit"
        blinds_str = match.group(5) # "20/40" or "0.05/0.10"
        date_str = match.group(6)

        game_date = datetime.strptime(date_str, "%Y/%m/%d %H:%M:%S")

        # Parse Players
        players = []
        button_seat = 0
        max_players = 0 # Will count or infer

        # Seat 1: PlayerName (1000)
        # Note: Winamax stacks often include spaces, e.g. (1 000€)
        seat_regex = r"Seat (\d+): (.*?) \(([\d\.\s]+)[\€\$£]?\)"

        current_line_idx = 1
        while current_line_idx < len(lines):
            line = lines[current_line_idx]
            if "Table:" in line: # Cash game tables sometimes have "Table: 'Name' 5-max"
                pass

            seat_match = re.search(seat_regex, line)
            if seat_match:
                seat = int(seat_match.group(1))
                name = seat_match.group(2)
                stack = float(seat_match.group(3).replace(' ', '')) # Remove spaces in stack "1 000"

                players.append(HandPlayerDTO(name=name, seat=seat, stack=stack))
                if seat > max_players:
                    max_players = seat
            elif "posts" in line:
                break # End of seat declarations
            elif "*** ANTE/BLINDS ***" in line:
                break

            current_line_idx += 1

        # Identify Button
        # "Table: 'Super Stack' 9-max (real money) Seat #1 is the button"
        # Winamax actually says "Seat #X is the button" in the line after header usually?
        # Wait, the example header above is one line.
        # Usually line 2 is "Table '...': Seat #X is the button"
        button_match = re.search(r"Seat #(\d+) is the button", raw_hand)
        if button_match:
            button_seat = int(button_match.group(1))

        actions = []
        street = "preflop"

        # Parsing Actions
        # Iterate through remaining lines
        for i in range(current_line_idx, len(lines)):
            line = lines[i].strip()

            if "*** ANTE/BLINDS ***" in line:
                continue
            elif "*** PRE-FLOP ***" in line:
                street = "preflop"
                continue
            elif "*** FLOP ***" in line:
                street = "flop"
                # "*** FLOP *** [As Ks 2d]"
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

            # Parse individual actions
            # "PlayerName calls 40"
            # "PlayerName raises 40 to 80"
            # "PlayerName folds"
            # "PlayerName checks"
            # "PlayerName bets 100"
            # "PlayerName collects 200 from pot"
            # "PlayerName shows [As Ks] (One pair : Aces)"

            # Use regex to find player name and action
            # We need to match player names which can contain spaces.
            # Best is to check if line starts with a known player name?
            # Or assume Action is at the end.

            # Generic action parsing
            # Actions: folds, checks, calls X, bets X, raises X to Y, collects X, shows [X X]

            player_acted = None
            action_dto = None

            # Check for "shows" (revealed cards)
            if " shows [" in line:
                # "PlayerName shows [Ah Kd] (One pair : Aces)"
                match_shows = re.search(r"(.*?) shows \[(.*?)\]", line)
                if match_shows:
                    player_name = match_shows.group(1)
                    cards_str = match_shows.group(2)
                    cards = cards_str.split()
                    action_dto = HandActionDTO(
                        player_name=player_name,
                        action_type="shows",
                        street=street,
                        cards=cards
                    )
                    # Link to player hole cards
                    for p in players:
                        if p.name == player_name:
                            p.hole_cards = cards

            elif " folds" in line:
                match_fold = re.search(r"(.*?) folds", line)
                if match_fold:
                    action_dto = HandActionDTO(
                        player_name=match_fold.group(1),
                        action_type="folds",
                        street=street
                    )
            elif " checks" in line:
                match_check = re.search(r"(.*?) checks", line)
                if match_check:
                    action_dto = HandActionDTO(
                        player_name=match_check.group(1),
                        action_type="checks",
                        street=street
                    )
            elif " calls " in line:
                match_call = re.search(r"(.*?) calls ([\d\.]+)", line)
                if match_call:
                    action_dto = HandActionDTO(
                        player_name=match_call.group(1),
                        action_type="calls",
                        street=street,
                        amount=float(match_call.group(2))
                    )
            elif " bets " in line:
                match_bet = re.search(r"(.*?) bets ([\d\.]+)", line)
                if match_bet:
                    action_dto = HandActionDTO(
                        player_name=match_bet.group(1),
                        action_type="bets",
                        street=street,
                        amount=float(match_bet.group(2))
                    )
            elif " raises " in line:
                # "Player raises 40 to 80"
                match_raise = re.search(r"(.*?) raises (?:[\d\.]+ to )?([\d\.]+)", line)
                if match_raise:
                    action_dto = HandActionDTO(
                        player_name=match_raise.group(1),
                        action_type="raises",
                        street=street,
                        amount=float(match_raise.group(2))
                    )
            elif " collects " in line:
                 match_col = re.search(r"(.*?) collects ([\d\.]+)", line)
                 if match_col:
                    action_dto = HandActionDTO(
                        player_name=match_col.group(1),
                        action_type="collects",
                        street=street,
                        amount=float(match_col.group(2))
                    )
            elif " posts " in line:
                # "Player posts small blind 20"
                match_post = re.search(r"(.*?) posts (?:small blind|big blind|ante) ([\d\.]+)", line)
                if match_post:
                     action_dto = HandActionDTO(
                        player_name=match_post.group(1),
                        action_type="posts",
                        street=street,
                        amount=float(match_post.group(2))
                    )

            if action_dto:
                actions.append(action_dto)

        # Dealt to Hero check
        # "Dealt to HeroName [Ah As]"
        hero_match = re.search(r"Dealt to (.*?) \[(.*?)\]", raw_hand)
        if hero_match:
            hero_name = hero_match.group(1)
            hero_cards = hero_match.group(2).split()
            for p in players:
                if p.name == hero_name:
                    p.is_hero = True
                    p.hole_cards = hero_cards

        return CreateHandDTO(
            poker_site="Winamax",
            game_id=hand_id,
            date=game_date,
            game_type=game_variant,
            table_name=table_name,
            max_players=max_players,
            players=players,
            button_seat=button_seat,
            hero_seat=None, # Todo: infer from is_hero
            actions=actions
        )
