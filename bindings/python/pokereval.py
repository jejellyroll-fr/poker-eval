#
# Copyright (C) 2007, 2008 Loic Dachary <loic@dachary.org>
# Copyright (C) 2004, 2005, 2006 Mekensleep
#
# Mekensleep
# 24 rue vieille du temple
# 75004 Paris
#       licensing@mekensleep.com
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
#
# Authors:
#  Loic Dachary <loic@dachary.org>
#
# 
import sys

# CMake builds the native extension as ``pypokereval``.  Importing that stable
# module name keeps the Python wrapper independent of the interpreter's patch
# version and matches the name exported by PyInit_pypokereval.
import pypokereval as _pokereval



if sys.version_info[0] < 3:
    from types import ListType, TupleType
else:
    ListType, TupleType = list, tuple
    xrange = range

class PokerEval:
    """\
Evaluate the strengh of a poker hand for a given poker variant.
In all methods, when a list of cards is to be provided (for instance
with the "hand" argument of the "best" method), each member of the
list may be a number or a string designating a card according to
the following table:

       2h/00  2d/13  2c/26  2s/39
       3h/01  3d/14  3c/27  3s/40
       4h/02  4d/15  4c/28  4s/41
       5h/03  5d/16  5c/29  5s/42
       6h/04  6d/17  6c/30  6s/43
       7h/05  7d/18  7c/31  7s/44
       8h/06  8d/19  8c/32  8s/45
       9h/07  9d/20  9c/33  9s/46
       Th/08  Td/21  Tc/34  Ts/47
       Jh/09  Jd/22  Jc/35  Js/48
       Qh/10  Qd/23  Qc/36  Qs/49
       Kh/11  Kd/24  Kc/37  Ks/50
       Ah/12  Ad/25  Ac/38  As/51

The string __ (two underscore) or the number 255 are placeholders
meaning that the card is unknown.
"""

    def best(self, side, hand, board=None):
        """\
Return the best five card combination that can be made with the cards
listed in "hand" and, optionally, board. The "side" may be "hi" or
"low". The "board" argument must only be provided for variants where
knowing if a given card is taken from the board or not is significant
(such as Omaha but not Holdem).

A list is returned. The first element is the numerical value
of the hand (better hands have higher values if "side" is "hi" and
lower values if "side" is "low"). The second element is a list whose
first element is the strength of the hand among the following:

Nothing (only if "side" equals "low")
NoPair
TwoPair
Trips
Straight
Flush
FlHouse
Quads
StFlush

The last five elements are numbers describing the best hand properly
sorted (for instance the ace is at the end for no pair if "side" is low or
at the beginning if "side" high).

Examples:

[134414336, ['StFlush', 29, 28, 27, 26, 38]] is the wheel five to ace, clubs
[475920, ['NoPair', 45, 29, 41, 39, 51]] is As, 8s, 5c, 4s, 2s 
[268435455, ['Nothing']] means there is no qualifying low
"""
        if board is None:
            board = []
        if len(hand + board) >= 5:
            return _pokereval.eval_hand(side, hand, board)
        else:
            return False

    def _expand_player_ranges(self, game_name, list_of_player_range_definitions, board_card_strings, dead_card_strings):
        processed_player_ranges_uint64 = []

        is_omaha_type = "omaha" in game_name.lower()
        is_stud_type = "stud" in game_name.lower()

        game_total_cards_for_stud = 7
        if is_stud_type and "5" in game_name:
            game_total_cards_for_stud = 5

        for p_idx, player_range_def_strings in enumerate(list_of_player_range_definitions):
            if not isinstance(player_range_def_strings, (list, tuple)):
                raise TypeError(f"Player {p_idx+1} range definition must be a list of strings.")

            current_player_expanded = []
            temp_dead_for_generation = list(board_card_strings) + list(dead_card_strings)

            for range_str in player_range_def_strings:
                specific_hands_as_str_lists = []
                try:
                    if is_omaha_type:
                        specific_hands_as_str_lists = self.omaha_hands(range_str, dead_cards=temp_dead_for_generation)
                    elif is_stud_type:
                        specific_hands_as_str_lists = self.get_stud_hands(
                            range_str,
                            game_total_cards=game_total_cards_for_stud,
                            dead_cards=temp_dead_for_generation
                        )
                    else:
                        if game_name.lower() in ["holdem", "holdem8", "shortdeck", "shortdeckholdem"]:
                            if len(range_str) == 4 and range_str.count('x') == 0 and range_str.count('X') == 0:
                                specific_hands_as_str_lists = [[range_str[0:2], range_str[2:4]]]
                            else:
                                print(
                                    f"Warning: Hold'em/other range string '{range_str}' is not a specific 2-card hand "
                                    "(e.g. AsKs) and full expansion is not supported by this Python method. It will "
                                    "likely be skipped or result in an error if not parsable by underlying utilities.",
                                    file=sys.stderr
                                )
                        else:
                            print(
                                f"Warning: Range string '{range_str}' for game type '{game_name}' might not be correctly "
                                "expanded. Assuming it's a pre-defined hand if possible.",
                                file=sys.stderr
                            )
                except RuntimeError as e_inst:
                    print(f"Warning: Error instantiating range string '{range_str}': {e_inst}", file=sys.stderr)
                    specific_hands_as_str_lists = []

                for hand_as_str_list in specific_hands_as_str_lists:
                    if not isinstance(hand_as_str_list, (list, tuple)) or not all(isinstance(c, str) for c in hand_as_str_list):
                        print(
                            f"Warning: Expected list of card strings from hand generator for '{range_str}', got "
                            f"{hand_as_str_list}. Skipping this hand.",
                            file=sys.stderr
                        )
                        continue
                    try:
                        mask_val = _pokereval.convert_card_strings_to_mask_value(hand_as_str_list)
                        current_player_expanded.append(mask_val)
                    except RuntimeError as e_mask:
                        print(
                            f"Warning: Could not convert hand {hand_as_str_list} to mask for range '{range_str}': {e_mask}",
                            file=sys.stderr
                        )

            if not current_player_expanded:
                print(
                    f"Warning: Player {p_idx+1} has no valid hands in their expanded range. "
                    "This might lead to 0 matchups or errors if other players have hands.",
                    file=sys.stderr
                )

            processed_player_ranges_uint64.append(list(current_player_expanded))

        return processed_player_ranges_uint64

    def best_hand(self, side, hand, board=None):
        """\
Return the best five card combination that can be made with the cards
listed in "hand" and, optionaly, board. The "side" may be "hi" or
"low". The returned value is the second element of the list returned
by the "best" method.
"""
        if board is None:
            board = []
        if len(hand + board) >= 5:
            return _pokereval.eval_hand(side, hand, board)[1]
        else:
            return False

    def best_hand_value(self, side, hand, board=None):
        """\
Return the best five card combination that can be made with the cards
listed in "hand" and, optionaly, board. The "side" may be "hi" or
"low". The returned value is the first element of the list returned
by the "best" method.
"""
        if board is None:
            board = []
        if len(hand + board) >= 5:
            return _pokereval.eval_hand(side, hand, board)[0]
        else:
            return False

    def evaln(self, cards):
        """\
Call the poker-eval Hand_EVAL_N function with the "cards" argument.
Return the strength of the "cards" as a number. The higher the
better.
"""
        return _pokereval.evaln(cards)
    
    def winners(self, *args, **kwargs):
        """\
Return a list of the indexes of the best hands, relative to the "pockets"
keyword argument. For instance, if the first pocket and third pocket cards
tie, the list would be [0, 2]. Since there may be more than one way to
win a hand, a hash is returned with the list of the winners for each so
called side. For instace {'hi': [0], 'low': [1]} means pocket cards
at index 0 won the high side of the hand and pocket cards at index 1
won the low side.

See the"poker_eval" method for a detailed
explanation of the semantics of the arguments.

If the keyword argument "fill_pockets" is set, pocket cards
can contain a placeholder (i.e. 255 or __) that will be be
used as specified in the "poker_eval" method documentation.

If the keyword argument "fill_pockets" is not set, pocket cards
that contain at least one placeholder (i.e. 255 or __) are
ignored completly. For instance if winners is called as follows
o.winners(game = 'holdem', pockets = [ [ '__', 'As' ], [ 'Ks', 'Kd'] ])
it is strictly equivalent as calling
o.winners(game = 'holdem', pockets = [ [ 'Ks', 'Kd'] ]).
"""
        index2index = {}
        normalized_pockets = []
        normalized_index = 0
        pockets = kwargs["pockets"][:]
        for index in xrange(len(pockets)):
            if "fill_pockets" not in kwargs and (255 in pockets[index] or "__" in pockets[index]):
                pockets[index] = []

            if pockets[index] != []:
                normalized_pockets.append(pockets[index])
                index2index[index] = normalized_index
                normalized_index += 1
        kwargs["pockets"] = normalized_pockets
        
        results = _pokereval.poker_eval(*args, **kwargs)

        (count, haslopot, hashipot) = results.pop(0)
        winners = { 'low': [], 'hi': [] }
        for index in xrange(len(pockets)):
            if index in index2index:
                result = results[index2index[index]]
                if result[1] == 1 or result[3] == 1:
                    winners["hi"].append(index)
                if result[4] == 1 or result[6] == 1:
                    winners["low"].append(index)

        if not haslopot or len(winners["low"]) == 0:
            del winners["low"]
        if not hashipot:
            del winners["hi"]
        return winners
        
    def poker_eval(self, *args, **kwargs):
        """\
Provided with a description of a poker game, return the outcome (if at showdown) or
the expected value of each hand. The poker game description is provided as a set
of keyword arguments with the following meaning:

game      : the variant (holdem, holdem8, omaha, omaha8, 7stud, 7stud8, razz,
            5draw, 5draw8, 5drawnsq, lowball, lowball27). 
            Mandatory, no default.
                    
pockets   : list of pocket cards for each player still in game. Each member
            of the list is a list of cards. The position of the pocket cards
            in the list is meaningfull for the value returned will refer to
            this position when stating which player wins, tie or loose.
            Example: [ ["tc", "ac"],  ["3h", "ah"],  ["8c", "6h"]]
            Cards do not have to be real cards like "tc" or "4s". They may also be a 
            placeholder, denoted by "__" or 255. When using placeholders, the 
            keyword argument "iterations" may be specified to use Monte Carlo instead of
            exhaustive exploration of all the possible combinations.
            Example2: [ ["tc", "__"],  [255, "ah"],  ["8c", "6h"]]

            Mandatory, no default.

board     : list of community cards, for games where this is meaningfull. If
            specified when irrelevant, the return value cannot be predicted.
            Default: []

dead      : list of dead cards. These cards won't be accounted for when exloring
            the possible hands.
            Default: []

iterations: the maximum number of iterations when exploring the
            possible outcome of a given hand. Roughly speaking, each
            iteration means to distribute cards that are missing (for
            which there are place holders in the board or pockets
            keywords arguments, i.e. 255 or __). If the number of
            iterations is not specified and there are place holders,
            the return value cannot be predicted.
            Default: +infinite (i.e. exhaustive exploration)

Example: object.poker_eval(game = "holdem",
                           pockets = [ ["tc", "ac"],  ["3h", "ah"],  ["8c", "6h"]],
                           dead = [],
                           board = ["7h", "3s", "2c"])

The return value is a map of two entries:
'info' contains three integers:
 - the number of samples (which must be equal to the number of iterations given
   in argument).
 - 1 if the game has a low side, 0 otherwise
 - 1 if the game has a high side, 0 otherwise
'eval' is a list of as many maps as there are pocket cards, each
made of the following entries:
 'scoop': the number of time these pocket cards scoop
 'winhi': the number of time these pocket cards win the high side
 'losehi': the number of time these pocket cards lose the high side
 'tiehi': the number of time these pocket cards tie for the high side
 'winlo': the number of time these pocket cards win the low side
 'loselo': the number of time these pocket cards lose the low side
 'tielo': the number of time these pocket cards tie for the low side
 'ev': the EV of these pocket cards as an int in the range [0,1000] with
       1000 being the best.

It should be clear that if there is only one sample (i.e. because all the
cards are known which is the situation that occurs at showdown) the details
provided by the 'eval' entry is mostly irrelevant and the caller might
prefer to call the winners method instead.
"""
        result = _pokereval.poker_eval(*args, **kwargs)
        return {
            'info': result[0],
            'eval': [ { 'scoop': x[0],
                        'winhi': x[1],
                        'losehi': x[2],
                        'tiehi': x[3],
                        'winlo': x[4],
                        'loselo': x[5],
                        'tielo': x[6],
                        'ev': int(x[7] * 1000) } for x in result[1:] ]
            }

    def deck(self):
        """\
Return the list of all cards in the deck.
"""
        return [ self.string2card(i + j) for i in "23456789TJQKA" for j in "hdcs" ]

    def nocard(self):
        """Return 255, the numerical value of a place holder in a list of cards."""
        return 255

    def string2card(self, cards):
        """\
Convert card names (strings) to card numbers (integers) according to the
following map:

       2h/00  2d/13  2c/26  2s/39
       3h/01  3d/14  3c/27  3s/40
       4h/02  4d/15  4c/28  4s/41
       5h/03  5d/16  5c/29  5s/42
       6h/04  6d/17  6c/30  6s/43
       7h/05  7d/18  7c/31  7s/44
       8h/06  8d/19  8c/32  8s/45
       9h/07  9d/20  9c/33  9s/46
       Th/08  Td/21  Tc/34  Ts/47
       Jh/09  Jd/22  Jc/35  Js/48
       Qh/10  Qd/23  Qc/36  Qs/49
       Kh/11  Kd/24  Kc/37  Ks/50
       Ah/12  Ad/25  Ac/38  As/51

The "cards" argument may be either a list in which case a converted list
is returned or a string in which case the corresponding number is
returned.
"""
        if isinstance(cards, (ListType, TupleType)):
            return [ _pokereval.string2card(card) for card in cards ]
        else:
            return _pokereval.string2card(cards)

    def card2string(self, cards):
        """\
Convert card numbers (integers) to card names (strings) according to the
following map:

       2h/00  2d/13  2c/26  2s/39
       3h/01  3d/14  3c/27  3s/40
       4h/02  4d/15  4c/28  4s/41
       5h/03  5d/16  5c/29  5s/42
       6h/04  6d/17  6c/30  6s/43
       7h/05  7d/18  7c/31  7s/44
       8h/06  8d/19  8c/32  8s/45
       9h/07  9d/20  9c/33  9s/46
       Th/08  Td/21  Tc/34  Ts/47
       Jh/09  Jd/22  Jc/35  Js/48
       Qh/10  Qd/23  Qc/36  Qs/49
       Kh/11  Kd/24  Kc/37  Ks/50
       Ah/12  Ad/25  Ac/38  As/51

The "cards" argument may be either a list in which case a converted list
is returned or an integer in which case the corresponding string is
returned.
"""
        if isinstance(cards, (ListType, TupleType)):
            return [ _pokereval.card2string(card) for card in cards ]
        else:
            return _pokereval.card2string(cards)

    def omaha_hands(self, omaha_hand_str, dead_cards=None):
        """Generates all possible Omaha hands matching a given pattern string,
considering specified dead cards.

Args:
    omaha_hand_str (str): The Omaha hand pattern string.
        Examples: "AsKdQhJc" (specific hand),
                  "AAxx" (two Aces, two other cards),
                  "AKQJds" (Ace, King, Queen, Jack, double-suited),
                  "JT98r" (Jack, Ten, Nine, Eight, rainbow).
    dead_cards (list of str, optional): A list of dead card strings,
        e.g., ["Ah", "Td"]. Defaults to None or an empty list.

Returns:
    list of list of str: A list where each inner list represents a
    unique 4-card Omaha hand matching the criteria. Each card is
    represented as a string (e.g., "As").
    Returns an empty list if no hands match or if the input string
    is invalid (though the C extension might raise an error for
    invalid input).
        Example: [["Ac", "Ad", "Kc", "Kd"], ["As", "Ah", "Ks", "Kh"]]
        """
        if dead_cards is None:
            dead_cards = [] # Pass empty list if None
        
        # The C extension py_omaha_hand_instantiate is defined to handle
        # the dead_cards argument being None (translating to NULL in C) or a list.
        # If dead_cards is an empty list, PyList2CardMask will correctly produce an empty mask.
        return _pokereval.omaha_hand_instantiate(omaha_hand_str, dead_cards)

    def get_stud_hands(self, stud_hand_str, game_total_cards=7, dead_cards=None):
        """Generates all possible Stud hands matching a given pattern string,
for a specified total number of cards per hand (e.g., 7 for 7-Card Stud),
considering specified dead cards.

Args:
    stud_hand_str (str): The Stud hand pattern string.
        Down cards are enclosed in parentheses. 'x' denotes a wildcard.
        Examples: "(AsKdAc)QhJhTh9s" (specific 7-card hand),
                  "(AA)Kxxxx" (Aces in hole, King up, four unknown for 7-card stud),
                  "(AhKh)QhJhxx" (Ah,Kh down; Qh,Jh up; two unknown for 6th street in 7-card stud,
                                 will be filled to game_total_cards).
    game_total_cards (int, optional): The total number of cards each generated
        hand should have. Defaults to 7 (for 7-Card Stud). Typically 5 or 7.
    dead_cards (list of str, optional): A list of dead card strings,
        e.g., ["Ah", "Td"]. Defaults to None.

Returns:
    list of list of str: A list where each inner list represents a
    unique Stud hand of 'game_total_cards' cards matching the criteria.
    Each card is represented as a string (e.g., "As").
    Returns an empty list if no hands match or if the input is invalid
    (the C extension might raise an error for invalid input).
        Example for a 3-card pattern "(AA)K" in 3-card game:
            get_stud_hands("(AA)K", game_total_cards=3) could return
            [['As', 'Ad', 'Kc'], ['As', 'Ac', 'Kd'], ...]
        """
        # The C extension py_stud_hand_instantiate is defined to handle
        # the dead_cards argument being None or a list.
        return _pokereval.stud_hand_instantiate(stud_hand_str, game_total_cards, dead_cards)

    def calculate_range_equity(self, game_name, list_of_player_range_definitions, 
                               board_card_strings=None, dead_card_strings=None, 
                               num_board_cards_to_deal=0, 
                               use_montecarlo=False, iterations=0, orderflag=0):
        """
Calculates equity for player ranges against each other.

Each player's range can be defined by one or more hand distribution strings
(e.g., "AAxx", "AKs", "(AsKd)Qhxxx"). These strings are expanded into all
possible specific hands, and then equity is calculated for every combination
of specific hands across all player ranges.

Args:
    game_name (str): The poker variant (e.g., "holdem", "omaha", "7stud").
    list_of_player_range_definitions (list of list of str): 
        A list where each inner list contains hand distribution strings
        for a player. 
        Example for 2 players: 
        [ 
            ["AAxx", "KKxx"],  # Player 1's Omaha range definitions
            ["QQJJ", "AsKdQhJc"]  # Player 2's Omaha range definitions
        ]
        For Hold'em, currently simple specific hands like "AsKs" are best supported
        for expansion, e.g., [ ["AsKs", "AdKd"], ["QcQh"] ]. More complex
        Hold'em ranges like "AJs+" or "TT+" are not yet expanded by this Python method.
    board_card_strings (list of str, optional): Community cards. Default None.
    dead_card_strings (list of str, optional): Dead cards. Default None.
    num_board_cards_to_deal (int, optional): Number of additional board cards to deal.
        Relevant if the board is partial. E.g., for Hold'em, if board_card_strings
        is empty, this would be 5. If flop is given, this would be 2. Default 0.
    use_montecarlo (bool, optional): True to use Monte Carlo simulation, False for
        exhaustive enumeration. Default False.
    iterations (int, optional): Number of iterations if use_montecarlo is True.
        Default 0 (ignored if not Monte Carlo).
    orderflag (int, optional): Order flag for C evaluation (rarely used). Default 0.

Returns:
    dict: A dictionary structured similarly to `poker_eval`'s return:
        {
            'info': (total_matchups_evaluated, haslopot, hashipot),
            'eval': [ 
                {player_1_results_dict}, # Keys: 'ev', 'nwinhi', etc.
                {player_2_results_dict},
                ...
            ]
        }
        The 'ev' is the average equity for the player's entire range against
        the other players' entire ranges (float between 0.0 and 1.0). 
        Counts (nwinhi etc.) are totals across all matchups.
        Returns None or raises RuntimeError on critical error.
        """
        if board_card_strings is None:
            board_card_strings = []
        if dead_card_strings is None:
            dead_card_strings = []

        num_players = len(list_of_player_range_definitions)
        if num_players == 0:
            return {'info': (0, 0, 0), 'eval': []}

        processed_player_ranges_uint64 = self._expand_player_ranges(
            game_name,
            list_of_player_range_definitions,
            board_card_strings,
            dead_card_strings
        )

        raw_results = _pokereval.calculate_equity_for_ranges(
            game_name,
            processed_player_ranges_uint64,
            board_card_strings,
            num_board_cards_to_deal,
            1 if use_montecarlo else 0, # C function expects int for bool
            iterations,
            dead_card_strings, 
            orderflag
        )

        if not raw_results or not isinstance(raw_results, list) or len(raw_results) < 1:
            # This might occur if C extension returns NULL due to an internal error not caught by Python-side checks
            raise RuntimeError("C extension for range equity calculation returned invalid or empty data.")

        info_tuple = raw_results[0]
        eval_list = []
        
        # Number of player result tuples should be num_players
        if len(raw_results[1:]) != num_players:
             raise RuntimeError(f"C extension returned results for {len(raw_results[1:])} players, but expected {num_players}.")

        for i in range(num_players):
            player_res_tuple = raw_results[i+1]
            # Ensure tuple has expected number of elements (8: scoop, winhi, losehi, tiehi, winlo, loselo, tielo, ev)
            if not isinstance(player_res_tuple, tuple) or len(player_res_tuple) != 8:
                raise RuntimeError(f"Player result tuple for player {i+1} has incorrect format or length.")

            eval_list.append({
                'nscoop': player_res_tuple[0],
                'nwinhi': player_res_tuple[1],
                'nlosehi': player_res_tuple[2],
                'ntiehi': player_res_tuple[3],
                'nwinlo': player_res_tuple[4],
                'nloselo': player_res_tuple[5],
                'ntielo': player_res_tuple[6],
                'ev': player_res_tuple[7] 
            })
        
        return {
            'info': info_tuple, 
            'eval': eval_list
        }
    
    def calculate_multiway_equity(self, game_name, list_of_player_range_definitions,
                                  invested_amounts,
                                  board_card_strings=None, dead_card_strings=None,
                                  num_board_cards_to_deal=0,
                                  use_montecarlo=False, iterations=0, orderflag=0):
        if board_card_strings is None:
            board_card_strings = []
        if dead_card_strings is None:
            dead_card_strings = []

        num_players = len(list_of_player_range_definitions)
        if num_players == 0:
            return {
                'matchups': 0,
                'total_weighted_samples': 0.0,
                'players': []
            }

        if len(invested_amounts) != num_players:
            raise ValueError("Length of invested amounts must match number of players")

        processed_player_ranges_uint64 = self._expand_player_ranges(
            game_name,
            list_of_player_range_definitions,
            board_card_strings,
            dead_card_strings
        )

        payload = []
        for combos in processed_player_ranges_uint64:
            payload.append([int(mask) for mask in combos])

        invested_floats = [float(x) for x in invested_amounts]

        raw_result = _pokereval.calculate_multiway_equity(
            game_name,
            payload,
            invested_floats,
            board_card_strings,
            dead_card_strings,
            num_board_cards_to_deal,
            1 if use_montecarlo else 0,
            iterations,
            orderflag
        )

        if not isinstance(raw_result, dict):
            raise RuntimeError("C extension for multiway equity returned invalid data")

        players = []
        ev_list = raw_result.get('ev', [])
        equity_list = raw_result.get('equity', [])
        win_prob_list = raw_result.get('win_prob', [])
        tie_prob_list = raw_result.get('tie_prob', [])

        for idx in range(num_players):
            players.append({
                'ev': ev_list[idx] if idx < len(ev_list) else 0.0,
                'equity': equity_list[idx] if idx < len(equity_list) else 0.0,
                'win_prob': win_prob_list[idx] if idx < len(win_prob_list) else 0.0,
                'tie_prob': tie_prob_list[idx] if idx < len(tie_prob_list) else 0.0
            })

        return {
            'matchups': raw_result.get('matchups', 0),
            'total_weighted_samples': raw_result.get('total_weighted_samples', 0.0),
            'players': players
        }
        
