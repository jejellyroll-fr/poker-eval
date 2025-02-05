import sys
import pokereval

# Créer une instance de la classe PokerEval
poker_eval = pokereval.PokerEval()

# Nombre d'itérations pour les simulations de Monte Carlo
iterations_low = 100000
iterations_high = 200000

# Test 1: Vérifier que la valeur retournée pour une main haute spécifique est correcte
# La main est composée de As, As, As, Kh, Ks.
# Cette main devrait être évaluée avec une valeur spécifique pour une main "hi" (high hand).
if (
    poker_eval.best_hand_value("holdem", "hi", ["Ah", "Ad", "As", "Kh", "Ks"])
    != 101494784
):
    sys.exit(1)

# Test 2: Vérifier la conversion d'une chaîne représentant une carte en entier numérique
# '2h' (deux de cœur) doit être converti en 0, selon le mapping standard des cartes.
if poker_eval.string2card("2h") != 0:
    sys.exit(1)

# Test 3: Tester l'évaluation des gagnants pour le jeu de poker Stud à 7 cartes
# Le but est d'identifier le gagnant parmi plusieurs mains fournies en mode "7stud".
pockets = [
    ["As", "Ad", "Ac", "Tc", "Ts", "2d", "5c"],
    ["Js", "Jc", "7s", "8c", "8d", "3c", "3h"],
    [255, 255],
]  # 255 représente des cartes inconnues
print(
    "stud7 (1) result = %s\n"
    % poker_eval.winners(game="7stud", pockets=pockets, dead=[], board=[])
)

# Test 4: Tester l'évaluation d'une autre configuration de Stud à 7 cartes
# Objectif : Identifier les gagnants parmi des poches contenant des cartes numériques et inconnues (255).
pockets = [[22, 18, 21, 3, 41, 1, 30], [39, 255, 255, 15, 13, 17, 255]]
print(
    "stud7 (2) result = %s\n"
    % poker_eval.winners(game="7stud", pockets=pockets, dead=[], board=[])
)

# Test 5: Vérifier la conversion complète du deck de cartes en valeurs numériques
# L'objectif est de s'assurer que chaque carte du jeu est correctement mappée à un entier.
print(
    [
        f"{j}{i}/%d" % poker_eval.string2card(f"{j}{i}")
        for i in "hdcs"
        for j in "23456789TJQKA"
    ]
)
print("deck = %s\n" % poker_eval.deck())

# Test 6: Evaluation des mains pour le jeu de poker Texas Hold'em
# Ce test évalue et retourne les résultats pour un tableau donné et plusieurs mains de joueur.
print(
    "result = %s\n"
    % poker_eval.poker_eval(
        game="holdem",
        pockets=[["tc", "ac"], ["3h", "ah"], ["8c", "6h"]],
        dead=[],
        board=["7h", "3s", "2c"],
    )
)

# Test 7: Vérifier l'identification des gagnants pour un jeu de Texas Hold'em
# Identifier quel joueur possède la meilleure main sur un tableau donné.
print(
    "winners = %s\n"
    % poker_eval.winners(
        game="holdem",
        pockets=[["tc", "ac"], ["3h", "ah"], ["8c", "6h"]],
        dead=[],
        board=["7h", "3s", "2c"],
    )
)

# Test 8: Evaluation d'une main complète après toutes les cartes du tableau sont distribuées
# Ce test inclut un tableau de 5 cartes.
print(
    "result = %s\n"
    % poker_eval.poker_eval(
        game="holdem",
        pockets=[["tc", "ac"], ["th", "ah"], ["8c", "6h"]],
        dead=[],
        board=["7h", "3s", "2c", "7s", "7d"],
    )
)
print(
    "winners = %s\n"
    % poker_eval.winners(
        game="holdem",
        pockets=[["tc", "ac"], ["th", "ah"], ["8c", "6h"]],
        dead=[],
        board=["7h", "3s", "2c", "7s", "7d"],
    )
)

# Test 9: Évaluer une main pour un jeu de Omaha
# Ce test vérifie si les gagnants sont correctement identifiés pour une configuration Omaha.
print(
    "winners omaha = %s\n"
    % poker_eval.winners(
        game="omaha",
        pockets=[
            ["tc", "ac", "ks", "kc"],
            ["th", "ah", "qs", "qc"],
            ["8c", "6h", "js", "jc"],
        ],
        dead=[],
        board=["7h", "3s", "2c", "7s", "7d"],
    )
)

