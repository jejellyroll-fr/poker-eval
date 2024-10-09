import pokereval

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()


# Test 1 : Évaluer une main donnée
def test_evaln():
    hand_value = poker_eval.evaln(["As", "Kd", "Qc", "Jh", "Ts"])  # Quinte à l'as
    print(f"Test 1 - Hand value (As, Kd, Qc, Jh, Ts): {hand_value}")
    assert hand_value > 0, "Erreur dans l'évaluation de la main"
    assert isinstance(hand_value, int), "La valeur de la main doit être un entier"


# Test 2 : Convertir une chaîne de cartes en entiers et retour
def test_string2card_and_card2string():
    cards = ["As", "Kd", "Qc", "Jh", "Ts"]
    card_numbers = poker_eval.string2card(cards)
    converted_back = poker_eval.card2string(card_numbers)
    print(f"Test 2 - String to card and back: {converted_back}")
    assert cards == converted_back, "Erreur dans la conversion carte-chaine"
    assert all(
        isinstance(num, int) for num in card_numbers
    ), "Tous les numéros de cartes doivent être des entiers"


# Test 3 : Retourner la meilleure main de cinq cartes avec un tableau
def test_best_hand():
    hand = ["As", "Kd"]
    board = ["Qc", "Jh", "Ts", "3d", "7h"]  # Tableau avec Quinte à l'as
    best_hand = poker_eval.best_hand("holdem", "hi", hand, board)
    print(f"Test 3 - Best hand (As, Kd with board): {best_hand}")
    assert best_hand is not False, "Erreur, aucune main n'a été retournée"
    assert len(best_hand) == 5, "Erreur, la meilleure main ne contient pas 5 cartes"
    assert (
        "As" in best_hand
    ), f"La meilleure main devrait inclure l'As, mais elle contient: {best_hand}"


# Test 4 : Tester `best_hand_value` pour retourner seulement la valeur numérique de la main
def test_best_hand_value():
    hand = ["As", "Kd"]
    board = ["Qc", "Jh", "Ts", "3d", "7h"]  # Tableau avec Quinte à l'as
    best_hand_value = poker_eval.best_hand_value("holdem", "hi", hand, board)
    print(f"Test 4 - Best hand value (As, Kd with board): {best_hand_value}")
    assert best_hand_value > 0, "Erreur dans la valeur de la meilleure main"
    assert isinstance(best_hand_value, int), "La valeur de la main doit être un entier"


# Test 5 : Tester le remplissage de poches pour Monte Carlo
def test_fill_pockets():
    pockets = [["As", "__"], ["Kd", "Qc"]]  # "__" représente une carte manquante
    filled_pockets = poker_eval.fill_pockets(pockets)
    print(f"Test 5 - Filled pockets: {filled_pockets}")
    for pocket in filled_pockets:
        for card in pocket:
            assert isinstance(
                card, str
            ), "La carte manquante doit être remplie par une chaîne valide"
            assert card != "__", "Toutes les cartes doivent être remplies"


# Test 6 : Déterminer les gagnants entre plusieurs mains
def test_winners():
    hands = [
        ["As", "Ac", "Ad", "Ah", "2s"],  # Carré d'As
        ["Ks", "Qs", "Js", "Ts", "9s"],  # Quinte flush
    ]
    board = ["5c", "8c", "7d", "6h", "3h"]
    winners = poker_eval.winners(
        "holdem", hands, board
    )  # Ajout de 'holdem' comme type de jeu
    print(f"Test 6 - Winning hand(s): {winners}")
    assert (
        "hi" in winners and len(winners["hi"]) > 0
    ), "Erreur dans la détermination des gagnants"
    assert winners["hi"] == [1], "La quinte flush devrait être la main gagnante"


