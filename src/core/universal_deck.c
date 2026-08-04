/*
 * universal_deck.c - Implementation of universal deck functions
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

#include <ctype.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/universal_deck.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/deck/deck_std.h>
#include <string.h>

/* Convert StdDeck_CardMask to JokerDeck_CardMask */
void Universal_ConvertStdToJoker(StdDeck_CardMask std,
                                 JokerDeck_CardMask *joker) {
  JokerDeck_CardMask_fromStd(*joker, std);
  /* Joker bit is not set when converting from standard deck */
}

/* Convert JokerDeck_CardMask to StdDeck_CardMask */
void Universal_ConvertJokerToStd(JokerDeck_CardMask joker,
                                 StdDeck_CardMask *std) {
  JokerDeck_CardMask_toStd(joker, *std);
  /* Note: This loses the joker if present */
}

/* Reset a universal card mask */
void Universal_CardMask_RESET(UniversalCardMask *mask, deck_type_t type) {
  if (type == UNIVERSAL_DECK_STANDARD) {
    StdDeck_CardMask_RESET(mask->std);
  } else {
    JokerDeck_CardMask_RESET(mask->joker);
  }
}

/* Set a card in universal mask */
void Universal_CardMask_SET(UniversalCardMask *mask, int card,
                            deck_type_t type) {
  if (type == UNIVERSAL_DECK_STANDARD) {
    StdDeck_CardMask_SET(mask->std, card);
  } else {
    JokerDeck_CardMask_SET(mask->joker, card);
  }
}

/* Check if card is set in universal mask */
int Universal_CardMask_CARD_IS_SET(UniversalCardMask mask, int card,
                                   deck_type_t type) {
  if (type == UNIVERSAL_DECK_STANDARD) {
    return StdDeck_CardMask_CARD_IS_SET(mask.std, card);
  } else {
    return JokerDeck_CardMask_CARD_IS_SET(mask.joker, card);
  }
}

/* OR operation on universal masks */
void Universal_CardMask_OR(UniversalCardMask *result, UniversalCardMask op1,
                           UniversalCardMask op2, deck_type_t type) {
  if (type == UNIVERSAL_DECK_STANDARD) {
    StdDeck_CardMask_OR(result->std, op1.std, op2.std);
  } else {
    JokerDeck_CardMask_OR(result->joker, op1.joker, op2.joker);
  }
}

/* Parse string to card, detecting joker */
int Universal_StringToCard(const char *str, int *card, deck_type_t *type) {
  /* Check for joker notation */
  if ((str[0] == 'X' || str[0] == 'x') && (str[1] == 'x' || str[1] == 'X')) {
    *card = JokerDeck_JOKER;
    *type = UNIVERSAL_DECK_JOKER;
    return 2; /* consumed 2 characters */
  }

  /* Try standard deck parsing - create a mutable copy */
  char temp[3];
  temp[0] = str[0];
  temp[1] = str[1];
  temp[2] = '\0';

  int result = StdDeck_stringToCard(temp, card);
  if (result > 0) {
    *type = UNIVERSAL_DECK_STANDARD;
  }
  return result;
}

/* Convert card to string */
int Universal_CardToString(int card, char *str, deck_type_t type) {
  if (type == UNIVERSAL_DECK_JOKER && card == JokerDeck_JOKER) {
    str[0] = 'X';
    str[1] = 'x';
    str[2] = '\0';
    return 1;
  } else if (type == UNIVERSAL_DECK_STANDARD) {
    return StdDeck_cardToString(card, str);
  } else {
    return JokerDeck_cardToString(card, str);
  }
}

/* Count cards in universal mask */
int Universal_numCards(UniversalCardMask mask, deck_type_t type) {
  if (type == UNIVERSAL_DECK_STANDARD) {
    return StdDeck_numCards(mask.std);
  } else {
    return JokerDeck_numCards(mask.joker);
  }
}

/* Determine required deck type for a game */
deck_type_t Universal_DetermineRequiredDeckType(enum_game_t game) {
  switch (game) {
  case game_5draw:
  case game_5draw8:
  case game_5drawnsq:
  case game_lowball:
    return UNIVERSAL_DECK_JOKER;
  case game_holdem:
  case game_holdem8:
  case game_omaha:
  case game_omaha5:
  case game_omaha6:
  case game_omaha8:
  case game_omaha85:
  case game_omaha86:
  case game_7stud:
  case game_7stud8:
  case game_7studnsq:
  case game_razz:
  case game_lowball27:
  case game_sdholdem:
  case game_doubleflop_holdem:
  case game_drawmaha:
  case game_pineapple:
  case game_pineapple8:
  case game_27_triple_draw:
  case game_a5_triple_draw:
  case game_badacey:
  case game_badeucy:
  case game_badugi:
  case game_fusion:
  case game_courchevel:
  case game_courchevel8:
  case game_irish:
  case game_ofc:
  case game_manila:
  case game_NUMGAMES:
  default:
    return UNIVERSAL_DECK_STANDARD;
  }
}

/* Check if game uses joker */
int Universal_IsJokerGame(enum_game_t game) {
  return Universal_DetermineRequiredDeckType(game) == UNIVERSAL_DECK_JOKER;
}
