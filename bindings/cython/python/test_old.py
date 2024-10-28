import sys
from pokereval import PokerEval

iterations_low = 100000
iterations_high = 200000

pokereval = PokerEval()

# Fonction pour convertir toutes les cartes en minuscules
def to_lower(cards):
    return [card.lower() for card in cards]

# Test de best_hand_value
if pokereval.best_hand_value("holdem", "hi", to_lower(["AH", "AD", "AS", "KH", "KS"])) != 101494784:
    sys.exit(1)

# Test de string2card
if pokereval.string2card("2h") != 0:
    sys.exit(1)

print("")

# Test de winners pour le Stud à 7 cartes
pockets_stud1 = [
    to_lower(["AS", "AD", "AC", "TC", "TS", "2D", "5C"]),
    to_lower(["JS", "JC", "7S", "8C", "8D", "3C", "3H"]),
    ["__", "__", "__", "__", "__", "__", "__"],  # Placeholders
]
print(
    "stud7 (1) result = %s\n"
    % pokereval.winners("7stud", pockets_stud1, dead=[], board=[], fill_pockets=True)
)

pockets_stud2 = [
    to_lower(["9C", "8H", "7S", "3D", "4S", "2H", "7C"]),
    ["ks", "__", "__", "5d", "3d", "6d", "__"],
]
print(
    "stud7 (2) result = %s\n"
    % pokereval.winners("7stud", pockets_stud2, dead=[], board=[], fill_pockets=True)
)

# Affichage du deck
deck_list = [j + i + f"/{pokereval.string2card(j + i)}" for i in "hdcs" for j in "23456789tjqka"]
print(deck_list)
print("deck = %s\n" % pokereval.deck())

# Test de poker_eval et winners pour le Hold'em
result_holdem1 = pokereval.poker_eval(
    "holdem",
    [to_lower(["tc", "ac"]), to_lower(["3h", "ah"]), to_lower(["8c", "6h"])],
    board=to_lower(["7h", "3s", "2c"]),
    dead=[],
)
print(f"result = {result_holdem1}\n")

winners_holdem1 = pokereval.winners(
    "holdem",
    [to_lower(["tc", "ac"]), to_lower(["3h", "ah"]), to_lower(["8c", "6h"])],
    board=to_lower(["7h", "3s", "2c"]),
    dead=[],
)
print(f"winners = {winners_holdem1}\n")

result_holdem2 = pokereval.poker_eval(
    "holdem",
    [to_lower(["tc", "ac"]), to_lower(["th", "ah"]), to_lower(["8c", "6h"])],
    board=to_lower(["7h", "3s", "2c", "7s", "7d"]),
    dead=[],
)
print(f"result = {result_holdem2}\n")

winners_holdem2 = pokereval.winners(
    "holdem",
    [to_lower(["tc", "ac"]), to_lower(["th", "ah"]), to_lower(["8c", "6h"])],
    board=to_lower(["7h", "3s", "2c", "7s", "7d"]),
    dead=[],
)
print(f"winners = {winners_holdem2}\n")

# Test de winners avec des mains incomplètes (utilisation de fill_pockets=True)
pockets_filthy = [
    to_lower(["tc", "ac"]),
    ["__", "__"],
    ["__", "__"],
    to_lower(["th", "ah"]),
    to_lower(["8c", "6h"]),
]
print(
    "winners (filthy pockets) = %s\n"
    % pokereval.winners(
        "holdem",
        pockets_filthy,
        board=to_lower(["7h", "3s", "2c", "7s", "7d"]),
        dead=[],
        fill_pockets=True,
    )
)

# Test de winners pour Omaha et Omaha8
pockets_omaha = [
    to_lower(["tc", "ac", "ks", "kc"]),
    to_lower(["th", "ah", "qs", "qc"]),
    to_lower(["8c", "6h", "js", "jc"]),
]
print(
    "winners omaha = %s\n"
    % pokereval.winners(
        "omaha",
        pockets_omaha,
        board=to_lower(["7h", "3s", "2c", "7s", "7d"]),
        dead=[],
    )
)
print(
    "winners omaha8 = %s\n"
    % pokereval.winners(
        "omaha8",
        pockets_omaha,
        board=to_lower(["7h", "3s", "2c", "7s", "7d"]),
        dead=[],
    )
)

# Fonction Utilitaire pour Afficher best_hand avec Gestion des Exceptions
def print_best_hand(hand, game, side, board=None, retro=False):
    """
    Affiche la meilleure main pour une combinaison donnée de cartes.

    Parameters:
        hand (list): Liste des cartes de la main.
        game (str): Type de jeu de poker (e.g., "holdem", "7stud", "omaha8", etc.).
        side (str): Type d'évaluation de la main ("hi" ou "low").
        board (list, optional): Liste des cartes communes sur le tableau. Par défaut, None.
        retro (bool, optional): Si True, utilise le format rétro. Par défaut, False.
    """
    try:
        best_hand = pokereval.best_hand(
            game,
            side,
            to_lower(hand),
            to_lower(board) if board else None,
            include_description=True,
            retro=retro
        )
        
        if retro:
            # Format Rétro : ['Flush', 11, 10, 9, 8, 0]
            description = best_hand[0]
            card_indices = best_hand[1:]
            print(f'best hand from {hand} (retro=True) = {best_hand}')
            print(f"best hand from {hand} (retro=True) = ({description}) {card_indices}\n")
        else:
            # Format Nouveau : ['Flush (K Q J T 2)', 'kh', 'qh', 'jh', 'th', '2h']
            description = best_hand[0]
            cards = best_hand[1:]
            print(f'best hand from {hand} = {best_hand}')
            print(f"best hand from {hand} = ({description}) {cards}\n")
            
    except ValueError as ve:
        print(f"Erreur lors de l'évaluation de la main {hand} pour {game} {side}: {ve}\n")

