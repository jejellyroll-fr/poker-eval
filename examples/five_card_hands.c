/*
 *  five_card_hands.c: Enumerate and tabulate five-card hands
 *
 *  Copyright (C) 1993-99 Clifford T. Matthews, Brian Goetz
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "poker_defs.h"

// Utiliser getopt_w32.h pour Windows et getopt.h pour les autres systèmes
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__BCPLUSPLUS__) || defined(__MWERKS__)
#include "getopt_w32.h"
#else
#include <getopt.h>
#endif

#include "inlines/eval.h"

// Définir VERBOSE pour obtenir un affichage détaillé, sinon commenter
// #define VERBOSE

uint32 totals[HandType_LAST + 1];
int num_dead;
CardMask dead_cards;

const char *opts = "d:";

/*
 * Parse les arguments de la ligne de commande.
 * Retourne le nombre de cartes mortes, ou -1 en cas d'erreur.
 */
static int parse_args(int argc, char **argv, int *num_dead, CardMask *dead_cards) {
    int i, c, o, rc, len;

    if (num_dead == NULL || dead_cards == NULL) {
        return -1;
    }

    *num_dead = 0;
    CardMask_RESET(*dead_cards);

    while ((o = getopt(argc, argv, opts)) != -1) {
        switch (o) {
            case 'd':
                len = strlen(optarg);
                for (i = 0; i < len;) {
                    rc = StdDeck_stringToCard(optarg + i, &c);
                    if (rc) {
                        StdDeck_CardMask_SET(*dead_cards, c);
                        (*num_dead)++;
                        i += 2; // Passer à la carte suivante
                    } else {
                        i++;
                    }
                }
                break;
            default:
                return -1; // Option non reconnue
        }
    }

    return *num_dead;
}

void dump_totals(void) {
    int i;
    for (i = HandType_FIRST; i <= HandType_LAST; i++) {
        printf("%s: %d\n", handTypeNamesPadded[i], totals[i]);
    }
}

#ifdef VERBOSE
#define DUMP_HAND do { \
    Deck_printMask(cards); \
    printf(": "); \
    HandVal_print(handval); \
    printf("\n"); \
} while (0)
#else
#define DUMP_HAND do { } while (0)
#endif

int main(int argc, char *argv[]) {
    CardMask cards;
    HandVal handval;

    if (parse_args(argc, argv, &num_dead, &dead_cards) == -1) {
        fprintf(stderr, "Erreur lors de l'analyse des arguments.\n");
        exit(EXIT_FAILURE);
    }

    ENUMERATE_5_CARDS_D(cards, dead_cards, {
        handval = Hand_EVAL_N(cards, 5);
        ++totals[HandVal_HANDTYPE(handval)];
        DUMP_HAND;
    });

    dump_totals();
    exit(EXIT_SUCCESS);
}
