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


# Test 5 : Tester le remplissage de pocket pour Monte Carlo
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
        ["As", "Ac"],  # Deux cartes pour Hold'em
        ["Ks", "Qs"],  # Deux cartes pour Hold'em
    ]
    board = ["Js", "Ts", "9s", "5c", "3h"]  # Le tableau de 5 cartes
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


# Test 10 : Tester le remplissage de pocket pour Monte Carlo avec Placeholders
def test_empty_pockets():
    pockets = [["__", "__"], ["__", "__"]]  # pocket avec des placeholders
    filled_pockets = poker_eval.fill_pockets(pockets)
    print(f"Test 10 - Filled pockets with placeholders: {filled_pockets}")
    # Vérifier que chaque poche a bien été remplie de deux cartes
    assert all(
        len(pocket) == 2 for pocket in filled_pockets
    ), "Erreur, les pocket doivent contenir deux cartes"
    # Vérifier que les placeholders ont été remplacés
    for pocket in filled_pockets:
        for card in pocket:
            assert (
                card != "__" and card != 255
            ), "Toutes les cartes doivent être remplies avec des valeurs valides"
            assert isinstance(
                card, str
            ), "Les cartes remplies doivent être des chaînes de caractères valides"


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

    # Vérification des résultats de la main haute uniquement
    assert (
        results["eval"][0]["losehi"] == 1000
    ), "Erreur, la première main devrait perdre 1000 fois"
    assert (
        results["eval"][1]["winhi"] == 1000
    ), "Erreur, la deuxième main devrait gagner 1000 fois"


# Test 12: Test Omaha hand evaluation
def test_omaha():
    pockets = [["As", "Kd", "Qc", "Jh"], ["Qs", "Jh", "9h", "8d"]]  # Two Omaha hands
    board = ["Ts", "9d", "8c", "2h", "3c"]
    game = "omaha"
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)
    print(f"Test 12 - Omaha results: {results}")

    # Vérifier que les mains sont évaluées correctement, il peut y avoir égalité
    assert (
        results["eval"][0]["tiehi"] == 1 or results["eval"][0]["winhi"] == 1
    ), "Erreur dans l'évaluation de Omaha (la première main doit gagner ou être à égalité)"
    assert (
        results["eval"][1]["tiehi"] == 1 or results["eval"][1]["winhi"] == 1
    ), "Erreur dans l'évaluation de Omaha (la deuxième main doit gagner ou être à égalité)"


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


# Test 14 : Vérifier l'évaluation pour une main basse (low)
def test_razz():
    # Chaque poche doit contenir exactement 7 cartes pour le jeu 'razz'
    pockets = [
        ["As", "2d", "3c", "4h", "5s", "6d", "7c"],  # Main du joueur 0
        ["5s", "6h", "7d", "8c", "9h", "Ts", "Jc"],  # Main du joueur 1
    ]
    board = []  # Pas de tableau partagé dans 'razz'
    game = "razz"
    iterations = 0  # Évaluation exhaustive pour des mains entièrement connues

    try:
        results = poker_eval.poker_eval(
            game=game, pockets=pockets, board=board, iterations=iterations
        )
        print(f"Test 14 - Razz results: {results}")

        # Vérifier que le joueur 0 a gagné le low et que le joueur 1 a perdu
        assert (
            results["eval"][0]["winlo"] == 1
        ), "Erreur, le joueur 0 aurait dû gagner le 'low'"
        assert (
            results["eval"][1]["loselo"] == 1
        ), "Erreur, le joueur 1 aurait dû perdre le 'low'"

    except ValueError as e:
        print(f"Test 14 - Razz - Exception capturée: {e}")
        assert False, f"Erreur dans l'évaluation de Razz: {e}"


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
        results["eval"][1]["winhi"] == 1 or results["eval"][1]["tiehi"] == 1
    ), "Erreur dans l'évaluation de Omaha Hi/Lo (le joueur 1 devrait gagner ou être à égalité pour la main haute)"

    # Vérifier que le joueur 0 gagne la main basse
    assert (
        results["eval"][0]["winlo"] == 1 or results["eval"][0]["tielo"] == 1
    ), "Erreur dans l'évaluation de Omaha Hi/Lo (le joueur 0 devrait gagner ou être à égalité pour la main basse)"


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

    # Vérification si le joueur 1 est soit à égalité (tiehi) soit gagnant (winhi)
    assert (
        results["eval"][0]["tiehi"] == 1 or results["eval"][0]["winhi"] == 1
    ), "Erreur dans l'évaluation de 7-Card Stud Hi/Lo (pocket 1 devrait gagner ou être à égalité pour la main haute)"

    # Le joueur 2 doit soit être perdant (losehi) soit à égalité (tiehi) pour la main haute
    assert (
        results["eval"][1]["losehi"] == 1 or results["eval"][1]["tiehi"] == 1
    ), "Erreur dans l'évaluation de 7-Card Stud Hi/Lo (pocket 2 devrait perdre ou être à égalité pour la main haute)"

    # Vérifier que le joueur 0 gagne la main basse
    assert (
        results["eval"][0]["winlo"] == 1
    ), "Erreur dans l'évaluation de 7-Card Stud Hi/Lo (pocket 1 devrait gagner la main basse)"

    # Vérifier que le joueur 1 perd la main basse
    assert (
        results["eval"][1]["loselo"] == 1
    ), "Erreur dans l'évaluation de 7-Card Stud Hi/Lo (pocket 2 devrait perdre la main basse)"


