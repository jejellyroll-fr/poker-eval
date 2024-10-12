# poker_eval.pyx

# cython: language_level=3, boundscheck=False, wraparound=False, cdivision=True
from libc.stdlib cimport malloc, free
from libc.string cimport memset, memcpy
from cpython cimport bool
from itertools import combinations
import logging
import random  # Utiliser le module random de Python
from libc.stdint cimport uint32_t, uint64_t

# Configurer le logging
# logging.basicConfig(level=logging.DEBUG)

# Importation des types et fonctions externes depuis les en-têtes C
cdef extern from "poker_defs.h":
    ctypedef unsigned int HandVal
    ctypedef unsigned int LowHandVal

cdef extern from "deck_std.h":
    ctypedef struct StdDeck_CardMask:
        pass  # Déclaration de la structure comme opaque

    int StdDeck_stringToCard(const char* str, int* card)
    void StdDeck_cardToString(int card, char* str)

    int StdDeck_N_CARDS

cdef extern from "pokereval_wrapper.h":
    void py_StdDeck_CardMask_RESET(StdDeck_CardMask* cm)
    void py_StdDeck_CardMask_OR(StdDeck_CardMask* res, StdDeck_CardMask* op1, StdDeck_CardMask* op2)
    void py_StdDeck_CardMask_SET(StdDeck_CardMask* mask, int index)
    void py_StdDeck_CardMask_UNSET(StdDeck_CardMask* mask, int index)
    int py_StdDeck_CardMask_CARD_IS_SET(StdDeck_CardMask* cm, int index)

    HandVal py_Hand_EVAL_N(StdDeck_CardMask* hand, int n)
    LowHandVal py_Hand_EVAL_LOW(StdDeck_CardMask* hand, int n)
    LowHandVal py_Hand_EVAL_LOW8(StdDeck_CardMask* hand, int n)
    LowHandVal py_Hand_EVAL_LOWBALL(StdDeck_CardMask* hand, int n)  # Pour lowball27

# Définir une structure pour les résultats d'évaluation
cdef struct EvalResult:
    HandVal handval
    StdDeck_CardMask combined_mask

# Implémentation du Générateur Aléatoire en Cython en utilisant le module random de Python
cdef class MT19937:
    def __cinit__(self):
        pass  # Aucune initialisation nécessaire avec le module random de Python

    cpdef void seed(self, unsigned int seed_value):
        """Initialise le générateur avec une graine en utilisant Python's random."""
        random.seed(seed_value)
        logging.debug(f"MT19937 seeded with {seed_value}")

    cpdef uint32_t rand_uint32(self):
        """Génère un entier non signé de 32 bits en utilisant Python's random."""
        return random.getrandbits(32)

    def rand_double(self):
        """Génère un nombre flottant en double précision dans l'intervalle [0, 1)."""
        return random.random()

    cpdef int rand_int(self, int upper_bound):
        """Génère un entier entre 0 et upper_bound-1 sans biais en utilisant Python's random."""
        if upper_bound <= 0:
            raise ValueError("upper_bound doit être positif")
        if upper_bound == 1:
            return 0
        return random.randint(0, upper_bound - 1)

