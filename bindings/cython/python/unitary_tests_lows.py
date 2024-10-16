import logging
from pokereval import PokerEval

# Configurer le logging pour afficher les messages de débogage
logging.basicConfig(level=logging.DEBUG)


def test_lowball27_corrected():
    eval = PokerEval()

    # Exemple de main partiellement remplie avec des placeholders
    pockets = [["7h", "5d"], ["__", "__"], ["__", "__"]]
    board = ["__", "__", "__", "__", "__"]  # Tableau vide avec placeholders

    # Évaluer la main basse pour `lowball27`
    try:
        result = eval.best_hand("lowball27", "low", pockets[0], board)
        print(f"Main basse pour {pockets[0]}: {result}")
    except Exception as e:
        print(f"Erreur lors de l'évaluation (lowball27): {e}")

    # Évaluer une main complète
    filled_pockets = [["7h", "5d"], ["4s", "3c"], ["2h", "2d"]]
    filled_board = ["6s", "5c", "4d", "3s", "2c"]

    try:
        result = eval.best_hand("lowball27", "low", filled_pockets[0], filled_board)
        print(
            f"Main basse pour {filled_pockets[0]} avec le tableau {filled_board}: {result}"
        )
    except Exception as e:
        print(f"Erreur lors de l'évaluation (lowball27): {e}")


def test_low_classic():
    eval = PokerEval()

    # Test 1: 7 Stud No Qualifier - main partiellement remplie avec des placeholders
    pockets = [["7h", "6d"], ["__", "__"], ["__", "__"]]
    board = ["__", "__", "__", "__", "__"]  # Pas de tableau partagé dans le Stud
    try:
        result = eval.best_hand("7stud_no_qualifier", "low_classic", pockets[0], board)
        print(f"Main basse pour {pockets[0]} (7 Stud No Qualifier): {result}")
    except Exception as e:
        print(f"Erreur lors de l'évaluation (7 Stud No Qualifier): {e}")

    # Test 2: 7 Stud No Qualifier - main complète
    filled_pockets = [["7h", "6d"], ["5s", "4c"], ["3h", "2d"]]
    board = []  # Pas de tableau partagé
    try:
        result = eval.best_hand(
            "7stud_no_qualifier", "low_classic", filled_pockets[0], board
        )
        print(f"Main basse pour {filled_pockets[0]} (7 Stud No Qualifier): {result}")
    except Exception as e:
        print(f"Erreur lors de l'évaluation (7 Stud No Qualifier): {e}")

    # Test 3: Razz - main partiellement remplie avec des placeholders
    pockets = [["8h", "7d"], ["__", "__"], ["__", "__"]]
    board = ["__", "__", "__", "__", "__"]  # Pas de tableau partagé dans Razz
    try:
        result = eval.best_hand("razz", "low_classic", pockets[0], board)
        print(f"Main basse pour {pockets[0]} (Razz): {result}")
    except Exception as e:
        print(f"Erreur lors de l'évaluation (Razz): {e}")

    # Test 4: Razz - main complète
    filled_pockets = [["8h", "7d"], ["6s", "5c"], ["4h", "3d"]]
    board = []  # Pas de tableau partagé
    try:
        result = eval.best_hand("razz", "low_classic", filled_pockets[0], board)
        print(f"Main basse pour {filled_pockets[0]} (Razz): {result}")
    except Exception as e:
        print(f"Erreur lors de l'évaluation (Razz): {e}")


def test_low8():
    eval = PokerEval()

    # Test 1: Hold'em8 - main partiellement remplie avec des placeholders
    pockets = [["8h", "7d"], ["__", "__"], ["__", "__"]]
    board = ["__", "__", "__", "__", "__"]  # Tableau partagé dans Hold'em
    try:
        result = eval.best_hand("holdem8", "low8", pockets[0], board)
        print(
            f"Main basse pour {pockets[0]} avec le tableau {board} (Hold'em8): {result}"
        )
    except Exception as e:
        print(f"Erreur lors de l'évaluation (Hold'em8): {e}")

    # Test 2: Hold'em8 - main complète
    filled_pockets = [["8h", "7d"], ["6s", "5c"], ["4h", "3d"]]
    filled_board = ["2s", "5c", "4h", "3d", "8h"]
    try:
        result = eval.best_hand("holdem8", "low8", filled_pockets[0], filled_board)
        print(
            f"Main basse pour {filled_pockets[0]} avec le tableau {filled_board} (Hold'em8): {result}"
        )
    except Exception as e:
        print(f"Erreur lors de l'évaluation (Hold'em8): {e}")

    # Test 3: 7 Stud8 - main partiellement remplie avec des placeholders
    pockets = [["8h", "7d"], ["__", "__"], ["__", "__"]]
    board = ["__", "__", "__", "__", "__"]  # Pas de tableau partagé dans 7 Stud
    try:
        result = eval.best_hand("7stud8", "low8", pockets[0], board)
        print(f"Main basse pour {pockets[0]} (7 Stud8): {result}")
    except Exception as e:
        print(f"Erreur lors de l'évaluation (7 Stud8): {e}")

    # Test 4: 7 Stud8 - main complète
    filled_pockets = [["8h", "7d"], ["6s", "5c"], ["4h", "3d"]]
    board = []  # Pas de tableau partagé
    try:
        result = eval.best_hand("7stud8", "low8", filled_pockets[0], board)
        print(f"Main basse pour {filled_pockets[0]} (7 Stud8): {result}")
    except Exception as e:
        print(f"Erreur lors de l'évaluation (7 Stud8): {e}")


def main():
    print("=== Test Deuce-to-Seven Lowball (lowball27) ===")
    test_lowball27_corrected()
    print("\n=== Test Low Classic (7 Stud No Qualifier & Razz) ===")
    test_low_classic()
    print("\n=== Test Low8 (Hold'em8 & 7 Stud8) ===")
    test_low8()


if __name__ == "__main__":
    main()
