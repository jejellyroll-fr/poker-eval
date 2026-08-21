/*
 * universal_deck.c - Implementation of universal deck functions
 *
 * ISSUE-03 phase 2 (#203): the standard/joker mask operations and string
 * conversions now delegate to the generalized deck layer (pe_deck_spec_t +
 * pe_card_mask_t from generalized_deck). Under USE_INT64 the 64-bit storage of
 * StdDeck/JokerDeck masks and pe_card_mask_t alias the same dense bits, so the
 * delegation is exact. Non-USE_INT64 builds keep the historical per-type
 * branches byte-for-byte, and the ConvertStdToJoker/ConvertJokerToStd helpers
 * keep their existing (bitfield-based) semantics unchanged.
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

/* Resolve the generalized deck descriptor backing a deck type. The descriptors
 * are obtained from the generalized_deck presets and cached after first use. */
static const pe_deck_spec_t *pe_universal_spec(deck_type_t type) {
  static pe_deck_spec_t std_spec;
  static pe_deck_spec_t joker_spec;
  static int inited = 0;

  if (!inited) {
    (void)pe_deck_get_predefined(PE_DECK_PRESET_STD, &std_spec);
    (void)pe_deck_get_predefined(PE_DECK_PRESET_JOKER_53, &joker_spec);
    inited = 1;
  }
  return (type == UNIVERSAL_DECK_JOKER) ? &joker_spec : &std_spec;
}

/* Convert StdDeck_CardMask to JokerDeck_CardMask.
 * Joker bit is not set when converting from a standard deck. */
void Universal_ConvertStdToJoker(StdDeck_CardMask std,
                                 JokerDeck_CardMask *joker) {
  JokerDeck_CardMask_fromStd(*joker, std);
}

/* Convert JokerDeck_CardMask to StdDeck_CardMask.
 * Note: This loses the joker if present. */
void Universal_ConvertJokerToStd(JokerDeck_CardMask joker,
                                 StdDeck_CardMask *std) {
  JokerDeck_CardMask_toStd(joker, *std);
}

/* Reset a universal card mask */
void Universal_CardMask_RESET(UniversalCardMask *mask, deck_type_t type) {
#ifdef USE_INT64
  const pe_deck_spec_t *spec = pe_universal_spec(type);
  (void)spec; /* RESET is independent of the deck geometry */
  mask->cards = (pe_card_mask_t)0;
#else
  if (type == UNIVERSAL_DECK_STANDARD) {
    StdDeck_CardMask_RESET(mask->std);
  } else {
    JokerDeck_CardMask_RESET(mask->joker);
  }
#endif
}

/* Set a card in universal mask */
void Universal_CardMask_SET(UniversalCardMask *mask, int card,
                            deck_type_t type) {
#ifdef USE_INT64
  pe_deck_mask_set(pe_universal_spec(type), &mask->cards, card);
#else
  if (type == UNIVERSAL_DECK_STANDARD) {
    StdDeck_CardMask_SET(mask->std, card);
  } else {
    JokerDeck_CardMask_SET(mask->joker, card);
  }
#endif
}

/* Check if card is set in universal mask */
int Universal_CardMask_CARD_IS_SET(UniversalCardMask mask, int card,
                                   deck_type_t type) {
#ifdef USE_INT64
  return pe_deck_mask_is_set(pe_universal_spec(type), mask.cards, card);
#else
  if (type == UNIVERSAL_DECK_STANDARD) {
    return StdDeck_CardMask_CARD_IS_SET(mask.std, card);
  } else {
    return JokerDeck_CardMask_CARD_IS_SET(mask.joker, card);
  }
#endif
}

/* OR operation on universal masks */
void Universal_CardMask_OR(UniversalCardMask *result, UniversalCardMask op1,
                           UniversalCardMask op2, deck_type_t type) {
#ifdef USE_INT64
  const pe_deck_spec_t *spec = pe_universal_spec(type);
  (void)spec; /* OR is independent of the deck geometry */
  result->cards = op1.cards | op2.cards;
#else
  if (type == UNIVERSAL_DECK_STANDARD) {
    StdDeck_CardMask_OR(result->std, op1.std, op2.std);
  } else {
    JokerDeck_CardMask_OR(result->joker, op1.joker, op2.joker);
  }
#endif
}

/* Parse string to card, detecting joker */
int Universal_StringToCard(const char *str, int *card, deck_type_t *type) {
  /* Check for joker notation */
  if ((str[0] == 'X' || str[0] == 'x') && (str[1] == 'x' || str[1] == 'X')) {
    *card = JokerDeck_JOKER;
    *type = UNIVERSAL_DECK_JOKER;
    return 2; /* consumed 2 characters */
  }

  {
    const pe_deck_spec_t *spec = pe_universal_spec(UNIVERSAL_DECK_STANDARD);
    int c;
    int result = pe_deck_string_to_card(spec, str, &c);
    if (result > 0) {
      *card = c;
      *type = UNIVERSAL_DECK_STANDARD;
    }
    return result;
  }
}

/* Convert card to string */
int Universal_CardToString(int card, char *str, deck_type_t type) {
  if (type == UNIVERSAL_DECK_JOKER && card == JokerDeck_JOKER) {
    str[0] = 'X';
    str[1] = 'x';
    str[2] = '\0';
    return 1;
  } else {
    /* Card names are always 2 characters + NUL, matching the legacy writes. */
    return pe_deck_card_to_string(pe_universal_spec(type), card, str, 3u);
  }
}

/* Count cards in universal mask */
int Universal_numCards(UniversalCardMask mask, deck_type_t type) {
#ifdef USE_INT64
  return pe_deck_mask_count(pe_universal_spec(type), mask.cards);
#else
  if (type == UNIVERSAL_DECK_STANDARD) {
    return StdDeck_numCards(mask.std);
  } else {
    return JokerDeck_numCards(mask.joker);
  }
#endif
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
  case game_pineapple_crazy:
  case game_pineapple_lazy:
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
  case game_royal:
  case game_astud:
  case game_italian:
  case game_archie:
  case game_badugi_hilo:
  case game_drawmaha49:
  case game_drawmaha_zero:
  case game_drawmaha_dugi:
  case game_doubleboard_omaha85:
  case game_chinese13:
  case game_NUMGAMES:
  default:
    return UNIVERSAL_DECK_STANDARD;
  }
}

/* Check if game uses joker */
int Universal_IsJokerGame(enum_game_t game) {
  return Universal_DetermineRequiredDeckType(game) == UNIVERSAL_DECK_JOKER;
}
