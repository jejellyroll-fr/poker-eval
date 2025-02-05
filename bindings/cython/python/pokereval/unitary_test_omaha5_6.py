import pokereval
import logging

# Configurer le logging si nécessaire
logging.basicConfig(level=logging.DEBUG)

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()


def test_hand(game, hand, board, expected_description, expected_cards):
    """Vérifie la meilleure main retournée par l'évaluation."""
    best_hand = poker_eval.best_hand(
        game=game, side="hi", hand=hand, board=board, include_description=True
    )
    description = best_hand[0]
    cards = best_hand[1:]
    print(f"Best hand from hand={hand} board={board} = ({description}) {cards}")

    assert (
        description == expected_description
    ), f"Expected description {expected_description}, got {description}"
    assert sorted(cards, key=poker_eval.rank_card) == sorted(
        expected_cards, key=poker_eval.rank_card
    ), f"Expected cards {expected_cards}, got {cards}"


### ✅ **Tests pour Omaha 5 cartes**
def test_omaha5_straight():
    # Straight: 7c, 6c de la main et "9d", "8h", "Ts" du board
    hand = ["7c", "6c", "5h", "4d", "3h"]
    board = ["2s", "9d", "8h", "Ts", "Jh"]
    test_hand(
        game="omaha5",
        hand=hand,
        board=board,
        expected_description="Straight (T)",
        expected_cards=["Ts", "9d", "8h", "7c","6c"],
    )


def test_omaha5_flush():
    # Flush: Ah, Kh de la main et Jh, 9h, 5h du board
    hand = ["Ah", "Kh", "Qd", "2d", "5h"]
    board = ["Jh", "9h", "3c", "2s", "8h"]
    test_hand(
        game="omaha5",
        hand=hand,
        board=board,
        expected_description="Flush (A K J 9 8)",
        expected_cards=["Ah", "Kh", "Jh", "9h", "8h"],
    )


def test_omaha5_full_house():
    # Full House: Ah, Ad de la main et As, Kh, Kd du board
    hand = ["Ah", "Ad", "Ks", "2d", "7h"]
    board = ["As", "Kh", "Kd", "3c", "5h"]
    test_hand(
        game="omaha5",
        hand=hand,
        board=board,
        expected_description="FlHouse (A K)",
        expected_cards=["Ah", "Ad", "As", "Kh", "Kd"],
    )


def test_omaha5_quads():
    # Four of a Kind: Ah, As de la main et Ad, Ac du board
    hand = ["Ah", "As", "2d", "3c", "Kd"]
    board = ["Ad", "Ac", "Kh", "5c", "6d"]
    test_hand(
        game="omaha5",
        hand=hand,
        board=board,
        expected_description="Quads (A K)",
        expected_cards=["Ah", "Ad", "Ac", "As", "Kh"],
    )


### **Tests pour Omaha 6 cartes**
def test_omaha6_straight_flush():
    # Straight Flush: 7h, 6h de la main et 5h, 4h, 3h du board
    hand = ["7h", "6h", "2d", "9c", "Jh", "Qs"]
    board = ["5h", "4h", "3h", "Ts", "8s"]
    test_hand(
        game="omaha6",
        hand=hand,
        board=board,
        expected_description="StFlush (7)",
        expected_cards=["7h", "6h", "5h", "4h", "3h"],
    )


def test_omaha6_full_house():
    # Full House: Ah, Ad de la main et As, Kh, Kd du board
    hand = ["Ah", "Ad", "Ks", "2d", "7h", "4c"]
    board = ["As", "Kh", "Kd", "3c", "5h"]
    test_hand(
        game="omaha6",
        hand=hand,
        board=board,
        expected_description="FlHouse (A K)",
        expected_cards=["Ah", "Ad", "As", "Kh", "Kd"],
    )


def test_omaha6_three_of_a_kind():
    # Trips: Ah, As de la main et Ad du board
    hand = ["Ah", "As", "Kd", "2d", "9c", "6h"]
    board = ["Ad", "5c", "2s", "3c", "Jd"]
    test_hand(
        game="omaha6",
        hand=hand,
        board=board,
        expected_description="Trips (A J 5)",
        expected_cards=["Ah", "Ad", "As", "Jd", "5c"],
    )


def test_omaha6_one_pair():
    # One Pair: Ah, Ad de la main et Qc du board
    hand = ["Ah", "Ad", "5c", "7h", "6d", "4s"]
    board = ["Qc", "Jh", "9d", "2s", "3c"]
    test_hand(
        game="omaha6",
        hand=hand,
        board=board,
        expected_description="OnePair (A Q J 9)",
        expected_cards=["Ah", "Ad", "Qc", "Jh", "9d"],
    )


### ✅ **Exécution des tests**
if __name__ == "__main__":
    # Tests Omaha 5 cartes
    test_omaha5_straight()
    test_omaha5_flush()
    test_omaha5_full_house()
    test_omaha5_quads()

    # Tests Omaha 6 cartes
    test_omaha6_straight_flush()
    test_omaha6_full_house()
    test_omaha6_three_of_a_kind()
    test_omaha6_one_pair()
