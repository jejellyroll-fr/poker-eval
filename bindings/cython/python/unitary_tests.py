import sys
import pokereval

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()


def test_hand(game, hand, expected_description, expected_cards):
    best_hand = poker_eval.best_hand(
        game=game, side="hi", hand=hand, include_description=True
    )
    description = best_hand[0]
    cards = best_hand[1:]
    print(
        f"Best hand from {hand} = ({description}) {cards}"
    )  # Expected: {expected_description} {expected_cards}


def test_texas_holdem_best_hand():
    # Straight to Ace (Broadway)
    hand = ["Ah", "4d", "9h", "Kd", "Qc", "Jh", "Ts"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="Straight",
        expected_cards=["Ah", "Kd", "Qc", "Jh", "Ts"],
    )


def test_flush():
    # Flush
    hand = ["Ah", "Kh", "Qh", "Jh", "9h", "2d", "3c"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="Flush",
        expected_cards=["Ah", "Kh", "Qh", "Jh", "9h"],
    )


def test_straight_flush():
    # Straight Flush
    hand = ["8h", "7h", "6h", "5h", "4h", "2d", "3c"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="StFlush",
        expected_cards=["8h", "7h", "6h", "5h", "4h"],
    )


def test_full_house():
    # Full House
    hand = ["Ah", "Ad", "As", "Kh", "Kd", "3c", "2d"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="FlHouse",
        expected_cards=["Ah", "Ad", "As", "Kh", "Kd"],
    )


def test_straight():
    # Straight to Ten
    hand = ["9h", "8d", "7h", "6c", "5s", "3d", "2s"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="Straight",
        expected_cards=["9h", "8d", "7h", "6c", "5s"],
    )


def test_three_of_a_kind():
    # Three of a Kind (Brelan)
    hand = ["Ah", "As", "Ad", "Kh", "5c", "3d", "2s"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="Trips",
        expected_cards=["Ah", "As", "Ad", "Kh", "5c"],
    )


def test_four_of_a_kind():
    # Four of a Kind
    hand = ["Ah", "As", "Ad", "Ac", "2d", "5s", "6c"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="Quads",
        expected_cards=["Ah", "As", "Ad", "Ac", "2d"],
    )


def test_two_pair():
    # Two Pair
    hand = ["Kh", "Kd", "3c", "3s", "9d", "7h", "5c"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="TwoPair",
        expected_cards=["Kh", "Kd", "3c", "3s", "9d"],
    )


def test_one_pair():
    # One Pair
    hand = ["Ah", "Ad", "3c", "5h", "9d", "7h", "2c"]
    test_hand(
        game="holdem",
        hand=hand,
        expected_description="OnePair",
        expected_cards=["Ah", "Ad", "3c", "5h", "9d"],
    )


def test_high_card():
    # High Card
    hand = ["Ah", "Kd", "5h", "9d", "3c", "2s", "Jc"]
    test_hand(
        game="holdem",
        hand=hand,
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
