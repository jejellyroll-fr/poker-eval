/*
 * universal_deck.h - Universal deck types to support both StdDeck and JokerDeck
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

#ifndef __UNIVERSAL_DECK_H__
#define __UNIVERSAL_DECK_H__

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/generalized_deck.h>
#include <poker_eval/core/pokereval_export.h>

/* Deck type enumeration */
typedef enum {
  UNIVERSAL_DECK_STANDARD,
  UNIVERSAL_DECK_JOKER
} deck_type_t;

/* Universal card mask that can hold either StdDeck or JokerDeck */
typedef union {
  StdDeck_CardMask std;
  JokerDeck_CardMask joker;
  pe_card_mask_t cards; /* Alias of the same 64 bits under USE_INT64 */
} UniversalCardMask;

/* Universal deck context */
typedef struct {
  deck_type_t type;
  int hasJoker;  /* Flag to indicate if joker is present in the hand */
} UniversalDeckContext;

/* Conversion functions */
POKEREVAL_EXPORT void Universal_ConvertStdToJoker(StdDeck_CardMask std, JokerDeck_CardMask *joker);
POKEREVAL_EXPORT void Universal_ConvertJokerToStd(JokerDeck_CardMask joker, StdDeck_CardMask *std);

/* Card mask operations */
POKEREVAL_EXPORT void Universal_CardMask_RESET(UniversalCardMask *mask, deck_type_t type);
POKEREVAL_EXPORT void Universal_CardMask_SET(UniversalCardMask *mask, int card, deck_type_t type);
POKEREVAL_EXPORT int Universal_CardMask_CARD_IS_SET(UniversalCardMask mask, int card, deck_type_t type);
POKEREVAL_EXPORT void Universal_CardMask_OR(UniversalCardMask *result, UniversalCardMask op1, UniversalCardMask op2, deck_type_t type);

/* String conversion */
POKEREVAL_EXPORT int Universal_StringToCard(const char *str, int *card, deck_type_t *type);
POKEREVAL_EXPORT int Universal_CardToString(int card, char *str, deck_type_t type);

/* Utility functions */
POKEREVAL_EXPORT int Universal_numCards(UniversalCardMask mask, deck_type_t type);

/* Game-related functions - only declare if enum_game_t is available */
#ifdef ENUMDEFS_H
POKEREVAL_EXPORT deck_type_t Universal_DetermineRequiredDeckType(enum_game_t game);
POKEREVAL_EXPORT int Universal_IsJokerGame(enum_game_t game);
#endif

/* Macros for easy access */
#define UMASK_RESET(mask, type) Universal_CardMask_RESET(&(mask), (type))
#define UMASK_SET(mask, card, type) Universal_CardMask_SET(&(mask), (card), (type))
#define UMASK_IS_SET(mask, card, type) Universal_CardMask_CARD_IS_SET((mask), (card), (type))
#define UMASK_OR(result, op1, op2, type) Universal_CardMask_OR(&(result), (op1), (op2), (type))

#endif /* __UNIVERSAL_DECK_H__ */
