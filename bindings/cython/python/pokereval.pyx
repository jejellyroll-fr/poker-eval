# cython: language_level=3
from libc.stdlib cimport rand, srand
from time import time

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

# Importation des wrappers
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
        if StdDeck_stringToCard(card_cstr, &card_num) == 0:
            if card == '__' or card == '255':
                return 255  # Placeholder
            else:
                raise ValueError(f"Carte invalide : {card}")
        return card_num

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

    def best_hand(self, side, hand, board=None):
        """Retourne la meilleure main en termes de cartes (combinaison)."""
        if board is None:
            board = []
        if len(hand) + len(board) < 5:
            return False
        cdef EvalResult result = self.eval_hand_cdef(side, hand, board)
        return self._get_hand_details(result.handval, &result.combined_mask, include_description=False)

    cdef EvalResult eval_hand_cdef(self, side, hand, board):
        """Évalue une main spécifique et retourne les résultats dans une structure C."""
        cdef StdDeck_CardMask hand_mask, board_mask, combined_mask
        cdef EvalResult result
        cdef int card_num

        py_StdDeck_CardMask_RESET(&hand_mask)
        py_StdDeck_CardMask_RESET(&board_mask)
        py_StdDeck_CardMask_RESET(&combined_mask)

        for card in hand:
            if isinstance(card, str):
                card_num = self._string2card(card)
            else:
                card_num = card
            if card_num != 255:
                py_StdDeck_CardMask_SET(&hand_mask, card_num)

        for card in board:
            if isinstance(card, str):
                card_num = self._string2card(card)
            else:
                card_num = card
            if card_num != 255:
                py_StdDeck_CardMask_SET(&board_mask, card_num)

        py_StdDeck_CardMask_OR(&combined_mask, &hand_mask, &board_mask)

        if side == 'hi':
            result.handval = py_Hand_EVAL_N(&combined_mask, len(hand) + len(board))
        elif side == 'low':
            result.handval = py_Hand_EVAL_LOW8(&combined_mask, len(hand) + len(board))
        else:
            raise ValueError("Le paramètre 'side' doit être 'hi' ou 'low'.")

        result.combined_mask = combined_mask
        return result

    def eval_hand(self, side, hand, board):
        """Évalue une main spécifique et retourne les résultats sous forme de liste Python."""
        cdef EvalResult result = self.eval_hand_cdef(side, hand, board)
        if side == 'hi':
            return [result.handval, result.combined_mask]
        elif side == 'low':
            return [result.handval, result.combined_mask]
        else:
            raise ValueError("Le paramètre 'side' doit être 'hi' ou 'low'.")

    cdef list _get_hand_details(self, HandVal handval, StdDeck_CardMask* hand_mask, bint include_description=True, bint low=False):
        """Récupère les détails de la main pour l'affichage."""
        all_cards = self.card2string(self._cardmask_to_list(hand_mask))
        if include_description:
            hand_description = self.hand_type(handval)
            if len(all_cards) > 5:
                sorted_cards = sorted(all_cards, key=lambda card: self._string2card(card), reverse=True)
                return [hand_description] + sorted_cards[:5]
            return [hand_description] + all_cards
        else:
            if len(all_cards) > 5:
                sorted_cards = sorted(all_cards, key=lambda card: self._string2card(card), reverse=True)
                return sorted_cards[:5]
            return all_cards

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

        filled_pockets = self.fill_pockets(pockets) if iterations > 0 else pockets
        results = []
        
        win_counts = [0] * len(filled_pockets)  # Comptes des victoires pour chaque main
        tie_counts = [0] * len(filled_pockets)  # Comptes des égalités
        loss_counts = [0] * len(filled_pockets)  # Comptes des défaites

        if iterations > 0:
            # Monte Carlo simulation
            for _ in range(iterations):
                filled_pocket = self.fill_pockets(pockets)
                eval_results = [self.eval_hand('hi', pocket, board) for pocket in filled_pocket]
                best_value = max(eval_results, key=lambda x: x[0])[0]

                # Comparer les valeurs de mains et calculer win/tie/loss
                for i, result in enumerate(eval_results):
                    if result[0] == best_value:
                        win_counts[i] += 1
                    else:
                        loss_counts[i] += 1
            total = sum(win_counts) + sum(loss_counts)
        else:
            # Énumération exhaustive (méthode par défaut)
            eval_results = [self.eval_hand('hi', pocket, board) for pocket in filled_pockets]
            best_value = max(eval_results, key=lambda x: x[0])[0]

            # Comparer les valeurs de mains et calculer win/tie/loss
            for i, result in enumerate(eval_results):
                if result[0] == best_value:
                    win_counts[i] += 1
                else:
                    loss_counts[i] += 1
            total = len(filled_pockets)

        # Calcul de l'EV en fonction des victoires et des égalités
        ev_values = [win / total for win in win_counts]

        return {
            'info': [iterations, 1 if game.endswith('8') or game == 'razz' else 0, 1],
            'eval': [
                {'scoop': 0, 'winhi': win_counts[i], 'losehi': loss_counts[i], 'tiehi': tie_counts[i], 'ev': ev_values[i]}
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
        global is_seed_initialized  # On utilise la variable globale
        if not is_seed_initialized:
            srand(<unsigned int>time())  # Initialisation du générateur de nombres pseudo-aléatoires
            is_seed_initialized = True
        return rand() % StdDeck_N_CARDS

    def winners(self, pockets, board=None, fill_pockets=False):
        """Détermine les gagnants parmi plusieurs mains fournies."""
        if board is None:
            board = []
        if fill_pockets:
            pockets = self.fill_pockets(pockets)

        results = []
        for pocket in pockets:
            hi_result = self.eval_hand('hi', pocket, board)
            low_result = self.eval_hand('low', pocket, board)
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
