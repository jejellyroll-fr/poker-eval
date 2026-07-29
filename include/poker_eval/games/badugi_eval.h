/*
 * Copyright (C) 2024
 *           Poker-eval contributors
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

#ifndef __BADUGI_EVAL_H__
#define __BADUGI_EVAL_H__

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/handval_low.h>
#include <poker_eval/games/rules_badugi.h>
#include <poker_eval/core/pokereval_export.h>

/* Badugi hand value type */
typedef uint32 BadugiHandVal;

/* Badugi hand value macros - similar to HandVal but optimized for 4 cards */
#define BadugiHandVal_HANDTYPE_SHIFT 28
#define BadugiHandVal_HANDTYPE_MASK 0xF0000000
#define BadugiHandVal_CARDS_SHIFT 0
#define BadugiHandVal_CARDS_MASK 0x0FFFFFFF

#define BadugiHandVal_FIRST_CARD_SHIFT 21
#define BadugiHandVal_FIRST_CARD_MASK 0x01E00000
#define BadugiHandVal_SECOND_CARD_SHIFT 17
#define BadugiHandVal_SECOND_CARD_MASK 0x001E0000
#define BadugiHandVal_THIRD_CARD_SHIFT 13
#define BadugiHandVal_THIRD_CARD_MASK 0x0001E000
#define BadugiHandVal_FOURTH_CARD_SHIFT 9
#define BadugiHandVal_FOURTH_CARD_MASK 0x00001E00

#define BadugiHandVal_CARD_WIDTH 4
#define BadugiHandVal_CARD_MASK 0x0000000F

/* Accessor macros */
#define BadugiHandVal_HANDTYPE(hv) (((hv) & BadugiHandVal_HANDTYPE_MASK) >> BadugiHandVal_HANDTYPE_SHIFT)
#define BadugiHandVal_CARDS(hv) ((hv) & BadugiHandVal_CARDS_MASK)
#define BadugiHandVal_FIRST_CARD(hv) (((hv) & BadugiHandVal_FIRST_CARD_MASK) >> BadugiHandVal_FIRST_CARD_SHIFT)
#define BadugiHandVal_SECOND_CARD(hv) (((hv) & BadugiHandVal_SECOND_CARD_MASK) >> BadugiHandVal_SECOND_CARD_SHIFT)
#define BadugiHandVal_THIRD_CARD(hv) (((hv) & BadugiHandVal_THIRD_CARD_MASK) >> BadugiHandVal_THIRD_CARD_SHIFT)
#define BadugiHandVal_FOURTH_CARD(hv) (((hv) & BadugiHandVal_FOURTH_CARD_MASK) >> BadugiHandVal_FOURTH_CARD_SHIFT)

/* Value construction macros */
#define BadugiHandVal_HANDTYPE_VALUE(ht) ((ht) << BadugiHandVal_HANDTYPE_SHIFT)
#define BadugiHandVal_FIRST_CARD_VALUE(c) ((c) << BadugiHandVal_FIRST_CARD_SHIFT)
#define BadugiHandVal_SECOND_CARD_VALUE(c) ((c) << BadugiHandVal_SECOND_CARD_SHIFT)
#define BadugiHandVal_THIRD_CARD_VALUE(c) ((c) << BadugiHandVal_THIRD_CARD_SHIFT)
#define BadugiHandVal_FOURTH_CARD_VALUE(c) ((c) << BadugiHandVal_FOURTH_CARD_SHIFT)

/* Special values */
#define BadugiHandVal_NOTHING 0xFFFFFFFF
#define BadugiHandVal_WORST_BADUGI (BadugiHandVal_HANDTYPE_VALUE(BadugiRules_HandType_BADUGI) | \
                                    BadugiHandVal_FIRST_CARD_VALUE(StdDeck_Rank_KING) |         \
                                    BadugiHandVal_SECOND_CARD_VALUE(StdDeck_Rank_QUEEN) |       \
                                    BadugiHandVal_THIRD_CARD_VALUE(StdDeck_Rank_JACK) |         \
                                    BadugiHandVal_FOURTH_CARD_VALUE(StdDeck_Rank_TEN))

/* Badugi evaluation functions */
extern POKEREVAL_EXPORT BadugiHandVal StdDeck_BadugiRules_EVAL_N(StdDeck_CardMask cards, int n_cards);
extern POKEREVAL_EXPORT BadugiHandVal StdDeck_BadugiRules_EVAL_4(StdDeck_CardMask cards);
extern POKEREVAL_EXPORT BadugiHandVal StdDeck_BadugiRules_EVAL_5(StdDeck_CardMask cards);

/* Badacey evaluation functions (Badugi + A-5 lowball) */
extern POKEREVAL_EXPORT HandVal StdDeck_BadaceyRules_EVAL_N(StdDeck_CardMask cards, int n_cards);
extern POKEREVAL_EXPORT HandVal StdDeck_BadaceyRules_EVAL_5(StdDeck_CardMask cards);

/* Badeucy evaluation functions (Badugi + 2-7 lowball) */
extern POKEREVAL_EXPORT HandVal StdDeck_BadeucyRules_EVAL_N(StdDeck_CardMask cards, int n_cards);
extern POKEREVAL_EXPORT HandVal StdDeck_BadeucyRules_EVAL_5(StdDeck_CardMask cards);

/* Helper functions */
extern POKEREVAL_EXPORT int BadugiEval_GetBestBadugi(StdDeck_CardMask cards, int n_cards,
                                                     StdDeck_CardMask *best_badugi, int *badugi_size);
extern POKEREVAL_EXPORT int BadugiEval_RemoveDuplicates(StdDeck_CardMask cards, int n_cards,
                                                        StdDeck_CardMask *unique_cards);
extern POKEREVAL_EXPORT int BadugiEval_CountUniqueSuits(StdDeck_CardMask cards, int n_cards);
extern POKEREVAL_EXPORT int BadugiEval_CountUniqueRanks(StdDeck_CardMask cards, int n_cards);

/* Utility functions for string representation */
/* Bounded variant; prefer it. size includes the terminator. */
extern POKEREVAL_EXPORT int BadugiHandVal_toString_n(BadugiHandVal hv, char *outString, size_t size);
/* Requires a buffer of at least POKER_EVAL_HANDVAL_STRING_MAX bytes. */
extern POKEREVAL_EXPORT int BadugiHandVal_toString(BadugiHandVal hv, char *outString);
extern POKEREVAL_EXPORT int BadugiHandVal_print(BadugiHandVal hv);

#endif /* __BADUGI_EVAL_H__ */