# Test 7 : Obtenir toutes les cartes du deck
def test_deck():
    deck = poker_eval.deck()
    print(f"Test 7 - Full deck: {deck}")
    assert len(deck) == 52, "Le deck doit contenir 52 cartes"
    assert "As" in deck, "L'As de pique doit être dans le deck"
    assert "Kd" in deck, "Le Roi de carreau doit être dans le deck"
    assert isinstance(deck, list), "Le deck doit être une liste de cartes"


# Test 8 : Tester la carte joker / placeholder
def test_nocard():
    nocard = poker_eval.nocard()
    print(f"Test 8 - Placeholder card: {nocard}")
    assert nocard == 255, "Le joker ou placeholder doit être 255"
    assert isinstance(nocard, int), "La carte joker doit être un entier"


# Test 9 : Vérifier l'évaluation pour une main basse (low)
def test_eval_low_hand():
    hand = ["As", "2d", "3c", "4h", "5s"]  # Main basse typique
    board = []
    low_hand_value = poker_eval.best_hand_value(
        "razz", "low", hand, board
    )  # Utiliser un jeu 'low' comme Razz ou Omaha8
    print(f"Test 9 - Low hand value (As, 2d, 3c, 4h, 5s): {low_hand_value}")
    assert low_hand_value > 0, "Erreur dans l'évaluation de la main basse"
    assert isinstance(
        low_hand_value, int
    ), "La valeur de la main basse doit être un entier"


# Test 10 : Tester des poches vides
def test_empty_pockets():
    pockets = [[], []]  # Poches vides
    filled_pockets = poker_eval.fill_pockets(pockets)
    print(f"Test 10 - Filled pockets with empty input: {filled_pockets}")
    # Vérifier que chaque poche a bien été remplie de deux cartes
    assert all(
        len(pocket) == 2 for pocket in filled_pockets
    ), "Erreur, les poches doivent contenir deux cartes"


# Test 11: Test Texas Hold'em hand evaluation avec EV
def test_holdem_ev():
    pockets = [["As", "Kd"], ["Qs", "Jh"]]  # Deux mains de joueurs
    board = ["Ts", "9d", "8c", "2h", "3c"]  # Le tableau
    game = "holdem"
    results = poker_eval.poker_eval(
        game=game, pockets=pockets, board=board, iterations=1000
    )  # Simuler avec 1000 itérations

    print(f"Test 11 - Hold'em results avec EV: {results}")

    for result in results["eval"]:
        assert "ev" in result, "Erreur dans le calcul de l'EV, pas de clé 'ev'."
        assert isinstance(result["ev"], int), "L'EV doit être un entier."


# Test 12: Test Omaha hand evaluation
def test_omaha():
    pockets = [["As", "Kd", "Qc", "Jh"], ["Qs", "Jh", "9h", "8d"]]  # Two Omaha hands
    board = ["Ts", "9d", "8c", "2h", "3c"]
    game = "omaha"
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)
    print(f"Test 12 - Omaha results: {results}")
    assert (
        results["eval"][0]["winhi"] == 1
    ), "Erreur dans l'évaluation de Omaha (pocket 1 devrait gagner)"


# Test 13: Test 7-Card Stud hand evaluation
def test_7stud():
    pockets = [
        ["As", "Kd", "Qc", "Jh", "Ts", "9h", "8d"],
        ["Qs", "Jh", "9h", "8d", "7c", "6h", "5d"],
    ]
    board = []  # In 7-Card Stud, the board is not shared
    game = "7stud"
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)
    print(f"Test 13 - 7-Card Stud results: {results}")
    assert (
        results["eval"][0]["winhi"] == 1
    ), "Erreur dans l'évaluation de 7-Card Stud (pocket 1 devrait gagner)"


