/*
 * Copyright (C) 2024
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

#ifndef __TRIPLE_DRAW_H__
#define __TRIPLE_DRAW_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/pokereval_export.h>
#include <poker_eval/deck/deck_std.h>

/* Triple Draw specific structures and functions */

// Structure pour représenter l'état d'une main pendant les draws
typedef struct
{
    StdDeck_CardMask initial_hand; // Main initiale (peut être partielle)
    StdDeck_CardMask current_hand; // Main actuelle après draws
    int draw_round;                // Round de draw actuel (0, 1, 2)
    int cards_discarded[3];        // Nombre de cartes défaussées par round
    int total_cards;               // Nombre total de cartes dans la main
} TripleDrawHandState;

/* Fonctions d'évaluation pour Triple Draw */

/* 2-7 Triple Draw evaluation */
extern POKEREVAL_EXPORT LowHandVal TripleDraw_27_EVAL_N(StdDeck_CardMask cards, int n_cards);

/* A-5 Triple Draw evaluation */
extern POKEREVAL_EXPORT LowHandVal TripleDraw_A5_EVAL_N(StdDeck_CardMask cards, int n_cards);

/* Utility functions for Triple Draw hand management */
extern POKEREVAL_EXPORT void TripleDraw_InitHandState(TripleDrawHandState *state);
extern POKEREVAL_EXPORT int TripleDraw_AddCard(TripleDrawHandState *state, int card);
extern POKEREVAL_EXPORT int TripleDraw_RemoveCard(TripleDrawHandState *state, int card);
extern POKEREVAL_EXPORT int TripleDraw_GetHandSize(const TripleDrawHandState *state);

#endif /* __TRIPLE_DRAW_H__ */
