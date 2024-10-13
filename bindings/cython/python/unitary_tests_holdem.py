import pokereval
import logging

# Configurer le logging si nécessaire
logging.basicConfig(level=logging.DEBUG)

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()


def test_hand(game, hand, board, expected_description, expected_cards):
    best_hand = poker_eval.best_hand(
        game=game, side="hi", hand=hand, board=board, include_description=True
    )
    description = best_hand[0]
    cards = best_hand[1:]
    print(
        f"Best hand from hand={hand} board={board} = ({description}) {cards}"
    )  # Expected: {expected_description} {expected_cards}
    assert description.startswith(
        expected_description
    ), f"Expected description to start with '{expected_description}', got '{description}'"
    assert sorted(cards, key=poker_eval.rank_card) == sorted(
        expected_cards, key=poker_eval.rank_card
    ), f"Expected cards {expected_cards}, got {cards}"


def test_texas_holdem_best_hand():
    # Straight to Ace (Broadway)
    hand = ["Ah", "4d"]
    board = ["9h", "Kd", "Qc", "Jh", "Ts"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="Straight (A)",
        expected_cards=["Ah", "Kd", "Qc", "Jh", "Ts"],
    )


def test_flush():
    # Flush
    hand = ["Ah", "Kh"]
    board = ["Qh", "Jh", "9h", "2d", "3c"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="Flush (A K Q J 9)",
        expected_cards=["Ah", "Kh", "Qh", "Jh", "9h"],
    )


def test_straight_flush():
    # Straight Flush
    hand = ["8h", "7h"]
    board = ["6h", "5h", "4h", "2d", "3c"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="StFlush (8)",
        expected_cards=["8h", "7h", "6h", "5h", "4h"],
    )


def test_full_house():
    # Full House
    hand = ["Ah", "Ad"]
    board = ["As", "Kh", "Kd", "3c", "2d"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="FlHouse (A K)",
        expected_cards=["Ah", "Ad", "As", "Kh", "Kd"],
    )


def test_straight():
    # Straight to Nine
    hand = ["9h", "8d"]
    board = ["7h", "6c", "5s", "Qs", "Jd"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="Straight (9)",
        expected_cards=["9h", "8d", "7h", "6c", "5s"],
    )


def test_three_of_a_kind():
    # Three of a Kind (Trips)
    hand = ["Ah", "As"]
    board = ["Ad", "Kh", "5c", "3d", "2s"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="Trips (A K 5)",
        expected_cards=["Ah", "Ad", "As", "Kh", "5c"],
    )


def test_four_of_a_kind():
    # Four of a Kind
    hand = ["Ah", "As"]
    board = ["Ad", "Ac", "2d", "5s", "6c"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="Quads (A 6)",
        expected_cards=["Ah", "Ad", "Ac", "As", "6c"],  # Ajusté si nécessaire
    )


def test_two_pair():
    # Two Pair
    hand = ["Kh", "Kd"]
    board = ["3c", "3s", "9d", "7h", "5c"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="TwoPair",
        expected_cards=["Kh", "Kd", "3c", "3s", "9d"],
    )


def test_one_pair():
    # One Pair
    hand = ["Ah", "Ad"]
    board = ["3c", "5h", "9d", "7h", "2c"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="OnePair",
        expected_cards=["Ah", "Ad", "9d", "7h", "5h"],
    )


def test_high_card():
    # High Card
    hand = ["Ah", "Kd"]
    board = ["5h", "9d", "3c", "2s", "Jc"]
    test_hand(
        game="holdem",
        hand=hand,
        board=board,
        expected_description="NoPair",
        expected_cards=["Ah", "Kd", "Jc", "9d", "5h"],
    )


def test_card_conversion():
    """Test de la conversion des cartes."""
    test_cards = ["Ah", "Kd", "Qs", "Jc", "Ts", "9d", "8h", "__"]
    for card in test_cards:
        try:
            card_num = poker_eval.string2card(card)
            card_str = poker_eval.card2string(card_num)
            print(f"Card: {card} -> Num: {card_num} -> Str: {card_str}")
        except ValueError as e:
            print(f"Erreur pour la carte {card}: {e}")


# Exécuter les tests
if __name__ == "__main__":
    test_texas_holdem_best_hand()
    test_flush()
    test_straight_flush()
    test_full_house()
    test_straight()
    test_three_of_a_kind()
    test_four_of_a_kind()
    test_two_pair()
    test_one_pair()
    test_high_card()
    test_card_conversion()
