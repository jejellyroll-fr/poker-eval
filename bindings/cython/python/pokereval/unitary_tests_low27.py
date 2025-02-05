import pokereval
import logging

# Configurer le logging si nécessaire
logging.basicConfig(level=logging.DEBUG)

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()


def normalize_low_description(description):
    """Supprime les couleurs des cartes dans la description pour Lowball 2-7."""
    suffix = ""
    if description.endswith(" low"):
        # On retire le suffixe ' low' pour normaliser uniquement les cartes
        description_core = description[:-4]
        suffix = " low"
    else:
        description_core = description

    # Traitement sur chaque carte : suppression du dernier caractère si c'est une couleur
    tokens = description_core.split("-")
    tokens = [token[:-1] if token and token[-1] in "cdhs" else token for token in tokens]
    return "-".join(tokens) + suffix


def test_hand_low(game, hand, board, expected_description, expected_cards):
    best_hand = poker_eval.best_hand(
        game=game, side="low", hand=hand, board=board, include_description=True
    )
    
    # Normalisation des descriptions
    description = normalize_low_description(best_hand[0])  
    expected_description = normalize_low_description(expected_description)
    
    cards = best_hand[1:]

    print(f"Best low hand from hand={hand} board={board} = ({description}) {cards}")

    assert (
        description == expected_description
    ), f"Expected description {expected_description}, got {description}"

    assert sorted(cards, key=poker_eval.rank_card, reverse=True) == sorted(
        expected_cards, key=poker_eval.rank_card, reverse=True
    ), f"Expected cards {expected_cards}, got {cards}"


def test_lowball27_best_hand():
    # Meilleure main low: 7-5-4-3-2
    hand = ["2c", "3d", "4h", "5s", "7c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2-3-4-5-7 low",
        expected_cards=hand,
    )


def test_lowball27_with_pair():
    # Main avec une paire, ce qui est mauvais en lowball27
    hand = ["2c", "2d", "3h", "4s", "7c"]
    expected_best_hand = ["7c", "4s", "3h", "2d", "2c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2-2-3-4-7 low",
        expected_cards=expected_best_hand,
    )


def test_lowball27_with_straight():
    # Main formant une quinte, ce qui compte contre vous en lowball27
    hand = ["2c", "3d", "4h", "5s", "6c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2-3-4-5-6 low",
        expected_cards=hand,
    )


def test_lowball27_with_flush():
    # Main formant une couleur, ce qui compte contre vous en lowball27
    hand = ["2c", "5c", "7c", "8c", "9c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2-5-7-8-9 low",
        expected_cards=hand,
    )


def test_lowball27_high_cards():
    # Main avec des cartes hautes, ce qui donne une mauvaise main basse
    hand = ["9d", "Td", "Jc", "Qh", "Kc"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="9-T-J-Q-K low",
        expected_cards=hand,
    )


def test_lowball27_best_hand_from_seven_cards():
    # Choisir la meilleure main parmi sept cartes
    hand = ["2c", "3d", "4h", "7s", "9c", "Td", "Qh"]
    expected_best_hand = ["9c", "7s", "4h", "3d", "2c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2-3-4-7-9 low",
        expected_cards=expected_best_hand,
    )


def test_lowball27_with_ace():
    # L'As est une carte haute en lowball27
    hand = ["2c", "3d", "4h", "5s", "7c", "Ac", "Kh"]
    expected_best_hand = ["7c", "5s", "4h", "3d", "2c"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="2-3-4-5-7 low",
        expected_cards=expected_best_hand,
    )


def test_lowball27_no_pair_high_cards():
    # Main sans paire mais avec des cartes hautes
    hand = ["8c", "9d", "Ts", "Jh", "Qc"]
    test_hand_low(
        game="lowball27",
        hand=hand,
        board=[],
        expected_description="8-9-T-J-Q low",
        expected_cards=hand,
    )


# Exécuter les tests pour lowball27
if __name__ == "__main__":
    test_lowball27_best_hand()
    test_lowball27_with_pair()
    test_lowball27_with_straight()
    test_lowball27_with_flush()
    test_lowball27_high_cards()
    test_lowball27_best_hand_from_seven_cards()
    test_lowball27_with_ace()
    test_lowball27_no_pair_high_cards()
