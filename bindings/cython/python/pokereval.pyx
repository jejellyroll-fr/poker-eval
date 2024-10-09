# cython: language_level=3
from libc.stdlib cimport rand, srand
from time import time
from libc.string cimport memcpy

from itertools import combinations

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

# Définir une structure pour les résultats d'évaluation
cdef struct EvalResult:
    HandVal handval
    StdDeck_CardMask combined_mask

# Variable globale pour indiquer si la graine a été initialisée
cdef bint is_seed_initialized = False

# Définition de la classe PokerEval
cdef class PokerEval:
    def __cinit__(self):
        pass

    def string2card(self, cards):
        """Convertit une chaîne de caractères représentant une carte en son entier numérique."""
        if isinstance(cards, (list, tuple)):
            return [self._string2card(card) for card in cards]
        else:
            return self._string2card(cards)

    cdef int _string2card(self, str card):
        cdef int card_num
        cdef bytes card_bytes = card.encode('utf-8')
        cdef const char* card_cstr = card_bytes
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
        if game in ["omaha", "omaha8"]:
            return self.eval_omaha_hand_cdef(game, side, hand, board)
        else:
            return self.eval_best_hand_exhaustive_cdef(game, side, hand, board)

    cdef EvalResult eval_best_hand_exhaustive_cdef(self, str game, str side, list hand, list board):
        """Évalue la meilleure main de cinq cartes pour les jeux non-Omaha en utilisant une recherche exhaustive."""
        
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
            best_val = 0xFFFFFFFF  # On initialise à la plus haute valeur pour minimiser ensuite.

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
                if game == 'razz':
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
        cdef LowHandVal best_lo = 0xFFFFFFFF  # Valeur maximale pour la main basse
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
            raise ValueError("Omaha nécessite exactement 4 cartes en main.")

        # Préparer les cartes du board (exactement 5 cartes sur le board)
        for card in board:
            if isinstance(card, str):
                card_num = self._string2card(card)
            else:
                card_num = card
            if card_num != 255:
                board_cards.append(card_num)
        if len(board_cards) != 5:
            raise ValueError("Omaha nécessite exactement 5 cartes sur le board.")

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
                    if game == 'razz':
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
        hand_details = self._get_hand_details(result.handval, &result.combined_mask, include_description=True, low=(side == 'low'))
        return [result.handval, hand_details]

    cdef list _get_hand_details(self, HandVal handval, StdDeck_CardMask* hand_mask, bint include_description=True, bint low=False):
        """Récupère les détails de la main pour l'affichage."""
        cdef list all_cards = self.card2string(self._cardmask_to_list(hand_mask))
        cdef str hand_description = self.hand_type(handval)
        # Tri des cartes pour extraire les 5 meilleures
        cdef list sorted_cards = sorted(all_cards, key=lambda card: self._string2card(card), reverse=not low)

        if include_description:
            return [hand_description] + sorted_cards[:5]
        else:
            return sorted_cards[:5]

    cdef list _cardmask_to_list(self, StdDeck_CardMask* mask):
        """Convertit un StdDeck_CardMask en une liste de cartes."""
        cdef list cards = []
        cdef int i
        for i in range(StdDeck_N_CARDS):
            if py_StdDeck_CardMask_CARD_IS_SET(mask, i):
                cards.append(i)
        return cards

    cdef str hand_type(self, HandVal handval):
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

    def poker_eval(self, game, pockets, board=None, dead=None, iterations=0):
        """Évalue l'état du jeu de poker basé sur différentes variantes et calcule l'EV."""
        if game not in ["holdem", "holdem8", "omaha", "omaha8", "7stud", "7stud8", "razz"]:
            raise ValueError(f"Unsupported game type: {game}")

        if board is None:
            board = []
        if dead is None:
            dead = []

        # Remplissage des poches pour Monte Carlo, si nécessaire
        filled_pockets = self.fill_pockets(pockets) if iterations > 0 else pockets
        results = []
        
        # Initialisation des compteurs
        win_counts_hi = [0] * len(filled_pockets)
        win_counts_lo = [0] * len(filled_pockets)
        tie_counts = [0] * len(filled_pockets)
        loss_counts = [0] * len(filled_pockets)

        # Gérer les mains vides
        for i, pocket in enumerate(filled_pockets):
            if all(card == '__' or card == 255 for card in pocket):
                # Si la main est vide, la classer immédiatement comme perdante
                loss_counts[i] = 1
                continue

        # Simulation Monte Carlo ou évaluation directe
        if iterations > 0:
            # Simulation Monte Carlo
            for _ in range(iterations):
                filled_pocket = self.fill_pockets(pockets)
                eval_results_hi = [self.eval_hand(game, 'hi', pocket, board) for pocket in filled_pocket]
                eval_results_lo = [self.eval_hand(game, 'low', pocket, board) for pocket in filled_pocket]
                
                best_value_hi = max(eval_results_hi, key=lambda x: x[0])[0]
                best_value_lo = min(eval_results_lo, key=lambda x: x[0])[0]

                # Comparer les valeurs de mains et calculer win/tie/loss
                for i, (result_hi, result_lo) in enumerate(zip(eval_results_hi, eval_results_lo)):
                    if result_hi[0] == best_value_hi:
                        win_counts_hi[i] += 1
                    if result_lo[0] == best_value_lo:
                        win_counts_lo[i] += 1
                    else:
                        loss_counts[i] += 1
            total = sum(win_counts_hi) + sum(loss_counts)
        else:
            # Énumération exhaustive
            eval_results_hi = [self.eval_hand(game, 'hi', pocket, board) for pocket in filled_pockets]
            eval_results_lo = [self.eval_hand(game, 'low', pocket, board) for pocket in filled_pockets]
            
            best_value_hi = max(eval_results_hi, key=lambda x: x[0])[0]
            best_value_lo = min(eval_results_lo, key=lambda x: x[0])[0]

            # Comparer les valeurs de mains et calculer win/tie/loss
            for i, (result_hi, result_lo) in enumerate(zip(eval_results_hi, eval_results_lo)):
                if result_hi[0] == best_value_hi:
                    win_counts_hi[i] += 1
                if result_lo[0] == best_value_lo:
                    win_counts_lo[i] += 1
                else:
                    loss_counts[i] += 1
            total = len(filled_pockets)

        # Calcul de l'EV en fonction des victoires et des égalités
        ev_values = [win / total for win in win_counts_hi]

        return {
            'info': [iterations, 1 if game.endswith('8') or game == 'razz' else 0, 1],
            'eval': [
                {
                    'scoop': 0,
                    'winhi': win_counts_hi[i],
                    'winlo': win_counts_lo[i],
                    'losehi': loss_counts[i],
                    'tiehi': tie_counts[i],
                    'ev': int(ev_values[i] * 1000)
                }
                for i in range(len(filled_pockets))
            ]
        }





    def fill_pockets(self, pockets):
        """Remplit les poches avec des cartes manquantes pour une simulation Monte Carlo."""
        filled_pockets = []
        for pocket in pockets:
            # Remplir une poche vide par des cartes aléatoires
            if not pocket:
                filled = [self.card2string(self.random_card()), self.card2string(self.random_card())]
            else:
                filled = [self.card2string(self.random_card()) if card == '__' or card == 255 else card for card in pocket]
            filled_pockets.append(filled)
        return filled_pockets

    def random_card(self):
        """Retourne une carte aléatoire du jeu."""
        global is_seed_initialized
        if not is_seed_initialized:
            srand(<unsigned int>time())
            is_seed_initialized = True
        return rand() % StdDeck_N_CARDS

    def winners(self, game, pockets, board=None, fill_pockets=False):
        """Détermine les gagnants parmi plusieurs mains fournies."""
        if board is None:
            board = []
        if fill_pockets:
            pockets = self.fill_pockets(pockets)

        results = []
        for pocket in pockets:
            hi_result = self.eval_hand(game, 'hi', pocket, board)
            low_result = self.eval_hand(game, 'low', pocket, board)
            results.append((hi_result, low_result))

        best_hi_value = max(results, key=lambda x: x[0][0])[0][0]
        best_low_value = min(results, key=lambda x: x[1][0])[1][0]

        winners_hi = [i for i, result in enumerate(results) if result[0][0] == best_hi_value]
        winners_low = [i for i, result in enumerate(results) if result[1][0] == best_low_value]

        return {
            'hi': winners_hi,
            'low': winners_low if best_low_value != 0xFFFFFFFF else []
        }

    def deck(self):
        """Retourne la liste de toutes les cartes du jeu."""
        return [self.card2string(i) for i in range(StdDeck_N_CARDS)]

    def nocard(self):
        """Retourne la valeur de la carte joker ou placeholder."""
        return 255