# Test 14: Test Razz (lowball) hand evaluation
def test_razz():
    pockets = [["As", "2d", "3c"], ["5s", "6h", "7d"]]
    board = ["4c", "8h", "9s", "Ts", "Jh"]
    game = "razz"
    iterations = 0  # Évaluation exhaustive pour des mains entièrement connues
    results = poker_eval.poker_eval(
        game=game, pockets=pockets, board=board, iterations=iterations
    )

    print(f"Test 14 - Razz results: {results}")

    # Vérifier que le joueur 0 a gagné le low et que le joueur 1 a perdu
    assert (
        results["eval"][0]["winlo"] == 1
    ), "Erreur, le joueur 0 aurait dû gagner le 'low'"
    assert (
        results["eval"][1]["winlo"] == 0
    ), "Erreur, le joueur 1 ne devrait pas gagner le 'low'"


# Test 15: Test Omaha Hi/Lo evaluation
def test_omaha8():
    pockets = [["As", "2d", "3c", "4h"], ["Qs", "Jh", "9h", "8d"]]
    board = ["5s", "6h", "7d", "8c", "9c"]
    game = "omaha8"  # Omaha Hi/Lo
    iterations = 0  # Évaluation exhaustive pour des mains connues
    results = poker_eval.poker_eval(
        game=game, pockets=pockets, board=board, iterations=iterations
    )
    print(f"Test 15 - Omaha Hi/Lo results: {results}")

    # Vérifier que le joueur 1 gagne la main haute
    assert (
        results["eval"][1]["winhi"] == 1
    ), "Erreur dans l'évaluation de Omaha Hi/Lo (le joueur 1 devrait gagner la main haute)"
    # Vérifier que le joueur 0 gagne la main basse
    assert (
        results["eval"][0]["winlo"] == 1
    ), "Erreur dans l'évaluation de Omaha Hi/Lo (le joueur 0 devrait gagner la main basse)"


# Test 16: Test 7-Card Stud Hi/Lo evaluation
def test_7stud8():
    pockets = [
        ["Ah", "3h", "4h", "5h", "6h", "7h", "8h"],
        ["2c", "3c", "4c", "5c", "6c", "7c", "8c"],
    ]
    board = []  # Pas de tableau dans le 7-Card Stud
    game = "7stud8"
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)

    print(f"Test 16 - 7-Card Stud Hi/Lo results: {results}")

    assert (
        results["eval"][0]["winhi"] == 1
    ), "Erreur dans l'évaluation de 7-Card Stud Hi/Lo (pocket 1 devrait gagner)"
    assert (
        results["eval"][1]["losehi"] == 1
    ), "Erreur dans l'évaluation de 7-Card Stud Hi/Lo (pocket 2 devrait perdre)"


# Test 17: Test Lowball hand evaluation
def test_lowball():
    hand = ["As", "2d", "3c", "4h", "5s"]
    board = []

    # Utilisation explicite du jeu 'Ace-to-Five Lowball', une variante courante de Lowball
    low_hand_value = poker_eval.best_hand_value(
        "ace_to_five_lowball", "low", hand, board
    )

    print(f"Test 17 - Lowball hand value (As, 2d, 3c, 4h, 5s): {low_hand_value}")

    # Vérifiez que la main basse a été évaluée correctement
    assert low_hand_value > 0, "Erreur dans l'évaluation de la main basse (Lowball)"


# Test 18: Test edge case with an empty hand in poker_eval
def test_empty_hand():
    pockets = [["__", "__"]]  # Empty hand
    board = ["As", "Ks", "Qs", "Js", "Ts"]
    game = "holdem"
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)

    print(f"Test 18 - Empty hand evaluation: {results}")

    # Vérifiez que la main vide est bien considérée comme perdante
    assert results["eval"][0]["losehi"] == 1, "Erreur dans l'évaluation de la main vide"


# Exécuter les tests
if __name__ == "__main__":
    test_evaln()
    test_string2card_and_card2string()
    test_best_hand()
    test_best_hand_value()
    test_fill_pockets()
    test_winners()
    test_deck()
    test_nocard()
    test_eval_low_hand()
    test_empty_pockets()
    test_holdem_ev()
    test_omaha()
    test_7stud()
    test_razz()
    test_omaha8()
    test_7stud8()
    test_lowball()
    test_empty_hand()
