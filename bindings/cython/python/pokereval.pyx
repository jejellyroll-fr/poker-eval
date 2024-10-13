# pokereval.pyx

# cython: language_level=3, boundscheck=False, wraparound=False, cdivision=True
from libc.stdlib cimport malloc, free
from libc.string cimport memset, memcpy
from cpython cimport bool
from itertools import combinations
import logging
import random  # Utiliser le module random de Python
from libc.stdint cimport uint32_t, uint64_t

# Configurer le logging
logging.basicConfig(level=logging.DEBUG)

# Importation des types et fonctions externes depuis les en-têtes C


cdef extern from "poker_defs.h":
    ctypedef unsigned int HandVal
    ctypedef unsigned int LowHandVal

cdef extern from "inlines/eval_omaha.h":
    int StdDeck_OmahaHiLow8_EVAL(StdDeck_CardMask hole, StdDeck_CardMask board, HandVal *hival, LowHandVal *loval)
    int StdDeck_OmahaHi_EVAL(StdDeck_CardMask hole, StdDeck_CardMask board, HandVal *hival)


cdef extern from "handval_low.h":
    cdef int LowHandVal_TOP_CARD_SHIFT
    cdef int LowHandVal_CARD_MASK
    cdef int LowHandVal_SECOND_CARD_SHIFT
    cdef int LowHandVal_THIRD_CARD_SHIFT
    cdef int LowHandVal_FOURTH_CARD_SHIFT
    cdef int LowHandVal_FIFTH_CARD_SHIFT
    cdef int LowHandVal_HANDTYPE_SHIFT
    cdef int LowHandVal_HANDTYPE_MASK
    const unsigned int LowHandVal_NOTHING

cdef extern from "handval.h":
    cdef int HandVal_TOP_CARD_SHIFT
    cdef int HandVal_CARD_MASK
    cdef int HandVal_SECOND_CARD_SHIFT
    cdef int HandVal_THIRD_CARD_SHIFT
    cdef int HandVal_FOURTH_CARD_SHIFT
    cdef int HandVal_FIFTH_CARD_SHIFT
    cdef int HandVal_HANDTYPE_SHIFT
    cdef int HandVal_HANDTYPE_MASK
    const unsigned int HandVal_NOTHING

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

cdef extern from "rules_std.h":
    int StdRules_HandVal_print(HandVal handval)
    int StdRules_HandVal_toString(HandVal handval, char* outString)


# Définir une structure pour les résultats d'évaluation
cdef struct EvalResult:
    HandVal handval
    StdDeck_CardMask combined_mask