# Définir la classe PokerEval
cdef class PokerEval:
    cdef list available_cards
    cdef MT19937 rng  # Générateur RNG

    def __cinit__(self):
        self.rng = MT19937()
        self.available_cards = list(range(StdDeck_N_CARDS))
        logging.debug(f"Deck initialisé sans mélange: {self.available_cards}")

    cpdef int get_num_cards(self):
        """Retourne le nombre total de cartes dans le deck."""
        return StdDeck_N_CARDS

    cpdef list get_available_cards(self):
        """Retourne une copie de la liste des cartes disponibles."""
        return self.available_cards[:]

    cpdef remove_card_from_deck(self, int card_num):
        """Supprime une carte du deck si elle est présente."""
        if card_num in self.available_cards:
            self.available_cards.remove(card_num)


    def check_deck_integrity(self):
        """
        Vérifie que le deck contient exactement StdDeck_N_CARDS cartes uniques.
        Retourne True si l'intégrité est maintenue, False sinon.
        """
        current_num_cards = len(self.available_cards)
        unique_cards = len(set(self.available_cards))
        expected_num_cards = self.get_num_cards()

        if current_num_cards != expected_num_cards:
            logging.error(f"Deck contient {current_num_cards} cartes, attendu {expected_num_cards}.")
            return False

        if unique_cards != expected_num_cards:
            logging.error("Deck contient des cartes dupliquées.")
            return False

        return True


    cpdef int rand_int(self, int upper_bound):
        return self.rng.rand_int(upper_bound)

    cpdef uint32_t get_rand_uint32(self):
        """Expose rand_uint32 via une méthode publique."""
        return self.rng.rand_uint32()

    cpdef reset_deck(self):
        """Réinitialise le deck à l'état initial et le mélange."""
        self.available_cards = list(range(StdDeck_N_CARDS))  # Toutes les cartes doivent être présentes
        logging.debug(f"Deck reset: {self.available_cards}")  # Vérification que toutes les cartes sont là
        
        # Vérification de l'intégrité du deck après reset
        if not self.check_deck_integrity():
            logging.error("Intégrité du deck échouée après reset_deck!")
            raise ValueError("Intégrité du deck compromise après reset_deck.")
        
        self.shuffle_deck()


    def shuffle_deck(self):
        """Mélange le deck en utilisant l'algorithme Fisher-Yates sans biais."""
        cdef int i, j, temp
        for i in range(len(self.available_cards) - 1, 0, -1):
            j = self.rng.rand_int(i + 1)  # Génère un indice aléatoire entre 0 et i inclus
            temp = self.available_cards[i]
            self.available_cards[i] = self.available_cards[j]
            self.available_cards[j] = temp
            logging.debug(f"Swapped index {i} ({temp}) with index {j} ({self.available_cards[j]})")

    cpdef reset_seed(self, unsigned int seed):
        """Initialise le générateur RNG avec une graine spécifique."""
        self.rng.seed(seed)
        logging.debug(f"Seed set to: {seed}")

    def string2card(self, cards):
        """Convertit une chaîne de caractères représentant une carte en son entier numérique."""
        if isinstance(cards, (list, tuple)):
            return [self._string2card(card) for card in cards]
        else:
            return self._string2card(cards)

    cdef int _string2card(self, str card):
        cdef int card_num
        cdef bytes card_bytes = card.encode('utf-8')
        cdef const char* card_cstr = <const char*>card_bytes
        if StdDeck_stringToCard(card_cstr, &card_num) != 0:
            # Succès
            return card_num
        else:
            if card == '__':
                return 255  # Placeholder
            else:
                raise ValueError(f"Carte invalide : {card}")

    def card2string(self, cards):
        """Convertit un entier numérique représentant une carte en sa chaîne de caractères."""
        if isinstance(cards, (list, tuple)):
            return [self._card2string(card) for card in cards]
        else:
            return self._card2string(cards)

    cdef str _card2string(self, int card):
        cdef char buf[16]
        if card == 255:
            return '__'
        else:
            StdDeck_cardToString(card, buf)
            return buf.decode('utf-8')

    def evaln(self, cards):
        """Évalue la valeur d'une main donnée."""
        cdef StdDeck_CardMask hand
        cdef int card_num
        py_StdDeck_CardMask_RESET(&hand)
        for card in cards:
            if isinstance(card, str):
                card_num = self._string2card(card)
            else:
                card_num = card
            if card_num != 255:
                py_StdDeck_CardMask_SET(&hand, card_num)
        return py_Hand_EVAL_N(&hand, len(cards))

    def best_hand(self, game, side, hand, board=None):
        """Retourne la meilleure main en termes de cartes (combinaison)."""
        # Liste des jeux supportés
        supported_games = [
            "holdem", "holdem8",
            "omaha", "omaha8",
            "7stud", "7stud8", "7studnsq",
            "razz",
            "lowball", "ace_to_five_lowball", "lowball27",
            "5draw", "5draw8", "5drawnsq"
        ]

        # Vérification du jeu supporté
        if game not in supported_games:
            raise ValueError(f"Jeu non supporté : {game}")

        if board is None:
            board = []

        # Vérification que hand et board sont des listes
        if not isinstance(hand, list) or not isinstance(board, list):
            raise TypeError("Les mains et le tableau doivent être des listes.")

        # Vérification du nombre de cartes minimum
        if len(hand) + len(board) < 5:
            raise ValueError("Il faut au moins 5 cartes pour évaluer une main (combinaison de hand et board).")

        # Créer la structure pour stocker le résultat
        cdef EvalResult result

        # Utiliser la méthode spécifique pour Omaha si nécessaire
        if game in ["omaha", "omaha8"]:
            if len(hand) != 4:
                raise ValueError(f"Omaha nécessite exactement 4 cartes en main, mais {len(hand)} ont été fournies.")
            result = self.eval_omaha_hand_cdef(game, side, hand, board)
        else:
            # Sinon, utiliser la méthode d'évaluation exhaustive pour les autres jeux
            result = self.eval_best_hand_exhaustive_cdef(game, side, hand, board)

        # Retourner les détails de la main
        return self._get_hand_details(result.handval, &result.combined_mask, include_description=False)

    def best_hand_value(self, game, side, hand, board=None):
        """Retourne la valeur numérique de la meilleure main."""
        if board is None:
            board = []
        if len(hand) + len(board) < 5:
            return False

        # S'assurer que hand et board sont bien des listes
        if not isinstance(hand, list) or not isinstance(board, list):
            raise TypeError("Hand and board must be lists.")

        cdef EvalResult result
        if game in ["omaha", "omaha8"]:
            result = self.eval_omaha_hand_cdef(game, side, hand, board)
        else:
            result = self.eval_best_hand_exhaustive_cdef(game, side, hand, board)

        return result.handval

    cdef EvalResult eval_hand_cdef(self, str game, str side, list hand, list board):
        """Évalue une main spécifique et retourne les résultats dans une structure C."""

        # Vérification du nombre de cartes en fonction du jeu
        cdef dict game_hole_cards = {
            "holdem": 2,
            "holdem8": 2,
            "omaha": 4,
            "omaha8": 4,
            "7stud": 7,
            "7stud8": 7,
            "7studnsq": 7,
            "razz": 7,
            "lowball": 5,
            "ace_to_five_lowball": 5,
            "lowball27": 5,
            "5draw": 5,
            "5draw8": 5,
            "5drawnsq": 5
        }

        expected_hole_cards = game_hole_cards.get(game, -1)
        if expected_hole_cards == -1:
            raise ValueError(f"Jeu non supporté : {game}")

        if len(hand) != expected_hole_cards:
            raise ValueError(f"{game} nécessite exactement {expected_hole_cards} cartes en main, mais {len(hand)} ont été fournies.")

        # Si le jeu est Omaha ou Omaha Hi/Lo, évaluer avec la méthode spécifique pour Omaha
        if game in ["omaha", "omaha8"]:
            return self.eval_omaha_hand_cdef(game, side, hand, board)
        else:
            return self.eval_best_hand_exhaustive_cdef(game, side, hand, board)

    cdef EvalResult eval_best_hand_exhaustive_cdef(self, str game, str side, list hand, list board):
        """Évalue la meilleure main de cinq cartes pour les jeux non-Omaha en utilisant une recherche exhaustive."""

        # Vérifier que la somme de la main et du tableau fait au moins 5 cartes
        if len(hand) + len(board) < 5:
            raise ValueError(f"Le jeu {game} nécessite au moins 5 cartes, mais {len(hand)} en main et {len(board)} sur le tableau ont été fournies.")

        cdef list all_cards = hand + board
        cdef HandVal best_val
        cdef HandVal current_val  # Pour stocker l'évaluation actuelle
        cdef StdDeck_CardMask best_mask, current_mask
        cdef int card_num
        py_StdDeck_CardMask_RESET(&best_mask)

        # Initialiser la meilleure valeur pour les variantes 'hi' et 'low'
        if side == 'hi':
            best_val = 0
        elif side == 'low':
            best_val = 0xFFFFFFFF

        # Générer toutes les combinaisons possibles de cinq cartes
        for combo in combinations(all_cards, 5):
            py_StdDeck_CardMask_RESET(&current_mask)
            for card in combo:
                if isinstance(card, str):
                    card_num = self._string2card(card)
                else:
                    card_num = card
                if card_num != 255:
                    py_StdDeck_CardMask_SET(&current_mask, card_num)

            # Évaluation de la main pour 'hi' ou 'low'
            if side == 'hi':
                current_val = py_Hand_EVAL_N(&current_mask, 5)
                if current_val > best_val:
                    best_val = current_val
                    memcpy(&best_mask, &current_mask, sizeof(StdDeck_CardMask))
            elif side == 'low':
                if game == 'razz' or game == 'lowball27':
                    current_val = py_Hand_EVAL_LOW(&current_mask, 5)
                else:
                    current_val = py_Hand_EVAL_LOW8(&current_mask, 5)
                if current_val < best_val:
                    best_val = current_val
                    memcpy(&best_mask, &current_mask, sizeof(StdDeck_CardMask))

        # Retourner la meilleure main trouvée
        cdef EvalResult result
        result.handval = best_val
        result.combined_mask = best_mask
        return result

    cdef EvalResult eval_omaha_hand_cdef(self, str game, str side, list hand, list board):
        """Évalue une main Omaha spécifique et retourne les résultats dans une structure C."""
        cdef EvalResult result
        cdef int card_num
        cdef list hole_cards = []
        cdef list board_cards = []
        cdef HandVal best_hi = 0
        cdef LowHandVal best_lo = 0xFFFFFFFF
        cdef HandVal hi_val
        cdef LowHandVal lo_val
        cdef StdDeck_CardMask best_hi_mask, best_lo_mask, hand_mask, board_mask, combined_mask

        py_StdDeck_CardMask_RESET(&best_hi_mask)
        py_StdDeck_CardMask_RESET(&best_lo_mask)

        # Préparer les cartes de la main (exactement 4 cartes pour Omaha)
        for card in hand:
            if isinstance(card, str):
                card_num = self._string2card(card)
            else:
                card_num = card
            if card_num != 255:
                hole_cards.append(card_num)
        if len(hole_cards) != 4:
            raise ValueError(f"Omaha nécessite exactement 4 cartes en main, mais {len(hole_cards)} ont été fournies.")

        # Préparer les cartes du board (exactement 5 cartes sur le board)
        for card in board:
            if isinstance(card, str):
                card_num = self._string2card(card)
            else:
                card_num = card
            if card_num != 255:
                board_cards.append(card_num)
        if len(board_cards) != 5:
            raise ValueError(f"Omaha nécessite exactement 5 cartes sur le board, mais {len(board_cards)} ont été fournies.")

        # Générer les combinaisons de cartes de la main (2 cartes)
        cdef list hole_combinations = []
        for i in range(0, 3):
            for j in range(i + 1, 4):
                hole_combinations.append((hole_cards[i], hole_cards[j]))

        # Générer les combinaisons de cartes du board (3 cartes)
        cdef list board_combinations = []
        for a in range(0, 3):
            for b in range(a + 1, 4):
                for c in range(b + 1, 5):
                    board_combinations.append((board_cards[a], board_cards[b], board_cards[c]))

        for hole_combo in hole_combinations:
            py_StdDeck_CardMask_RESET(&hand_mask)
            py_StdDeck_CardMask_SET(&hand_mask, hole_combo[0])
            py_StdDeck_CardMask_SET(&hand_mask, hole_combo[1])

            for board_combo in board_combinations:
                py_StdDeck_CardMask_RESET(&board_mask)
                py_StdDeck_CardMask_SET(&board_mask, board_combo[0])
                py_StdDeck_CardMask_SET(&board_mask, board_combo[1])
                py_StdDeck_CardMask_SET(&board_mask, board_combo[2])

                py_StdDeck_CardMask_OR(&combined_mask, &hand_mask, &board_mask)

                if side == 'hi':
                    hi_val = py_Hand_EVAL_N(&combined_mask, 5)
                    if hi_val > best_hi:
                        best_hi = hi_val
                        memcpy(&best_hi_mask, &combined_mask, sizeof(StdDeck_CardMask))
                elif side == 'low':
                    if game == 'razz' or game == 'lowball27':
                        lo_val = py_Hand_EVAL_LOW(&combined_mask, 5)
                    else:
                        lo_val = py_Hand_EVAL_LOW8(&combined_mask, 5)
                    if lo_val < best_lo:
                        best_lo = lo_val
                        memcpy(&best_lo_mask, &combined_mask, sizeof(StdDeck_CardMask))

        if side == 'hi':
            result.handval = best_hi
            result.combined_mask = best_hi_mask
        elif side == 'low':
            result.handval = best_lo
            result.combined_mask = best_lo_mask
        else:
            raise ValueError("Le paramètre 'side' doit être 'hi' ou 'low'.")

        return result

    def eval_hand(self, game, side, hand, board):
        """Évalue une main spécifique et retourne les résultats sous forme de liste Python."""
        cdef EvalResult result = self.eval_hand_cdef(game, side, hand, board)
        cdef list hand_details = self._get_hand_details(result.handval, &result.combined_mask, include_description=True, low=(side == 'low'))
        return [result.handval, hand_details]

    cdef list _get_hand_details(self, HandVal handval, StdDeck_CardMask* hand_mask, bint include_description=True, bint low=False):
        """Récupère les détails de la main pour l'affichage."""

        cdef list all_cards = self._cardmask_to_list(hand_mask)
        cdef int j, k, temp
        cdef list sorted_cards = []
        cdef str description

        if low:
            # Trier les cartes par ordre croissant
            all_cards.sort()
        else:
            # Trier les cartes par ordre décroissant
            all_cards.sort(reverse=True)

        # Extraire les 5 meilleures cartes après tri
        for j in range(min(5, len(all_cards))):
            sorted_cards.append(self.card2string(all_cards[j]))

        if include_description:
            description = self.hand_type(handval)
            return [description] + sorted_cards
        else:
            return sorted_cards

    cdef list _cardmask_to_list(self, StdDeck_CardMask* mask):
        """Convertit un StdDeck_CardMask en une liste de cartes."""
        cdef list cards = []
        cdef int i
        for i in range(StdDeck_N_CARDS):
            if py_StdDeck_CardMask_CARD_IS_SET(mask, i):
                cards.append(i)
        return cards

    def hand_type(self, HandVal handval):
        """Décrit la force d'une main."""
        hand_types = {
            0: "Nothing",
            1: "NoPair",
            2: "OnePair",
            3: "TwoPair",
            4: "Trips",
            5: "Straight",
            6: "Flush",
            7: "FullHouse",
            8: "Quads",
            9: "StFlush"
        }
        return hand_types.get(handval >> 26, "Unknown")

    cpdef dict poker_eval(self, game, pockets, board=None, dead=None, int iterations=0, bint return_distributed=False, int seed=-1):
        """Évalue l'état du jeu de poker basé sur différentes variantes et calcule l'EV."""
        # Réinitialiser la seed aléatoire pour la simulation Monte Carlo
        self.reset_seed(seed)
        logging.debug(f"Seed set to: {seed}")

        # Réinitialiser le deck
        self.reset_deck()
        logging.debug(f"Deck après réinitialisation dans poker_eval: {self.available_cards}")

        # Vérifier le type de jeu
        supported_games = [
            "holdem", "holdem8",
            "omaha", "omaha8",
            "7stud", "7stud8", "7studnsq",
            "razz",
            "lowball", "ace_to_five_lowball", "lowball27",
            "5draw", "5draw8", "5drawnsq"
        ]
        if game not in supported_games:
            raise ValueError(f"Type de jeu non supporté : {game}")

        if board is None:
            board = []
        if dead is None:
            dead = []

        # Déclarations des variables
        cdef int num_players = len(pockets)
        cdef set all_distributed = set()
        cdef int i, j, _  # Variables pour les boucles
        cdef list pocket
        cdef int expected_hole_cards
        cdef bint has_low
        cdef HandVal best_value_hi = 0
        cdef int num_best_hi = 0
        cdef LowHandVal best_value_lo = 0
        cdef int num_best_lo = 0
        cdef int total_samples = iterations if iterations > 0 else 1
        cdef double pot_fraction
        cdef double hipot = 1.0
        cdef double lopot = 0.0
        cdef list losehi_results = [0] * num_players
        cdef list winhi, losehi, tiehi, winlo, loselo, tielo, scoop, ev
        cdef list distributed_cards_players
        cdef list distributed_cards_board
        cdef list filled_board
        cdef list filled_pockets
        cdef list eval_results_hi
        cdef list eval_results_lo
        cdef HandVal best_value_hi_local
        cdef LowHandVal best_value_lo_local
        cdef int num_best_hi_local
        cdef int num_best_lo_local
        cdef int card_num
        cdef dict result_dict

        # Ajouter les cartes connues du tableau à all_distributed
        for card in board:
            if card != '__' and card != 255:
                all_distributed.add(self._string2card(card))

        # Ajouter les cartes connues des pockets à all_distributed
        for pocket in pockets:
            for card in pocket:
                if card != '__' and card != 255:
                    all_distributed.add(self._string2card(card))

        # Définir le nombre requis de cartes par poche en fonction du jeu
        cdef dict game_hole_cards = {
            "holdem": 2,
            "holdem8": 2,
            "omaha": 4,
            "omaha8": 4,
            "7stud": 7,
            "7stud8": 7,
            "7studnsq": 7,
            "razz": 7,
            "lowball": 5,
            "ace_to_five_lowball": 5,
            "lowball27": 5,
            "5draw": 5,
            "5draw8": 5,
            "5drawnsq": 5
        }

        # Assignation de expected_hole_cards et vérification du jeu supporté
        expected_hole_cards = game_hole_cards.get(game, -1)
        if expected_hole_cards == -1:
            raise ValueError(f"Jeu non supporté : {game}")

        # Calcul de has_low en fonction du jeu
        has_low = game.endswith('8') or game in ["razz", "lowball", "ace_to_five_lowball", "lowball27"]

        # Vérifier les mains vides et les traiter comme perdantes
        for i in range(num_players):
            pocket = pockets[i]
            for j in range(len(pocket)):
                if pocket[j] == "__" or pocket[j] == 255:
                    losehi_results[i] = total_samples  # Considérer la main vide comme perdante
                    break

        # Initialisation des listes pour les résultats
        winhi = [0] * num_players
        losehi = losehi_results  # Utiliser les résultats de perte modifiés
        tiehi = [0] * num_players
        winlo = [0] * num_players
        loselo = [0] * num_players
        tielo = [0] * num_players
        scoop = [0] * num_players
        ev = [0.0] * num_players
        distributed_cards_players = [[] for _ in range(num_players)]  # Initialiser chaque élément
        distributed_cards_board = []  # Liste pour les cartes du tableau

        # Simulation Monte Carlo ou évaluation exhaustive
        for _ in range(total_samples):
            # Réinitialiser le deck et exclure les cartes déjà distribuées
            self.reset_deck()
            logging.debug(f"Deck avant distribution: {self.available_cards}")
            for card_num in all_distributed:
                if card_num in self.available_cards:
                    self.available_cards.remove(card_num)
                    logging.debug(f"Deck après distribution: {self.available_cards}")

            # Remplir les cartes manquantes du tableau
            filled_board = self.fill_board_cdef(board, all_distributed.copy())

            # Remplir les pockets manquantes
            filled_pockets = self.fill_pockets_cdef(pockets, expected_hole_cards, already_distributed=all_distributed.copy())

            # Évaluer les mains hautes
            eval_results_hi = []
            for current_pocket in filled_pockets:
                eval_results_hi.append(self.eval_hand(game, 'hi', current_pocket, filled_board))

            # Si le jeu supporte les mains basses, évaluer les mains basses
            if has_low:
                eval_results_lo = []
                for current_pocket in filled_pockets:
                    eval_results_lo.append(self.eval_hand(game, 'low', current_pocket, filled_board))
            else:
                eval_results_lo = [None] * num_players

            # Trouver la meilleure valeur haute
            best_value_hi_local = 0
            for result in eval_results_hi:
                if result[0] > best_value_hi_local:
                    best_value_hi_local = result[0]

            # Compter le nombre de joueurs ayant la meilleure valeur haute
            num_best_hi_local = 0
            for result in eval_results_hi:
                if result[0] == best_value_hi_local:
                    num_best_hi_local += 1

            # Trouver la meilleure valeur basse
            if has_low:
                best_value_lo_local = 0xFFFFFFFF
                for result in eval_results_lo:
                    if result and result[0] < best_value_lo_local:
                        best_value_lo_local = result[0]

                # Compter le nombre de joueurs ayant la meilleure valeur basse
                num_best_lo_local = 0
                for result in eval_results_lo:
                    if result and result[0] == best_value_lo_local:
                        num_best_lo_local += 1
            else:
                num_best_lo_local = 0

            # Calculer les gains pour chaque joueur
            for i in range(num_players):
                # Si la main a été marquée comme perdante, passer
                if losehi_results[i] == total_samples:
                    continue

                # Main haute
                if eval_results_hi[i][0] == best_value_hi_local:
                    if num_best_hi_local == 1:
                        winhi[i] += 1
                    else:
                        tiehi[i] += 1
                else:
                    losehi[i] += 1

                # Main basse
                if has_low:
                    if eval_results_lo[i] and eval_results_lo[i][0] == best_value_lo_local:
                        if num_best_lo_local == 1:
                            winlo[i] += 1
                        else:
                            tielo[i] += 1
                    else:
                        loselo[i] += 1

                # Calculer l'EV
                pot_fraction = 0.0
                if has_low:
                    hipot = 0.5
                    lopot = 0.5

                # Part du pot haute
                if eval_results_hi[i][0] == best_value_hi_local:
                    hi_share = hipot / num_best_hi_local
                    pot_fraction += hi_share

                # Part du pot basse
                if has_low and eval_results_lo[i] and eval_results_lo[i][0] == best_value_lo_local:
                    lo_share = lopot / num_best_lo_local
                    pot_fraction += lo_share

                ev[i] += pot_fraction

                # Comptage des scoops
                if has_low:
                    if eval_results_hi[i][0] == best_value_hi_local and eval_results_lo[i] and eval_results_lo[i][0] == best_value_lo_local:
                        if num_best_hi_local == 1 and num_best_lo_local == 1:
                            scoop[i] += 1

            # Stocker les cartes distribuées si nécessaire
            if return_distributed and total_samples > 0:
                # Stocker les cartes des joueurs
                for i in range(num_players):
                    for card in filled_pockets[i]:
                        if card != '__' and card != 255:
                            distributed_cards_players[i].append(card)

                # Ajouter les cartes du tableau une seule fois par itération
                for card in filled_board:
                    if card != '__' and card != 255:
                        distributed_cards_board.append(card)

        # Calculer les moyennes sur le total des échantillons
        for i in range(num_players):
            ev[i] = ev[i] / total_samples

        # Préparer le résultat
        result_dict = {}
        result_dict['info'] = [total_samples, int(has_low), 1]
        result_dict['eval'] = []
        for i in range(num_players):
            result_dict['eval'].append({
                'scoop': scoop[i],
                'winhi': winhi[i],
                'losehi': losehi[i],
                'tiehi': tiehi[i],
                'winlo': winlo[i] if has_low else 0,
                'loselo': loselo[i] if has_low else 0,
                'tielo': tielo[i] if has_low else 0,
                'ev': int(ev[i] * 1000)
            })

        if return_distributed and total_samples > 0:
            # Séparer les cartes distribuées en privées et tableau
            result_dict['distributed_cards_players'] = distributed_cards_players
            result_dict['distributed_cards_board'] = distributed_cards_board

        return result_dict

    def fill_pockets(self, pockets):
        """Méthode publique pour remplir les pockets avec les cartes manquantes."""
        cdef set already_distributed = set()
        cdef int expected_hole_cards = 2  # ou 4 pour Omaha, selon le jeu
        return self.fill_pockets_cdef(pockets, expected_hole_cards, already_distributed)

    cpdef list fill_pockets_cdef(self, list pockets, int expected_cards_per_pocket=2, set already_distributed=None):
        if already_distributed is None:
            already_distributed = set()
        cdef list filled_pockets = []
        cdef int selected_card
        cdef list filled
        cdef int j

        for pocket in pockets:
            filled = []
            for j in range(len(pocket)):
                card = pocket[j]
                if card == '__' or card == '255':
                    if not self.available_cards:
                        self.reset_deck()
                    selected_card = self.random_card()
                    filled.append(self.card2string(selected_card))
                    already_distributed.add(selected_card)
                else:
                    filled.append(card)
                    try:
                        card_num = self._string2card(card)
                        already_distributed.add(card_num)
                        if card_num in self.available_cards:
                            self.available_cards.remove(card_num)
                    except ValueError:
                        pass  # Ignore invalid cards
            # Remplir les cartes manquantes si nécessaire
            while len(filled) < expected_cards_per_pocket:
                if not self.available_cards:
                    self.reset_deck()
                selected_card = self.random_card()
                filled.append(self.card2string(selected_card))
                already_distributed.add(selected_card)
            filled_pockets.append(filled)
        return filled_pockets


    def get_random_card(self):
        """Returns a random card from the deck that hasn't been distributed yet."""
        return self.random_card()

    cdef list fill_board_cdef(self, list board, set already_distributed):
        cdef list filled_board = board.copy()
        cdef int selected_card
        cdef int i
        cdef str card

        for i in range(len(filled_board)):
            card = filled_board[i]
            if card == '__' or card == '255':
                if not self.available_cards:
                    raise ValueError("Plus de cartes disponibles pour la distribution.")
                selected_card = self.random_card()
                filled_board[i] = self.card2string(selected_card)
                already_distributed.add(selected_card)
        return filled_board


    cpdef int random_card(self):
        """Retourne une carte aléatoire du deck qui n'a pas encore été distribuée."""
        if not self.available_cards:
            raise ValueError("Plus de cartes disponibles pour la distribution.")
        cdef int upper_bound = len(self.available_cards)
        cdef int selected_index = self.rng.rand_int(upper_bound)
        cdef int selected_card = self.available_cards.pop(selected_index)
        card_str = self.card2string(selected_card)
        logging.debug(f"Random index: {selected_index}, selected_card: {selected_card} ({card_str})")
        #if card_str == "5c":
            #logging.warning("La carte 5c a été distribuée.")
        return selected_card

    def winners(self, game, pockets, board=None, fill_pockets=False):
        """Détermine les gagnants parmi plusieurs mains fournies."""
        if board is None:
            board = []
        if fill_pockets:
            pockets = self.fill_pockets(pockets)

        cdef bint has_low = game.endswith('8') or game in ['razz', 'lowball', 'lowball27', 'ace_to_five_lowball']
        cdef list results = []
        cdef int i
        cdef list pocket
        cdef list hi_result
        cdef list low_result
        cdef HandVal best_hi_value = 0
        cdef LowHandVal best_lo_value = 0
        cdef list winners_hi = []
        cdef list winners_low = []
        cdef tuple result

        # Évaluer la main de chaque joueur
        for i in range(len(pockets)):
            pocket = pockets[i]
            hi_result = self.eval_hand(game, 'hi', pocket, board)
            if has_low:
                low_result = self.eval_hand(game, 'low', pocket, board)
            else:
                low_result = [None, None]
            results.append((hi_result, low_result))

        # Trouver la meilleure main haute
        best_hi_value = 0
        for result in results:
            if result[0][0] > best_hi_value:
                best_hi_value = result[0][0]

        # Identifier les gagnants pour la main haute
        for i in range(len(results)):
            if results[i][0][0] == best_hi_value:
                winners_hi.append(i)

        # Trouver la meilleure main basse
        if has_low:
            best_lo_value = 0xFFFFFFFF
            for result in results:
                if result[1][0] is not None and result[1][0] < best_lo_value:
                    best_lo_value = result[1][0]

            # Identifier les gagnants pour la main basse
            for i in range(len(results)):
                if results[i][1][0] == best_lo_value:
                    winners_low.append(i)
        else:
            winners_low = []

        return {
            'hi': winners_hi,
            'low': winners_low
        }

    def deck(self):
        """Retourne la liste de toutes les cartes du jeu."""
        return [self.card2string(i) for i in range(StdDeck_N_CARDS)]

    def nocard(self):
        """Retourne la valeur de la carte joker ou placeholder."""
        return 255
