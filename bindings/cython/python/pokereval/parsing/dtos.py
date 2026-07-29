from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any
from datetime import datetime

@dataclass
class ParsedTournamentSummaryDTO:
    poker_site: str
    tournament_id: str
    buyin: float
    rake: float
    total_players: Optional[int] = None
    rank: Optional[int] = None
    winnings: float = 0.0
    currency: str = "EUR"
    timestamp: Optional[datetime] = None

@dataclass
class HandPlayerDTO:
    name: str
    seat: int
    stack: float
    hole_cards: List[str] = field(default_factory=list)
    is_hero: bool = False

@dataclass
class HandActionDTO:
    player_name: str
    action_type: str  # e.g., "folds", "calls", "raises", "checks", "bets", "posts", "collects", "shows", "mucks"
    street: str       # "preflop", "flop", "turn", "river", "showdown"
    amount: float = 0.0
    cards: List[str] = field(default_factory=list) # revealed cards
    all_in: bool = False

@dataclass
class CreateHandDTO:
    poker_site: str
    game_id: str
    date: datetime
    game_type: str  # "Holdem", "Omaha", etc.
    table_name: str
    max_players: int
    button_seat: int
    hero_seat: Optional[int]
    players: List[HandPlayerDTO] = field(default_factory=list)
    actions: List[HandActionDTO] = field(default_factory=list)
    total_pot: float = 0.0
    rake: float = 0.0
    currency: str = "EUR"
    winners: List[Dict[str, Any]] = field(default_factory=list) # [{'name': '...', 'amount': 100}]

@dataclass
class ParserResult:
    hands: List[CreateHandDTO] = field(default_factory=list)
    summaries: List[ParsedTournamentSummaryDTO] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
