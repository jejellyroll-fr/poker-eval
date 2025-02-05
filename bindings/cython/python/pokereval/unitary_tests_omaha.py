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
    assert (
        description == expected_description
    ), f"Expected description {expected_description}, got {description}"
    assert sorted(cards, key=poker_eval.rank_card) == sorted(
        expected_cards, key=poker_eval.rank_card
    ), f"Expected cards {expected_cards}, got {cards}"


def test_omaha_hi_best_hand():
    # Straight: 7c, 6c from hand and 5h, 4d, 3s from board
    hand = ["7c", "6c", "5h", "4d"]
    board = ["3s", "2h", "9d", "8h", "Ts"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="Straight (T)",
        expected_cards=["Ts", "9d", "8h", "7c", "6c"],
    )


def test_omaha_flush():
    # Flush: Ah, Kh from hand and Jh, 9h, 5h from board
    hand = ["Ah", "Kh", "Qd", "2d"]
    board = ["Jh", "9h", "5h", "3c", "2s"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="Flush (A K J 9 5)",
        expected_cards=["Ah", "Kh", "Jh", "9h", "5h"],
    )


def test_omaha_straight_flush():
    # Straight Flush: 7h, 6h from hand and 5h, 4h, 3h from board
    hand = ["7h", "6h", "2d", "9c"]
    board = ["5h", "4h", "3h", "Jh", "2s"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="StFlush (7)",
        expected_cards=["7h", "6h", "5h", "4h", "3h"],
    )


def test_omaha_full_house():
    # Full House: Ah, Ad from hand and As, Kh, Kd from board
    hand = ["Ah", "Ad", "Ks", "2d"]
    board = ["As", "Kh", "Kd", "3c", "5h"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="FlHouse (A K)",
        expected_cards=["Ah", "Ad", "As", "Kh", "Kd"],
    )


def test_omaha_straight():
    # Straight: 9h, 8d from hand and 7h, 6c, 5s from board
    hand = ["9h", "8d", "2c", "3d"]
    board = ["7h", "6c", "5s", "Qs", "Jd"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="Straight (9)",
        expected_cards=["9h", "8d", "7h", "6c", "5s"],
    )


def test_omaha_three_of_a_kind():
    # Trips: Ah, As from hand and Ad, 5c, 2s from board
    hand = ["Ah", "As", "Kd", "2d"]
    board = ["Ad", "5c", "2s", "3c", "Jd"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="Trips (A J 5)",
        expected_cards=["Ah", "Ad", "As", "Jd", "5c"],
    )


def test_omaha_four_of_a_kind():
    # Four of a Kind: Ah, As from hand and Ad, Ac, Kh from board
    hand = ["Ah", "As", "2d", "3c"]
    board = ["Ad", "Ac", "Kh", "5c", "6d"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="Quads (A K)",
        expected_cards=["Ah", "Ad", "Ac", "As", "Kh"],
    )


def test_omaha_two_pair():
    # Two Pair: Kh, Kd from hand and 5c, 5s, 9d from board
    hand = ["Kh", "Kd", "2c", "3d"]
    board = ["5c", "5s", "9d", "7h", "2s"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="TwoPair (K 5 9)",
        expected_cards=["Kh", "Kd", "5c", "5s", "9d"],
    )


def test_omaha_one_pair():
    # One Pair: Ah, Ad from hand and Qc, Jh, 9d from board
    hand = ["Ah", "Ad", "5c", "7h"]
    board = ["Qc", "Jh", "9d", "2s", "3c"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="OnePair (A Q J 9)",
        expected_cards=["Ah", "Ad", "Qc", "Jh", "9d"],
    )


def test_omaha_high_card():
    # High Card: Ah, Kd from hand and Jc, Ts, 9d from board
    hand = ["Ah", "Kd", "5h", "9d"]
    board = ["Jc", "Ts", "8s", "2s", "3c"]
    test_hand(
        game="omaha",
        hand=hand,
        board=board,
        expected_description="NoPair (A K J T 8)",
        expected_cards=["Ah", "Kd", "Jc", "Ts", "8s"],
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


# Exécuter les tests Omaha Hi
if __name__ == "__main__":
    test_omaha_hi_best_hand()
    test_omaha_flush()
    test_omaha_straight_flush()
    test_omaha_full_house()
    test_omaha_straight()
    test_omaha_three_of_a_kind()
    test_omaha_four_of_a_kind()
    test_omaha_two_pair()
    test_omaha_one_pair()
    test_omaha_high_card()
