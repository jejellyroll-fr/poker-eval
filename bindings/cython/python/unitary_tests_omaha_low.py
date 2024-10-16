import pokereval
import logging

# Configurer le logging si nécessaire
logging.basicConfig(level=logging.DEBUG)

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()


def test_hand_low(game, hand, board, expected_description, expected_cards):
    best_hand = poker_eval.best_hand(
        game=game, side="low", hand=hand, board=board, include_description=True
    )
    description = best_hand[0]
    cards = best_hand[1:]
    print(
        f"Best low hand from hand={hand} board={board} = ({description}) {cards}"
    )  # Expected: {expected_description} {expected_cards}
    assert (
        description == expected_description
    ), f"Expected description {expected_description}, got {description}"
    assert sorted(cards, key=poker_eval.rank_card) == sorted(
        expected_cards, key=poker_eval.rank_card
    ), f"Expected cards {expected_cards}, got {cards}"


def test_omaha_low_best_hand():
    # Best low: A-5 low, using Ah, 2h from hand and 3c, 4d, 5s from board
    hand = ["Ah", "2h", "Kh", "Kd"]
    board = ["3c", "4d", "5s", "7h", "9d"]
    test_hand_low(
        game="omaha8",
        hand=hand,
        board=board,
        expected_description="A-5 low",
        expected_cards=["Ah", "2h", "3c", "4d", "5s"],
    )


def test_omaha_ace_to_six_low():
    # Best low: A-6 low, using Ah, 2c from hand and 3s, 4h, 6d from board
    hand = ["Ah", "2c", "Qd", "Ks"]
    board = ["3s", "4h", "6d", "Td", "Jh"]
    test_hand_low(
        game="omaha8",
        hand=hand,
        board=board,
        expected_description="A-6 low",
        expected_cards=["Ah", "2c", "3s", "4h", "6d"],
    )


def test_omaha_ace_to_five_low():
    # Best low: A-5 low, using Ah, 2d from hand and 3h, 4c, 5d from board
    hand = ["Ah", "2d", "8h", "9d"]
    board = ["3h", "4c", "5d", "Qc", "Js"]
    test_hand_low(
        game="omaha8",
        hand=hand,
        board=board,
        expected_description="A-5 low",
        expected_cards=["Ah", "2d", "3h", "4c", "5d"],
    )


def test_omaha_wheel():
    # Best low: A-5 wheel, using Ah, 2s from hand and 3d, 4c, 5h from board
    hand = ["Ah", "2s", "8d", "Jc"]
    board = ["3d", "4c", "5h", "Kh", "Qs"]
    test_hand_low(
        game="omaha8",
        hand=hand,
        board=board,
        expected_description="A-5 low",
        expected_cards=["Ah", "2s", "3d", "4c", "5h"],
    )


def test_omaha_low_with_duplicates():
    # Best low: A-6 low, using Ah, 2c from hand and 3s, 4h, 6d from board, ignoring duplicate values
    hand = ["Ah", "2c", "6s", "Ks"]
    board = ["3s", "4h", "6d", "Td", "6c"]
    test_hand_low(
        game="omaha8",
        hand=hand,
        board=board,
        expected_description="A-6 low",
        expected_cards=["Ah", "2c", "3s", "4h", "6d"],
    )


def test_omaha_low_no_qualifying_hand():
    # No qualifying low hand: all cards in hand and board are too high
    hand = ["Kh", "Kd", "Qs", "Jd"]
    board = ["Td", "9d", "8c", "7h", "6s"]
    test_hand_low(
        game="omaha8",
        hand=hand,
        board=board,
        expected_description="No low hand",
        expected_cards=[],
    )


# Exécuter les tests Omaha Low
if __name__ == "__main__":
    test_omaha_low_best_hand()
    test_omaha_ace_to_six_low()
    test_omaha_ace_to_five_low()
    test_omaha_wheel()
    test_omaha_low_with_duplicates()
    test_omaha_low_no_qualifying_hand()
