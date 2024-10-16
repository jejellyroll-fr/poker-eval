from pokereval import PokerEval


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
        print(f"Erreur lors de l'évaluation: {e}")

    # Évaluer une main complète
    filled_pockets = [["7h", "5d"], ["4s", "3c"], ["2h", "2d"]]
    filled_board = ["6s", "5c", "4d", "3s", "2c"]

    try:
        result = eval.best_hand("lowball27", "low", filled_pockets[0], filled_board)
        print(
            f"Main basse pour {filled_pockets[0]} avec le tableau {filled_board}: {result}"
        )
    except Exception as e:
        print(f"Erreur lors de l'évaluation: {e}")


if __name__ == "__main__":
    test_lowball27_corrected()