# Implémentation du Générateur Aléatoire en Cython en utilisant le module random de Python
cdef class MT19937:
    def __cinit__(self):
        pass  # Aucune initialisation nécessaire avec le module random de Python

    cpdef void seed(self, int seed_value):
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

    cpdef reset_seed(self, int seed):
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
        cdef char card_cstr[3]

        # Vérifier la longueur de la chaîne de carte
        if len(card) != 2:
            if card == '__':
                logging.debug(f"Carte placeholder: {card} -> 255")
                return 255
            else:
                logging.error(f"Carte invalide par longueur: {card}")
                raise ValueError(f"Carte invalide : {card}")

        # Convertir les deux caractères en majuscule
        card_cstr[0] = card[0].upper().encode('ascii')[0]
        card_cstr[1] = card[1].upper().encode('ascii')[0]
        card_cstr[2] = 0  # Null terminator

        logging.debug(f"Conversion de carte: {card} en C-string: {card_cstr.decode('ascii')}")

        # Appeler la fonction C pour convertir la chaîne en numéro de carte
        StdDeck_stringToCard(card_cstr, &card_num)
        logging.debug(f"StdDeck_stringToCard('{card_cstr.decode('ascii')}', &card_num) = {card_num}")

        # Vérifier si card_num est valide
        if 0 <= card_num < StdDeck_N_CARDS:
            logging.debug(f"Carte convertie: {card} -> {card_num}")
            return card_num
        else:
            logging.error(f"StdDeck_stringToCard a échoué pour la carte: {card}")
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



    def best_hand(self, game, side, hand, board=None, include_description=False):
        """Retourne la meilleure main en termes de cartes (combinaison), incluant les mains hautes et basses."""
        
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

        # Déterminer si nous devons évaluer une main basse (low)
        is_low = (side == 'low')

        # Utiliser la méthode spécifique pour Omaha si nécessaire
        if game in ["omaha", "omaha8"]:
            if len(hand) != 4:
                raise ValueError(f"Omaha nécessite exactement 4 cartes en main, mais {len(hand)} ont été fournies.")
            result = self.eval_omaha_hand_cdef(game, side, hand, board)
        else:
            # Sinon, utiliser la méthode d'évaluation exhaustive pour les autres jeux
            result = self.eval_best_hand_exhaustive_cdef(game, side, hand, board)

        # Retourner les détails de la main en fonction du type high/low
        return self._get_hand_details(result.handval, &result.combined_mask, include_description=include_description, low=is_low)



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
                    # Copier les masques actuels dans best_mask
                    memcpy(&best_mask, &current_mask, sizeof(StdDeck_CardMask))
            elif side == 'low':
                if game == 'razz' or game == 'lowball27':
                    current_val = py_Hand_EVAL_LOW(&current_mask, 5)
                else:
                    current_val = py_Hand_EVAL_LOW8(&current_mask, 5)
                if current_val < best_val:
                    best_val = current_val
                    # Copier les masques actuels dans best_mask
                    memcpy(&best_mask, &current_mask, sizeof(StdDeck_CardMask))
        
        # Retourner la meilleure main trouvée
        cdef EvalResult result
        result.handval = best_val
        result.combined_mask = best_mask
        return result



    cdef EvalResult eval_omaha_hand_cdef(self, str game, str side, list hand, list board):
        """Évalue une main Omaha spécifique et retourne les résultats dans une structure C."""
        cdef EvalResult best_result
        cdef HandVal best_val
        cdef HandVal current_val
        cdef LowHandVal current_lo_val
        cdef int res
        cdef tuple hand_combo
        cdef tuple board_combo
        cdef tuple full_combo
        cdef list all_cards = hand + board
        cdef list best_combo = []

        # Construire les masques complets pour la main et le tableau
        cdef StdDeck_CardMask hole_mask
        cdef StdDeck_CardMask board_mask
        cdef StdDeck_CardMask best_mask, current_mask
        py_StdDeck_CardMask_RESET(&hole_mask)
        py_StdDeck_CardMask_RESET(&board_mask)
        py_StdDeck_CardMask_RESET(&best_mask)
        cdef int card_num

        # Convertir les cartes de la main en masque
        for card in hand:
            card_num = self._string2card(card)
            if card_num != 255:
                py_StdDeck_CardMask_SET(&hole_mask, card_num)

        # Convertir les cartes du tableau en masque
        for card in board:
            card_num = self._string2card(card)
            if card_num != 255:
                py_StdDeck_CardMask_SET(&board_mask, card_num)

        logging.debug(f"Masque main complet: {hole_mask}, Masque board complet: {board_mask}")

        # Appeler les fonctions C pour évaluer la main
        if side == 'hi':
            res = StdDeck_OmahaHi_EVAL(hole_mask, board_mask, &current_val)
            if res != 0:
                logging.warning(f"Évaluation hi échouée avec res={res} pour hole_mask={hand} et board_mask={board}")
                best_result.handval = HandVal_NOTHING
                return best_result
            best_val = current_val
        elif side == 'low':
            res = StdDeck_OmahaHiLow8_EVAL(hole_mask, board_mask, NULL, &current_lo_val)
            if res != 0:
                logging.warning(f"Évaluation low échouée avec res={res} pour hole_mask={hand} et board_mask={board}")
                best_result.handval = LowHandVal_NOTHING
                return best_result
            best_val = current_lo_val
        else:
            raise ValueError("Le paramètre 'side' doit être 'hi' ou 'low'.")

        # Déterminer les meilleures combinaisons de deux cartes de la main et trois du tableau
        for hand_combo in combinations(hand, 2):
            for board_combo in combinations(board, 3):
                full_combo = hand_combo + board_combo  # Assignment sans 'cdef'

                # Créer un masque pour la combinaison actuelle
                py_StdDeck_CardMask_RESET(&current_mask)
                for card in full_combo:
                    card_num = self._string2card(card)
                    if card_num != 255:
                        py_StdDeck_CardMask_SET(&current_mask, card_num)

                # Évaluer la combinaison
                if side == 'hi':
                    current_val = py_Hand_EVAL_N(&current_mask, 5)
                    if current_val == best_val:
                        best_combo = list(full_combo)  # Conversion en liste
                        break
                elif side == 'low':
                    if game == 'razz' or game == 'lowball27':
                        current_val = py_Hand_EVAL_LOW(&current_mask, 5)
                    else:
                        current_val = py_Hand_EVAL_LOW8(&current_mask, 5)
                    if current_val == best_val:
                        best_combo = list(full_combo)  # Conversion en liste
                        break
            if best_combo:
                break  # Sortir des boucles si une combinaison est trouvée

        if not best_combo:
            logging.warning("Aucune combinaison de deux cartes de la main et trois du tableau ne correspond à la meilleure valeur évaluée.")
            best_result.handval = HandVal_NOTHING if side == 'hi' else LowHandVal_NOTHING
            return best_result

        # Remplir best_mask avec les cartes de best_combo
        py_StdDeck_CardMask_RESET(&best_mask)
        for card in best_combo:
            card_num = self._string2card(card)
            if card_num != 255:
                py_StdDeck_CardMask_SET(&best_mask, card_num)

        # Stocker les résultats dans la structure EvalResult
        best_result.handval = best_val
        # Copier le best_mask dans combined_mask pour ne garder que les 5 meilleures cartes
        memcpy(&best_result.combined_mask, &best_mask, sizeof(StdDeck_CardMask))
        logging.debug(f"Main évaluée: handval={best_val}, combined_mask={self._cardmask_to_sorted_list(&best_result.combined_mask)}")

        return best_result







    cpdef int rank_card(self, str card):
        """Retourne l'ordre de tri basé sur le rang d'une carte."""
        rank_order = {
            '2': 2, '3': 3, '4': 4, '5': 5, '6': 6, '7': 7, '8': 8, '9': 9,
            'T': 10, 'J': 11, 'Q': 12, 'K': 13, 'A': 14
        }
        # Le premier caractère de `card` représente le rang
        rank = card[0].upper()  # Convertir le rang en majuscule
        return rank_order.get(rank, 0)




    cdef list _cardmask_to_sorted_list(self, StdDeck_CardMask* mask):
        """Convertit un StdDeck_CardMask en une liste de chaînes de caractères représentant les cartes triées."""
        cdef list cards = []
        cdef int i
        cdef char card_str[16]

        for i in range(StdDeck_N_CARDS):
            if py_StdDeck_CardMask_CARD_IS_SET(mask, i):
                StdDeck_cardToString(i, card_str)
                cards.append(card_str.decode('utf-8'))

        # Assurez-vous qu'il y a exactement 5 cartes
        if len(cards) != 5:
            logging.warning(f"Nombre de cartes extraites : {len(cards)} (attendu 5)")
            # Optionnel : Vous pouvez gérer cela en levant une exception ou en ajustant la liste
            # raise ValueError(f"Nombre de cartes extraites incorrect : {len(cards)} au lieu de 5")

        # Tri des cartes par rang (ordre décroissant)
        sorted_cards = sorted(cards, key=lambda card: self.rank_card(card), reverse=True)
        
        logging.debug(f"Cartes extraites et triées du masque : {sorted_cards}")
        
        return sorted_cards





    def eval_hand(self, game, side, hand, board):
        """Évalue une main spécifique et retourne les résultats sous forme de liste Python."""
        cdef EvalResult result = self.eval_hand_cdef(game, side, hand, board)
        cdef list hand_details = self._get_hand_details(result.handval, &result.combined_mask, include_description=True, low=(side == 'low'))
        return [result.handval, hand_details]

    cdef list _get_hand_details(self, HandVal handval, StdDeck_CardMask* hand_mask, bint include_description=False, bint low=False):
        """Récupère les détails de la main pour l'affichage, en tenant compte du type high/low."""
        
        cdef list sorted_cards = []
        cdef char hand_str[80]

        # Utilisation de StdRules_HandVal_toString pour obtenir la meilleure main sous forme de chaîne
        StdRules_HandVal_toString(handval, hand_str)
        hand_description = hand_str.decode('utf-8')
        
        logging.debug(f"Main évaluée (avec cartes) : {hand_description}")
        
        # Extraire les cartes du masque StdDeck_CardMask triées par ordre de rang
        sorted_cards = self._cardmask_to_sorted_list(hand_mask)
        
        if not sorted_cards:
            logging.warning("Le masque de cartes est vide ou l'extraction des cartes a échoué.")

        if include_description:
            # Retourner la chaîne complète, qui inclut le type de main et les cartes
            return [hand_description] + sorted_cards

        return sorted_cards




    cdef list _cardmask_to_list(self, StdDeck_CardMask* mask):
        """Convertit un StdDeck_CardMask en une liste de chaînes de caractères représentant les cartes."""
        cdef list cards = []
        cdef int i
        cdef char card_str[16]

        for i in range(StdDeck_N_CARDS):
            if py_StdDeck_CardMask_CARD_IS_SET(mask, i):
                StdDeck_cardToString(i, card_str)
                cards.append(card_str.decode('utf-8'))
        
        logging.debug(f"Cartes extraites du masque : {cards}")
        
        return cards



    def hand_type(self, HandVal handval):
        """Décrit la force d'une main."""
        hand_types = [
            "NoPair", "OnePair", "TwoPair", "Trips", "Straight", "Flush", "FullHouse", "Quads", "StFlush"
        ]

        # Extraire le type de la main haute
        hand_type_value = (handval >> HandVal_HANDTYPE_SHIFT) & HandVal_HANDTYPE_MASK
        if 0 <= hand_type_value < len(hand_types):
            return hand_types[hand_type_value]
        else:
            return "Unknown"

    def low_hand_type(self, LowHandVal handval):
        """Décrit la force d'une main basse."""
        low_hand_types = [
            "NoPair", "OnePair", "TwoPair", "Trips", "Straight", "Flush", "FullHouse", "Quads", "StFlush"
        ]
        
        # Extraire le type de main basse
        low_hand_type_value = (handval >> LowHandVal_HANDTYPE_SHIFT) & LowHandVal_HANDTYPE_MASK
        if 0 <= low_hand_type_value < len(low_hand_types):
            return low_hand_types[low_hand_type_value]
        else:
            return "Unknown"

    def poker_eval(self, game, pockets, board=None, dead=None, iterations=0, return_distributed=False, seed=-1):
        """Évalue l'état du jeu de poker basé sur différentes variantes et calcule l'EV."""
        """
        Parameters:
            game (str): Type de jeu de poker (e.g., "holdem", "7stud", "omaha8", etc.).
            pockets (list): Liste des mains des joueurs, chaque main étant une liste de cartes.
            board (list, optional): Liste des cartes sur le tableau. Par défaut, vide.
            dead (list, optional): Liste des cartes mortes. Par défaut, vide.
            iterations (int, optional): Nombre d'itérations pour la simulation Monte Carlo. Par défaut, 0 (évaluation exhaustive).
            return_distributed (bool, optional): Si True, retourne les cartes distribuées. Par défaut, False.
            seed (int, optional): Graine pour le générateur aléatoire. Par défaut, -1 (aléatoire).

        Returns:
            dict: Résultats de l'évaluation comprenant les statistiques de victoire, EV, etc.
        """
        # Définir les jeux supportant les cartes mortes
        games_supporting_dead = ["7stud", "7stud8", "7studnsq", "razz", "lowball", "lowball27", "ace_to_five_lowball"]

        # Liste des jeux ne supportant pas les cartes mortes
        games_not_supporting_dead = ["holdem", "holdem8", "omaha", "omaha8", "5draw", "5draw8", "5drawnsq"]

        # Vérifier si le jeu supporte les cartes mortes
        if game in games_not_supporting_dead and dead:
            raise ValueError(f"Le jeu {game} ne supporte pas les cartes mortes.")

        if board is None:
            board = []
        if dead is None:
            dead = []

        # Définir le nombre requis de cartes par poche en fonction du jeu
        game_hole_cards = {
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

        # Déclarations des variables
        cdef int num_players = len(pockets)
        cdef set all_distributed = set()
        cdef int i, j  # Variables pour les boucles
        cdef int total_samples = iterations if iterations > 0 else 1
        cdef double pot_fraction
        cdef double hipot = 1.0
        cdef double lopot = 0.0
        cdef list losehi_results = [0] * num_players
        cdef list winhi = [0] * num_players
        cdef list losehi = losehi_results[:]  # Copier la liste
        cdef list tiehi = [0] * num_players
        cdef list winlo = [0] * num_players
        cdef list loselo = [0] * num_players
        cdef list tielo = [0] * num_players
        cdef list scoop = [0] * num_players
        cdef list ev = [0.0] * num_players
        cdef list distributed_cards_players = [[] for _ in range(num_players)]  # Initialiser chaque élément
        cdef list distributed_cards_board = []  # Liste pour les cartes du tableau
        cdef list filled_board
        cdef list filled_pockets
        cdef list eval_results_hi
        cdef list eval_results_lo
        cdef HandVal best_value_hi_local
        cdef LowHandVal best_value_lo_local
        cdef int card_num
        cdef dict result_dict

        # Réinitialiser la seed aléatoire pour la simulation Monte Carlo
        self.reset_seed(seed)
        logging.debug(f"Seed set to: {seed}")

        # Réinitialiser le deck
        self.reset_deck()
        logging.debug(f"Deck après réinitialisation dans poker_eval: {self.available_cards}")

        # Ajouter les cartes mortes à all_distributed
        for card in dead:
            if card != '__' and card != 255:
                try:
                    all_distributed.add(self._string2card(card))
                except ValueError:
                    logging.warning(f"Carte morte invalide ignorée: {card}")

        # Ajouter les cartes connues du tableau à all_distributed
        for card in board:
            if card != '__' and card != 255:
                try:
                    all_distributed.add(self._string2card(card))
                except ValueError:
                    logging.warning(f"Carte du tableau invalide ignorée: {card}")

        # Ajouter les cartes connues des pockets à all_distributed
        for pocket in pockets:
            for card in pocket:
                if card != '__' and card != 255:
                    try:
                        all_distributed.add(self._string2card(card))
                    except ValueError:
                        logging.warning(f"Carte de poche invalide ignorée: {card}")

        # Vérifier les mains vides et les traiter comme perdantes
        for i in range(num_players):
            pocket = pockets[i]
            for j in range(len(pocket)):
                if pocket[j] == "__" or pocket[j] == 255:
                    losehi_results[i] = total_samples  # Considérer la main vide comme perdante
                    break

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
            best_hi_value = max(result[0] for result in eval_results_hi)

            # Identifier les gagnants pour la main haute
            winners_hi = [i for i, result in enumerate(eval_results_hi) if result[0] == best_hi_value]

            # Trouver la meilleure valeur basse
            if has_low:
                best_lo_value = min(result[0] for result in eval_results_lo if result is not None)
                winners_low = [i for i, result in enumerate(eval_results_lo) if result and result[0] == best_lo_value]
            else:
                winners_low = []

            # Comptage des résultats
            for i in range(num_players):
                # Si la main a été marquée comme perdante, passer
                if losehi_results[i] == total_samples:
                    continue

                # Main haute
                if eval_results_hi[i][0] == best_hi_value:
                    if len(winners_hi) == 1:
                        winhi[i] += 1
                    else:
                        tiehi[i] += 1
                else:
                    losehi[i] += 1

                # Main basse
                if has_low:
                    if eval_results_lo[i] and eval_results_lo[i][0] == best_lo_value:
                        if len(winners_low) == 1:
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
                    if eval_results_hi[i][0] == best_hi_value:
                        hi_share = hipot / len(winners_hi)
                        pot_fraction += hi_share

                    # Part du pot basse
                    if has_low and eval_results_lo[i] and eval_results_lo[i][0] == best_lo_value:
                        lo_share = lopot / len(winners_low)
                        pot_fraction += lo_share

                ev[i] += pot_fraction

                # Comptage des scoops
                if has_low:
                    if (eval_results_hi[i][0] == best_hi_value and
                        eval_results_lo[i] and eval_results_lo[i][0] == best_lo_value):
                        if len(winners_hi) == 1 and len(winners_low) == 1:
                            scoop[i] += 1

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
        filled_pockets_int = self.fill_pockets_cdef(pockets, expected_hole_cards, already_distributed)
        # Convertir les numéros de cartes en chaînes
        filled_pockets_str = [self.card2string(pocket) for pocket in filled_pockets_int]
        return filled_pockets_str

    cpdef list fill_pockets_cdef(self, list pockets, int expected_cards_per_pocket=2, set already_distributed=None):
        if already_distributed is None:
            already_distributed = set()
        cdef list filled_pockets = []
        cdef int selected_card
        cdef list filled
        cdef int j
        cdef int card_num

        for pocket in pockets:
            filled = []
            for j in range(len(pocket)):
                card = pocket[j]
                if card == '__' or card == '255' or card == 255:
                    if not self.available_cards:
                        self.reset_deck()
                    selected_card = self.random_card()
                    filled.append(selected_card)  # Append as int
                    already_distributed.add(selected_card)
                else:
                    try:
                        if isinstance(card, str):
                            card_num = self._string2card(card)
                        elif isinstance(card, int):
                            card_num = card
                        else:
                            raise TypeError(f"Invalid card type in pocket: {card}")
                        filled.append(card_num)
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
                filled.append(selected_card)  # Append as int
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



    def winners(self, game, pockets, board=None, dead=None, fill_pockets=False, return_distributed=False):
        """Détermine les gagnants parmi plusieurs mains fournies, en tenant compte des cartes mortes si nécessaire."""
        cdef int total_samples = 1  # Par défaut, une seule évaluation
        cdef list games_supporting_dead = ["7stud", "7stud8", "7studnsq", "razz", "lowball", "lowball27", "ace_to_five_lowball"]
        cdef list games_not_supporting_dead = ["holdem", "holdem8", "omaha", "omaha8", "5draw", "5draw8", "5drawnsq"]
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
        cdef int expected_hole_cards = game_hole_cards.get(game, -1)
        if expected_hole_cards == -1:
            raise ValueError(f"Jeu non supporté : {game}")

        cdef bint has_low = game.endswith('8') or game in ["razz", "lowball", "ace_to_five_lowball", "lowball27"]
        cdef int i, j
        cdef list pocket
        cdef HandVal best_hi_value = 0
        cdef LowHandVal best_lo_value = 0xFFFFFFFF  # Initialize to max for low
        cdef list winners_hi = []
        cdef list winners_low = []
        cdef set all_distributed = set()
        cdef list losehi_results = [0] * len(pockets)
        cdef list winhi = [0] * len(pockets)
        cdef list losehi = losehi_results[:]  # Copier la liste
        cdef list tiehi = [0] * len(pockets)
        cdef list winlo = [0] * len(pockets)
        cdef list loselo = [0] * len(pockets)
        cdef list tielo = [0] * len(pockets)
        cdef list scoop = [0] * len(pockets)
        cdef list ev = [0.0] * len(pockets)
        cdef list distributed_cards_players = [[] for _ in range(len(pockets))]  # Initialiser chaque élément
        cdef list distributed_cards_board = []  # Liste pour les cartes du tableau
        cdef list filled_board
        cdef list filled_pockets
        cdef list eval_results_hi
        cdef list eval_results_lo
        cdef int card_num
        cdef dict result_dict
        cdef double pot_fraction
        cdef double hipot = 1.0
        cdef double lopot = 0.0

        # Réinitialiser la seed aléatoire pour la simulation Monte Carlo
        self.reset_seed(-1)  # Vous pouvez ajuster la graine si nécessaire
        logging.debug(f"Seed set to: {-1}")

        # Réinitialiser le deck
        self.reset_deck()
        logging.debug(f"Deck après réinitialisation dans poker_eval: {self.available_cards}")

        if dead is None:
            dead = []  # Remplacer dead=None par une liste vide
        # Ajouter les cartes mortes à all_distributed
        for card in dead:
            if card != '__' and card != 255:
                try:
                    if isinstance(card, str):
                        card_num = self._string2card(card)
                    elif isinstance(card, int):
                        card_num = card
                    else:
                        raise TypeError(f"Invalid card type in dead: {card}")
                    all_distributed.add(card_num)
                except ValueError:
                    logging.warning(f"Carte morte invalide ignorée: {card}")

        # Ajouter les cartes connues du tableau à all_distributed
        for card in board:
            if card != '__' and card != 255:
                try:
                    if isinstance(card, str):
                        card_num = self._string2card(card)
                    elif isinstance(card, int):
                        card_num = card
                    else:
                        raise TypeError(f"Invalid card type in board: {card}")
                    all_distributed.add(card_num)
                except ValueError:
                    logging.warning(f"Carte du tableau invalide ignorée: {card}")

        # Ajouter les cartes connues des pockets à all_distributed
        for pocket in pockets:
            for card in pocket:
                if card != '__' and card != 255:
                    try:
                        if isinstance(card, str):
                            card_num = self._string2card(card)
                        elif isinstance(card, int):
                            card_num = card
                        else:
                            raise TypeError(f"Invalid card type in pocket: {card}")
                        all_distributed.add(card_num)
                    except ValueError:
                        logging.warning(f"Carte de poche invalide ignorée: {card}")

        # Vérifier les mains vides et les traiter comme perdantes
        for i in range(len(pockets)):
            pocket = pockets[i]
            for j in range(len(pocket)):
                if pocket[j] == "__" or pocket[j] == 255:
                    losehi_results[i] = total_samples  # Considérer la main vide comme perdante
                    break

        # Simulation Monte Carlo ou évaluation exhaustive
        for sample in range(total_samples):
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
                eval_results_lo = [None] * len(pockets)

            # Trouver la meilleure valeur haute
            best_hi_value = max(result[0] for result in eval_results_hi)

            # Identifier les gagnants pour la main haute
            winners_hi = [i for i, result in enumerate(eval_results_hi) if result[0] == best_hi_value]

            # Trouver la meilleure valeur basse
            if has_low:
                best_lo_value = min(result[0] for result in eval_results_lo if result is not None)
                winners_low = [i for i, result in enumerate(eval_results_lo) if result and result[0] == best_lo_value]
            else:
                winners_low = []

            # Comptage des résultats
            for i in range(len(pockets)):
                # Si la main a été marquée comme perdante, passer
                if losehi_results[i] == total_samples:
                    continue

                # Main haute
                if eval_results_hi[i][0] == best_hi_value:
                    if len(winners_hi) == 1:
                        winhi[i] += 1
                    else:
                        tiehi[i] += 1
                else:
                    losehi[i] += 1

                # Main basse
                if has_low:
                    if eval_results_lo[i] and eval_results_lo[i][0] == best_lo_value:
                        if len(winners_low) == 1:
                            winlo[i] += 1
                        else:
                            tielo[i] += 1
                    else:
                        loselo[i] += 1

                # Calculer l'EV
                pot_fraction = 0.0
                if has_low:
                    hipot = 1.0
                    lopot = 1.0

                    # Part du pot haute
                    if eval_results_hi[i][0] == best_hi_value:
                        hi_share = hipot / len(winners_hi)
                        pot_fraction += hi_share

                    # Part du pot basse
                    if has_low and eval_results_lo[i] and eval_results_lo[i][0] == best_lo_value:
                        lo_share = lopot / len(winners_low)
                        pot_fraction += lo_share

                ev[i] += pot_fraction

                # Comptage des scoops
                if has_low:
                    if (eval_results_hi[i][0] == best_hi_value and
                        eval_results_lo[i] and eval_results_lo[i][0] == best_lo_value):
                        if len(winners_hi) == 1 and len(winners_low) == 1:
                            scoop[i] += 1

        # Calculer les moyennes sur le total des échantillons
        for i in range(len(pockets)):
            ev[i] = ev[i] / total_samples

        # Préparer le résultat
        result_dict = {}
        result_dict['info'] = [total_samples, int(has_low), 1]
        result_dict['eval'] = []
        for i in range(len(pockets)):
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
        filled_pockets_int = self.fill_pockets_cdef(pockets, expected_hole_cards, already_distributed)
        # Convertir les numéros de cartes en chaînes
        filled_pockets_str = [self.card2string(pocket) for pocket in filled_pockets_int]
        return filled_pockets_str



    def get_random_card(self):
        """Returns a random card from the deck that hasn't been distributed yet."""
        return self.random_card()



    cpdef int random_card(self):
        """Retourne une carte aléatoire du deck qui n'a pas encore été distribuée."""
        if not self.available_cards:
            raise ValueError("Plus de cartes disponibles pour la distribution.")
        cdef int upper_bound = len(self.available_cards)
        cdef int selected_index = self.rng.rand_int(upper_bound)
        cdef int selected_card = self.available_cards.pop(selected_index)
        card_str = self.card2string(selected_card)
        logging.debug(f"Random index: {selected_index}, selected_card: {selected_card} ({card_str})")
        return selected_card

    def winners(self, game, pockets, board=None, dead=None, fill_pockets=False, return_distributed=False):
        """Détermine les gagnants parmi plusieurs mains fournies, en tenant compte des cartes mortes si nécessaire."""
        # Cette méthode est similaire à `poker_eval` et peut être optimisée
        # Pour l'instant, je vais me concentrer sur la correction des erreurs actuelles
        pass

    def deck(self):
        """Retourne la liste de toutes les cartes du jeu."""
        return [self.card2string(i) for i in range(StdDeck_N_CARDS)]

    def nocard(self):
        """Retourne la valeur de la carte joker ou placeholder."""
        return 255