# Test 17: Test Lowball hand evaluation
def test_lowball():
    hand1 = ["As", "2d", "3c", "4h", "5s"]  # Main 1 avec 5 cartes valides
    hand2 = ["Kd", "Qd", "Jd", "Td", "9d"]  # Main 2 avec 5 cartes valides
    board = []  # Lowball n'utilise pas de tableau partagé

    # Utilisation explicite du jeu 'ace_to_five_lowball', une variante courante de Lowball
    low_hand_value1 = poker_eval.best_hand_value(
        "ace_to_five_lowball", "low", hand1, board
    )
    low_hand_value2 = poker_eval.best_hand_value(
        "ace_to_five_lowball", "low", hand2, board
    )

    print(f"Test 17 - Lowball hand 1 value (As, 2d, 3c, 4h, 5s): {low_hand_value1}")
    print(f"Test 17 - Lowball hand 2 value (Kd, Qd, Jd, Td, 9d): {low_hand_value2}")

    # Vérifier que les deux mains ont été évaluées correctement
    assert (
        low_hand_value1 > 0
    ), "Erreur dans l'évaluation de la première main basse (Lowball)"
    assert (
        low_hand_value2 > 0
    ), "Erreur dans l'évaluation de la deuxième main basse (Lowball)"
    assert isinstance(
        low_hand_value1, int
    ), "La valeur de la main basse doit être un entier"
    assert isinstance(
        low_hand_value2, int
    ), "La valeur de la main basse doit être un entier"


# Test 18 : Évaluation avec une Carte Manquante dans PokerEval
def test_empty_hand():
    pockets = [["__", "__"]]  # Poche avec des placeholders
    board = ["As", "Ks", "Qs", "Js", "Ts"]
    game = "holdem"
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)

    print(f"Test 18 - Empty hand evaluation: {results}")

    # Vérifiez que la main vide est bien considérée comme perdante
    assert results["eval"][0]["losehi"] == 1, "Erreur dans l'évaluation de la main vide"


# Test 19 : Évaluation de Mains Complexes dans Différentes Variantes
def test_complex_hands():
    """Test l'évaluation de mains complexes dans différentes variantes de poker."""
    # Texas Hold'em avec une quinte flush
    pockets = [["As", "Ks"], ["Qs", "Js"]]
    board = ["Ts", "9s", "8s", "7s", "2h"]
    game = "holdem"
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)
    print(f"Test 19a - Texas Hold'em quinte flush: {results}")
    assert results["eval"][0]["losehi"] == 1, "Erreur : La première main devrait perdre"
    assert results["eval"][1]["winhi"] == 1, "Erreur : La deuxième main devrait gagner"

    # Razz avec une main basse parfaite
    pockets = [
        ["2d", "3c", "4h", "5s", "6d", "7c", "8h"],  # Main du joueur 0
        ["5s", "6h", "7d", "8c", "9h", "Ts", "Jc"],  # Main du joueur 1
    ]
    board = []  # Pas de tableau partagé dans 'razz'
    game = "razz"
    results = poker_eval.poker_eval(
        game=game, pockets=pockets, board=board, iterations=0
    )
    print(f"Test 19b - Razz main basse parfaite: {results}")
    assert (
        results["eval"][0]["winlo"] == 1
    ), "Erreur : La première main devrait gagner le low"
    assert (
        results["eval"][1]["loselo"] == 1
    ), "Erreur : La deuxième main devrait perdre le low"

    # Omaha avec full house et low
    pockets = [
        ["As", "2d", "3c", "4h"],
        ["5s", "6d", "7c", "8h"],
    ]  # Deux mains de Omaha avec 4 cartes
    board = ["2h", "2c", "3d", "3h", "4s"]  # Tableau de 5 cartes pour Omaha
    game = "omaha"  # Jeu Omaha
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)

    print(f"Test 19c - Omaha full house et low: {results}")

    # Vérification des résultats
    assert (
        results["eval"][0]["winhi"] == 1
    ), "Erreur : La première main devrait gagner le high avec un full house"

    assert (
        results["eval"][1]["losehi"] == 1
    ), "Erreur : La deuxième main devrait perdre le high"

    # Lowball avec une main sans qualification
    # Chaque poche doit contenir exactement 5 cartes pour le jeu lowball
    pockets = [
        ["As", "Kd", "Qc", "Jh", "9c"],
        ["Qc", "Jh", "9h", "8d", "7c"],
    ]  # Deux mains avec 5 cartes
    board = []  # Pas de tableau partagé dans lowball
    game = "lowball"  # Jeu Lowball
    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)

    print(f"Test 19d - Lowball main sans qualification: {results}")

    # Vérification des résultats : Ni l'une ni l'autre ne devraient gagner le low
    assert (
        results["eval"][0]["winlo"] == 0
    ), "Erreur : La première main ne devrait pas gagner le low"
    assert (
        results["eval"][1]["winlo"] == 0
    ), "Erreur : La deuxième main ne devrait pas gagner le low"