# Test 10: Évaluer une main pour un jeu de Omaha Hi/Lo
# Ce test vérifie si les gagnants pour la partie haute et basse sont bien identifiés.
print(
    "winners omaha8 = %s\n"
    % poker_eval.winners(
        game="omaha8",
        pockets=[
            ["tc", "ac", "ks", "kc"],
            ["th", "ah", "qs", "qc"],
            ["8c", "6h", "js", "jc"],
        ],
        dead=[],
        board=["7h", "3s", "2c", "7s", "7d"],
    )
)


# Test 11: Tester la meilleure main parmi plusieurs cartes dans une partie de Texas Hold'em
# Le but est de voir la combinaison de main la plus forte pour les cartes fournies.


hand = ["Ac", "As", "Td", "7s", "7h", "3s", "2c"]
best_hand = poker_eval.best_hand(
    game="holdem", side="hi", hand=hand, include_description=True
)
print(f"best hand from {hand} = {best_hand}")
# best_hand[0] est la description, best_hand[1:] sont les cartes
description = best_hand[0]
cards = best_hand[1:]
print(f"best hand from {hand} = ({description}) {cards}")


# Test 12: Vérification de la meilleure main avec une suite (straight)
hand = ["Ah", "Ts", "Kh", "Qs", "Js"]
best_hand = poker_eval.best_hand(
    game="holdem", side="hi", hand=hand, include_description=True
)
print(f"best hand from {hand} = ({best_hand[0]}) {best_hand[1:]}")

# Test 13: Vérification d'une main forte avec une quinte flush royale (royal flush)
hand = ["2h", "Kh", "Qh", "Jh", "Th"]
best_hand = poker_eval.best_hand(game="holdem", side="hi", hand=hand)
best_hand = poker_eval.best_hand(
    game="holdem", side="hi", hand=hand, include_description=True
)
print(f"best hand from {hand} = ({best_hand[0]}) {best_hand[1:]}")

# Test 14: Vérification de la meilleure main low dans Lowball
hand = ["As", "2s", "4d", "4s", "5c", "5d", "7s"]
best_hand = poker_eval.best_hand(
    game="lowball27", side="low", hand=hand, include_description=False
)
print(f"1/ low hand from {hand} = {best_hand}")
assert len(best_hand) == 5, "Erreur, la meilleure main ne contient pas 5 cartes"

# Si vous souhaitez inclure la description :
best_hand_with_desc = poker_eval.best_hand(
    game="lowball27", side="low", hand=hand, include_description=True
)
description = best_hand_with_desc[0]
cards = best_hand_with_desc[1:]
print(f"best low hand from {hand} = ({description}) {cards}")

# Test 15: Vérification d'une autre main low dans une partie de Lowball
hand = ["As", "2s", "4d", "4s", "5c", "5d", "8s"]
best_hand = poker_eval.best_hand(
    game="lowball27", side="low", hand=hand, include_description=True
)
print(f"best hand from {hand} = ({best_hand[0]}) {best_hand[1:]}")

# Test 16: Tester la meilleure main low à partir d'un tableau et de mains données
board = ["As", "4d", "5h", "7d", "9c"]
hand = ["2s", "Ts", "Jd", "Ks"]
best_hand = poker_eval.best_hand(
    game="lowball27", side="low", hand=hand, board=board, include_description=True
)
print(f"best hand from {hand} = ({best_hand[0]}) {best_hand[1:]}")

# Test 17: Vérification du calcul EV pour une main Hold'em avec simulation Monte Carlo
if len(sys.argv) > 2:
    print(
        "f0 result = %s\n"
        % poker_eval.poker_eval(
            game="holdem",
            pockets=[["As", "3s"], ["__", "__"], ["__", "__"]],
            dead=[],
            board=["__", "Qs", "2c", "Ac", "Kc"],
            iterations=1,
            return_distributed=False,
            seed=-1,
        )
    )

# Tester l'évaluation d'une main spécifique (Ace high) en utilisant evaln()
hand = ["As", "Ad"]
print(f"handval {hand} = {poker_eval.evaln(hand)}")