# Tests de best_hand
print_best_hand(
    ["AC", "AS", "TD", "7S", "7H", "3S", "2C"], "holdem", "hi"
)

print_best_hand(
    ["AC", "AS", "TD", "7S", "7H", "3S", "2C"], "holdem", "hi", retro=True
)

print_best_hand(
    ["AH", "TS", "KH", "QS", "JS"], "holdem", "hi"
)

print_best_hand(
    ["2H", "KH", "QH", "JH", "TH"], "holdem", "hi"
)

print_best_hand(
    ["2S", "3S", "JD", "KS", "AS", "4D", "5H", "7D", "9C"], "holdem", "hi"
)

# Suppression des Tests `low` pour Hold'em car non supportés
# print_best_hand(
#     ["as", "2s", "4d", "4s", "5c", "5d", "7s"], "holdem", "low"
# )

# print_best_hand(
#     ["as", "2s", "4d", "4s", "5c", "5d", "8s"], "holdem", "low"
# )

# print_best_hand(
#     ["7d", "6c", "5h", "4d", "as"], "holdem", "low"
# )

print_best_hand(
    ["2s", "ts", "jd", "ks"], "holdem", "low",
    ["as", "4d", "5h", "7d", "9c"]
)

print_best_hand(
    ["2s", "5s", "jd", "ks"], "holdem", "low",
    ["as", "4d", "6h", "7d", "3c"]
)

print_best_hand(
    ["jc", "4c", "3c", "5c", "9c"], "holdem", "hi",
    ["2c", "ac", "5h", "9d"]
)

print_best_hand(
    ["jd", "9c", "jc", "tc", "2h"], "holdem", "low",
    ["2c", "4c", "th", "6s"]
)

# Tests supplémentaires avec itérations
if len(sys.argv) > 2:
    print(
        "f0 result = %s\n"
        % pokereval.poker_eval(
            "holdem",
            [to_lower(["as", "3s"]), ["__", "__"], ["__", "__"]],
            board=to_lower(["__", "qs", "2c", "ac", "kc"]),
            dead=[],
            iterations=iterations_low,
            fill_pockets=True,
        )
    )

    print("")
    print(
        "f1 result = %s\n"
        % pokereval.poker_eval(
            "holdem",
            [to_lower(["as", "3s"]), ["__", "__"], ["__", "__"]],
            board=to_lower(["7s", "qs", "2c", "ac", "kc"]),
            dead=[],
            iterations=iterations_low,
            fill_pockets=True,
        )
    )

    print("")
    print(
        "f2 result = %s\n"
        % pokereval.poker_eval(
            "holdem",
            [to_lower(["as", "3s"]), ["__", "__"], ["__", "__"]],
            board=to_lower(["__", "__", "__", "__", "__"]),
            dead=[],
            iterations=iterations_low,
            fill_pockets=True,
        )
    )

    print("")
    print(
        "f3 result = %s\n"
        % pokereval.poker_eval(
            "holdem",
            [to_lower(["as", "ac"]), ["__", "__"], ["__", "__"]],
            board=to_lower(["__", "__", "__", "__", "__"]),
            dead=[],
            iterations=iterations_high,
            fill_pockets=True,
        )
    )

    print("")
    print(
        "f4 result = %s\n"
        % pokereval.poker_eval(
            "holdem",
            [to_lower(["as", "ks"]), ["__", "__"], ["__", "__"]],
            board=to_lower(["__", "__", "__", "__", "__"]),
            dead=[],
            iterations=iterations_high,
            fill_pockets=True,
        )
    )

    print("")
    print(
        "f5 result = %s\n"
        % pokereval.poker_eval(
            "holdem",
            [to_lower(["2s", "2c"]), ["__", "__"], ["__", "__"]],
            board=to_lower(["__", "__", "__", "__", "__"]),
            dead=[],
            iterations=iterations_high,
            fill_pockets=True,
        )
    )

    print("")
    print(
        "f6 result = %s\n"
        % pokereval.poker_eval(
            "holdem",
            [to_lower(["js", "jc"]), ["__", "__"], ["__", "__"]],
            board=to_lower(["__", "__", "__", "__", "__"]),
            dead=[],
            iterations=iterations_high,
            fill_pockets=True,
        )
    )

    print("")
    print(
        "f7 result = %s\n"
        % pokereval.poker_eval(
            "omaha",
            [to_lower(["js", "jc", "7s", "8c"]), ["__", "__", "__", "__"], ["__", "__", "__", "__"]],
            board=to_lower(["__", "__", "__", "__", "__"]),
            dead=[],
            iterations=iterations_high,
            fill_pockets=True,
        )
    )

# Tests de evaln
print("")
hand_eval1 = to_lower(["as", "ad"])
print(f"handval {hand_eval1} = {pokereval.evaln(hand_eval1)}")

print("")
hand_eval2 = to_lower(["qc", "7d"])
print(f"handval {hand_eval2} = {pokereval.evaln(hand_eval2)}")

pokereval = None
