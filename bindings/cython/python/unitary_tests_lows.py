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
    print(f"Best low hand from hand={hand} board={board} = ({description}) {cards}")
    assert (
        description == expected_description
    ), f"Expected description {expected_description}, got {description}"
    assert sorted(cards, key=poker_eval.rank_card, reverse=True) == sorted(
        expected_cards, key=poker_eval.rank_card, reverse=True
    ), f"Expected cards {expected_cards}, got {cards}"


def test_razz_best_hand():
    # Meilleure main low : A-2-3-4-5
    hand = ["Ah", "2c", "3d", "4h", "5s", "9c", "Kd"]
    expected_best_hand = ["Ah", "2c", "3d", "4h", "5s"]
    test_hand_low(
        game="razz",
        hand=hand,
        board=[],
        expected_description="Ah-2c-3d-4h-5s low",
        expected_cards=expected_best_hand,
    )


def test_omaha8_low_best_hand():
    # Meilleure main low : A-2-3-4-8 (qualifiée car 8 or better)
    hand = ["Ah", "2c", "Qd", "Ks"]
    board = ["3s", "4h", "8d", "Td", "Jh"]
    expected_best_hand = ["Ah", "2c", "3s", "4h", "8d"]
    test_hand_low(
        game="omaha8",  # Changement pour correspondre au nom utilisé dans la méthode
        hand=hand,
        board=board,
        expected_description="Ah-2c-3s-4h-8d low",
        expected_cards=expected_best_hand,
    )


def test_lowball27_best_hand():
    # Meilleure main low : 7-5-4-3-2
    hand = ["2c", "3d", "4h", "5s", "7c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2c-3d-4h-5s-7c low",
        expected_cards=hand,
    )


def test_lowball27_with_pair():
    # Main avec une paire, ce qui est mauvais en lowball27
    hand = ["2c", "2d", "3h", "4s", "7c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2d-2c-3h-4s-7c low",
        expected_cards=[
            "2d",
            "2c",
            "3h",
            "4s",
            "7c",
        ],  # La main ne se qualifie pas en raison de la paire
    )


def test_lowball27_with_straight():
    # Main formant une quinte, ce qui est mauvais en lowball27
    hand = ["2c", "3d", "4h", "5s", "6c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2c-3d-4h-5s-6c low",
        expected_cards=[
            "2c",
            "3d",
            "4h",
            "5s",
            "6c",
        ],  # La main ne se qualifie pas en raison de la quinte
    )


def test_lowball27_with_flush():
    # Main formant une couleur, ce qui est mauvais en lowball27
    hand = ["2c", "5c", "7c", "8c", "9c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2c-5c-7c-8c-9c low",
        expected_cards=[
            "2c",
            "5c",
            "7c",
            "8c",
            "9c",
        ],  # La main ne se qualifie pas en raison de la couleur
    )


def test_lowball27_high_cards():
    # Main avec des cartes hautes, ce qui donne une mauvaise main basse
    hand = ["9d", "Td", "Jc", "Qh", "Kc"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="9d-Td-Jc-Qh-Kc low",
        expected_cards=["9d", "Td", "Jc", "Qh", "Kc"],  # Pas de main basse qualifiée
    )


def test_ace_to_five_lowball8_or_better_best_hand():
    # Meilleure main low : A-2-3-4-8 (qualifiée car 8 or better)
    hand = ["Ah", "2c", "3d", "4h", "8s", "9c", "Kd"]
    expected_best_hand = ["Ah", "2c", "3d", "4h", "8s"]
    test_hand_low(
        game="7stud8",
        hand=hand,
        board=[],
        expected_description="Ah-2c-3d-4h-8s low",
        expected_cards=expected_best_hand,
    )


# Exécuter les tests
if __name__ == "__main__":
    test_razz_best_hand()
    test_omaha8_low_best_hand()
    test_lowball27_best_hand()
    test_lowball27_with_pair()
    test_lowball27_with_straight()
    test_lowball27_with_flush()
    test_lowball27_high_cards()
    test_ace_to_five_lowball8_or_better_best_hand()
