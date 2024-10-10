import pokereval

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()


# Test 1 : Vérifier l'initialisation Monte Carlo avec une graine aléatoire unique
def test_montecarlo_seed_initialization():
    # Initialisation de deux instances pour vérifier l'initialisation aléatoire
    eval1 = pokereval.PokerEval()
    eval2 = pokereval.PokerEval()

    # Vérifier que les deux instances n'ont pas le même état initial pour Monte Carlo
    pockets = [["As", "__"], ["Kd", "__"]]
    board = ["Ts", "__", "__", "__", "__"]

    # Simuler quelques tours pour chaque instance
    results1 = eval1.poker_eval("holdem", pockets, board, iterations=100)
    results2 = eval2.poker_eval("holdem", pockets, board, iterations=100)

    print(f"Test 1 - Monte Carlo Seed Initialization - Results 1: {results1}")
    print(f"Test 1 - Monte Carlo Seed Initialization - Results 2: {results2}")

    # Les résultats devraient différer en raison de l'initialisation aléatoire
    assert (
        results1 != results2
    ), "Erreur : Les simulations Monte Carlo devraient être différentes avec des seeds différentes"


# Test 2 : Évaluer une main dans un jeu manquant (Lowball)
def test_lowball_hand_evaluation():
    hand = ["As", "2d", "3c", "4h", "5s"]
    board = []
    lowball_hand_value = poker_eval.best_hand_value("lowball", "low", hand, board)

    print(f"Test 2 - Lowball hand value (As, 2d, 3c, 4h, 5s): {lowball_hand_value}")
    assert (
        lowball_hand_value > 0
    ), "Erreur : L'évaluation de la main Lowball n'est pas correcte"
    assert isinstance(
        lowball_hand_value, int
    ), "La valeur de la main Lowball doit être un entier"


# Test 3 : Évaluer une main Omaha Hi/Lo avec Monte Carlo
def test_omaha8_montecarlo():
    pockets = [["As", "2d", "3c", "4h"], ["Ks", "Qs", "Jh", "Td"]]
    board = ["5s", "6d", "7h", "__", "__"]  # Tableau partiellement rempli
    game = "omaha8"

    # Simuler 1000 itérations avec Monte Carlo
    results = poker_eval.poker_eval(
        game=game, pockets=pockets, board=board, iterations=1000
    )
    print(f"Test 3 - Omaha Hi/Lo Monte Carlo results: {results}")

    # Vérifier les résultats Monte Carlo pour le pot High et Low
    assert (
        results["eval"][0]["ev"] > 0
    ), "Erreur : L'évaluation Monte Carlo pour le joueur 0 devrait être positive"
    assert (
        results["eval"][1]["ev"] > 0
    ), "Erreur : L'évaluation Monte Carlo pour le joueur 1 devrait être positive"


# Test 4 : Vérifier l'évaluation exhaustive pour 7-Card Stud
def test_7stud_hand_evaluation():
    pockets = [
        ["As", "2d", "3c", "4h", "5s", "6d", "7c"],
        ["Ks", "Qs", "Jh", "Td", "9d", "8h", "7s"],
    ]
    board = []
    game = "7stud"

    results = poker_eval.poker_eval(game=game, pockets=pockets, board=board)
    print(f"Test 4 - 7-Card Stud hand evaluation results: {results}")

    # Vérifier que la main est correctement évaluée pour 'hi'
    assert (
        results["eval"][0]["winhi"] == 1
    ), "Erreur : Le joueur 0 devrait gagner dans 7-Card Stud"
    assert (
        results["eval"][1]["losehi"] == 1
    ), "Erreur : Le joueur 1 devrait perdre dans 7-Card Stud"


# Test 5 : Évaluer une main Ace-to-Five Lowball
def test_ace_to_five_lowball():
    hand = ["As", "2d", "3c", "4h", "5s"]  # Lowball parfait Ace-to-Five
    board = []
    game = "ace_to_five_lowball"

    low_hand_value = poker_eval.best_hand_value(game, "low", hand, board)
    print(
        f"Test 5 - Ace-to-Five Lowball hand value (As, 2d, 3c, 4h, 5s): {low_hand_value}"
    )

    assert (
        low_hand_value > 0
    ), "Erreur : L'évaluation Ace-to-Five Lowball est incorrecte"
    assert isinstance(
        low_hand_value, int
    ), "La valeur de la main Lowball doit être un entier"


# Test 6 : Vérifier la simulation Monte Carlo sur plusieurs variantes (Hold'em, Omaha, etc.)
def test_multi_variant_montecarlo():
    variants = ["holdem", "omaha", "7stud", "razz", "ace_to_five_lowball"]
    for variant in variants:
        pockets = [["As", "__"], ["Kd", "__"]]
        board = ["Ts", "__", "__", "__", "__"]
        results = poker_eval.poker_eval(
            game=variant, pockets=pockets, board=board, iterations=1000
        )

        print(f"Test 6 - Monte Carlo simulation for {variant} - Results: {results}")

        assert (
            results["eval"][0]["ev"] > 0
        ), f"Erreur : Le joueur 0 devrait avoir un EV positif pour {variant}"
        assert (
            results["eval"][1]["ev"] > 0
        ), f"Erreur : Le joueur 1 devrait avoir un EV positif pour {variant}"


# Test 7 : Vérifier la détection des duplicatas dans une simulation Monte Carlo
def test_no_duplicate_cards_montecarlo():
    pockets = [["As", "__"], ["Kd", "__"], ["Qc", "__"]]
    board = ["Jh", "__", "__", "__", "__"]
    game = "holdem"

    for _ in range(100):
        results = poker_eval.poker_eval(
            game=game,
            pockets=pockets,
            board=board,
            iterations=1,
            return_distributed=True,
        )

        distributed_cards = set(board)
        for pocket in results["distributed_cards"]:
            for card in pocket:
                assert (
                    card not in distributed_cards
                ), f"Erreur : Carte dupliquée trouvée : {card}"
                distributed_cards.add(card)

    print("Test 7 - No duplicate cards in Monte Carlo: Passed successfully.")


# Test 8 : Vérifier la gestion des exceptions dans les jeux manquants
def test_unsupported_game():
    try:
        poker_eval.best_hand(
            "unsupported_game", "hi", ["As", "Kd"], ["Qc", "Jh", "Ts", "3d", "7h"]
        )
    except ValueError as e:
        print(f"Test 8 - Unsupported game error caught: {e}")
    else:
        assert False, "Erreur : Un jeu non supporté n'a pas été détecté"


# Exécuter les tests
if __name__ == "__main__":
    test_montecarlo_seed_initialization()
    test_lowball_hand_evaluation()
    test_omaha8_montecarlo()
    test_7stud_hand_evaluation()
    test_ace_to_five_lowball()
    test_multi_variant_montecarlo()
    test_no_duplicate_cards_montecarlo()
    test_unsupported_game()