# Test 20 : Évaluation avec des Cartes Invalides et Gestion des Erreurs
def test_invalid_cards():
    """Test la gestion des cartes invalides et des erreurs."""

    # Carte invalide dans la poche
    try:
        poker_eval.evaln(["As", "Kd", "Qc", "Jh", "InvalidCard"])
    except ValueError as e:
        print(f"Test 20a - Carte invalide capturée: {e}")
    else:
        assert False, "Erreur : Une carte invalide n'a pas été détectée"

    # Poche avec plus de cartes que permises (devrait lever une exception maintenant)
    try:
        poker_eval.eval_hand(
            "holdem", "hi", ["As", "Kd", "Qc", "Jh", "Ts"], ["2d", "3h"]
        )
    except ValueError as e:
        print(f"Test 20b - Poche avec trop de cartes capturée: {e}")
    else:
        assert False, "Erreur : Une poche avec trop de cartes n'a pas été détectée"

    # Jeu non supporté
    try:
        poker_eval.best_hand(
            "unsupported_game", "hi", ["As", "Kd"], ["Qc", "Jh", "Ts", "3d", "7h"]
        )
    except ValueError as e:
        print(f"Test 20c - Jeu non supporté capturé: {e}")
    else:
        assert False, "Erreur : Un jeu non supporté n'a pas été détecté"


# Test 21 : Évaluation avec des Placeholders Multiples
def test_multiple_placeholders():
    """Test l'évaluation des mains avec plusieurs placeholders."""
    pockets = [["As", "__"], ["Kd", "__"], ["Qc", "__"]]
    board = ["Jh", "__", "__", "__", "__"]
    game = "holdem"
    iterations = 1000
    results = poker_eval.poker_eval(
        game=game, pockets=pockets, board=board, iterations=iterations
    )
    print(f"Test 21 - Évaluation avec multiples placeholders: {results}")
    assert results["info"][0] == iterations, "Erreur : Nombre d'itérations incorrect"
    assert results["info"][1] == 0, "Erreur : Le jeu n'a pas de low pot"
    assert results["info"][2] == 1, "Erreur : Le jeu a un high pot"


# Test 22 : Vérification de l'Unicité des Cartes Remplies
def test_no_duplicate_cards_filled_pockets():
    """Test to ensure no duplicate cards are distributed when filling pockets."""
    board = ["As", "Kd", "Qc"]
    pockets = [["Jh", "__"], ["Ts", "__"], ["9c", "__"]]

    filled_pockets = poker_eval.fill_pockets(pockets)
    all_distributed = set(board)

    for pocket in filled_pockets:
        for card in pocket:
            assert card not in all_distributed, f"Carte dupliquée trouvée: {card}"
            all_distributed.add(card)

    print("Test 22 - No duplicate cards in filled pockets: Passed successfully.")


# Test 23 : Vérification des Doublons dans les Simulations Monte Carlo
def test_no_duplicate_cards_monte_carlo():
    """Test pour vérifier qu'aucune carte dupliquée n'est distribuée lors des simulations Monte Carlo."""
    pockets = [["As", "__"], ["Kd", "__"], ["Qc", "__"]]
    board = ["Jh", "__", "__", "__", "__"]
    game = "holdem"
    iterations = 100

    for i in range(iterations):
        # Effectuer une simulation avec return_distributed=True pour obtenir les cartes distribuées
        results = poker_eval.poker_eval(
            game=game,
            pockets=pockets,
            board=board,
            iterations=1,
            return_distributed=True,
        )

        # Initialiser distributed_cards avec les cartes connues du tableau
        distributed_cards = set()
        for card in board:
            if card != "__" and card != 255:
                distributed_cards.add(card)

        # Vérifier qu'il n'y a pas de cartes dupliquées dans les pocket distribuées
        for pocket in results["distributed_cards"]:
            for card in pocket:
                assert (
                    card not in distributed_cards
                ), f"Carte dupliquée trouvée dans la simulation {i} : {card}"
                distributed_cards.add(card)

    print(
        "Test 23 - Aucune carte dupliquée dans les simulations Monte Carlo : Réussi avec succès."
    )


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
    test_complex_hands()
    test_invalid_cards()
    test_multiple_placeholders()
    test_no_duplicate_cards_filled_pockets()
    test_no_duplicate_cards_monte_carlo()
