/*
 * Copyright (C) 2024
 *           poker-eval development team
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

#ifndef RULES_DRAWMAHA_H
#define RULES_DRAWMAHA_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/handval_low.h>
#include <poker_eval/core/pokereval_export.h>
#include <poker_eval/deck/deck_std.h>

/* Drawmaha (also known as Sviten Special) is a split pot game combining:
 * - 5-Card Draw (for the "low" pot - actually best 5-card hand)
 * - Pot-Limit Omaha (for the "high" pot - best Omaha hand)
 *
 * Rules:
 * - Each player receives 5 cards
 * - Betting round, then flop is dealt
 * - Players choose cards to discard (0-5 cards)
 * - Draw phase: receive replacement cards
 * - Turn and river are dealt
 * - At showdown:
 *   * 50% of pot goes to best Omaha hand (2 from hand + 3 from board)
 *   * 50% of pot goes to best Draw hand (all 5 cards from hand)
 */

/* Maximum number of cards that can be drawn in Drawmaha */
#define DRAWMAHA_MAX_DRAW 5

/* Draw state structure for tracking draw decisions */
typedef struct
{
    StdDeck_CardMask original_hand; /* Original 5-card hand */
    StdDeck_CardMask current_hand;  /* Hand after draw */
    int cards_drawn;                /* Number of cards drawn */
    int draw_mask;                  /* Bitmask of which cards were discarded */
} DrawmahaDrawState;

/* Function prototypes */

/* Initialize a draw state */
POKEREVAL_EXPORT int Drawmaha_InitDrawState(DrawmahaDrawState *state,
                                            StdDeck_CardMask initial_hand);

/* Execute a draw (discard and replace cards) */
POKEREVAL_EXPORT int Drawmaha_ExecuteDraw(DrawmahaDrawState *state,
                                          int discard_mask,
                                          StdDeck_CardMask replacement_cards);

/* Find the optimal draw for a given hand and board situation */
POKEREVAL_EXPORT int Drawmaha_FindOptimalDraw(StdDeck_CardMask hand,
                                              StdDeck_CardMask board,
                                              StdDeck_CardMask dead_cards,
                                              int *optimal_discard_mask);

/* Evaluate a Drawmaha hand after optimal draw */
POKEREVAL_EXPORT int Drawmaha_EvaluateOptimal(StdDeck_CardMask hand,
                                              StdDeck_CardMask board,
                                              StdDeck_CardMask dead_cards,
                                              HandVal *hival,
                                              LowHandVal *loval);

/* Evaluate a specific Drawmaha hand configuration
 * hival: Best Omaha hand (2 from hand + 3 from board)
 * loval: Best Draw hand (all 5 cards from hand) - stored as LowHandVal but represents draw strength
 */
POKEREVAL_EXPORT int Drawmaha_EvaluateHand(StdDeck_CardMask hand,
                                           StdDeck_CardMask board,
                                           HandVal *hival,
                                           LowHandVal *loval);

/* Calculate the equity of all possible draws for a hand */
POKEREVAL_EXPORT int Drawmaha_CalculateDrawEquity(StdDeck_CardMask hand,
                                                  StdDeck_CardMask board,
                                                  StdDeck_CardMask dead_cards,
                                                  double *draw_equities);

/* Utility functions */

/* Check if a draw mask is valid (0-5 cards) */
POKEREVAL_EXPORT int Drawmaha_IsValidDrawMask(int draw_mask);

/* Count the number of cards in a draw mask */
POKEREVAL_EXPORT int Drawmaha_CountDrawCards(int draw_mask);

/* Convert a draw mask to card indices */
POKEREVAL_EXPORT int Drawmaha_DrawMaskToIndices(int draw_mask, int *indices);

/* Generate all possible draw masks */
POKEREVAL_EXPORT int Drawmaha_GenerateAllDrawMasks(int *draw_masks, int max_masks);

#endif /* RULES_DRAWMAHA_H */
