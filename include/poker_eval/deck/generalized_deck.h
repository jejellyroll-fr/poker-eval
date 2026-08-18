/*
 * generalized_deck.h - Generalized deck specification abstraction
 *
 * ISSUE-03 (#159): Generalized deck specification abstraction beyond the
 * hardcoded standard/joker pair in UniversalDeck. This module describes any
 * deck with up to 64 cards via a dynamic pe_deck_spec_t descriptor and offers
 * the corresponding 64-bit card mask operations and string conversions. It is
 * strictly additive and does not modify the existing StdDeck/JokerDeck/Universal
 * APIs.
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
#ifndef __GENERALIZED_DECK_H__
#define __GENERALIZED_DECK_H__

#include <stddef.h>
#include <stdint.h>
#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 64-bit card mask; encodes any deck of up to PE_DECK_MAX_CARDS cards. */
typedef uint64_t pe_card_mask_t;

/* The 64-bit mask cannot represent more than 64 cards. */
#define PE_DECK_MAX_CARDS 64
#define PE_DECK_MAX_SUITS 4

/* Preset names for pe_deck_get_predefined(). */
#define PE_DECK_PRESET_ROYAL      "royal_20"
#define PE_DECK_PRESET_SPANISH    "spanish_32"
#define PE_DECK_PRESET_SHORT      "short_36"
#define PE_DECK_PRESET_STD        "std_52"
#define PE_DECK_PRESET_JOKER_53   "joker_53"
#define PE_DECK_PRESET_JOKER_54   "joker_54"
#define PE_DECK_PRESET_CALIFORNIA "california_60"

/*
 * Dynamic deck descriptor. Cards are indexed [0, num_cards); the first
 * num_suits * num_ranks indices encode (suit, rank) pairs and the remaining
 * num_jokers indices encode extra (joker/filler) cards.
 */
typedef struct {
  int num_cards;
  int num_ranks;
  int num_suits;
  uint16_t active_rank_mask; /* Bit 0 = rank 2 ... Bit 12 = Ace */
  int num_jokers;            /* Number of extra (joker/filler) cards */
  char deck_name[32];
} pe_deck_spec_t;

/*
 * Deck construction.
 * Both functions return 0 on success and -1 on failure (invalid arguments or
 * a deck that exceeds PE_DECK_MAX_CARDS).
 */
POKEREVAL_EXPORT int pe_deck_create_custom(int min_rank, int max_rank,
                                           int num_suits, int num_jokers,
                                           pe_deck_spec_t *out_spec);
POKEREVAL_EXPORT int pe_deck_get_predefined(const char *preset_name,
                                            pe_deck_spec_t *out_spec);

/*
 * Bitmask operations. Set/unset/is_set ignore out-of-range card indices.
 * pe_deck_mask_count returns the number of set bits (== popcount of the mask
 * for any deck, since bits beyond num_cards are always zero).
 */
POKEREVAL_EXPORT pe_card_mask_t pe_deck_mask_full(const pe_deck_spec_t *spec);
POKEREVAL_EXPORT void pe_deck_mask_set(const pe_deck_spec_t *spec,
                                       pe_card_mask_t *mask, int card);
POKEREVAL_EXPORT void pe_deck_mask_unset(const pe_deck_spec_t *spec,
                                         pe_card_mask_t *mask, int card);
POKEREVAL_EXPORT int pe_deck_mask_is_set(const pe_deck_spec_t *spec,
                                         pe_card_mask_t mask, int card);
POKEREVAL_EXPORT int pe_deck_mask_count(const pe_deck_spec_t *spec,
                                        pe_card_mask_t mask);

/*
 * String conversions.
 * pe_deck_card_to_string writes the rank+suit of `card` (or "Xx" for a joker)
 * into buf and returns the number of characters written, or -1 on failure.
 * pe_deck_string_to_card parses a 2-character card string and returns 2 on
 * success, 0 on no match.
 * pe_deck_mask_to_string writes a space-separated list of the cards in `mask`
 * into buf and returns the number of characters written, or -1 if the output
 * does not fit in buf_len (buf is always NUL-terminated when it is used).
 */
POKEREVAL_EXPORT int pe_deck_card_to_string(const pe_deck_spec_t *spec,
                                            int card, char *buf,
                                            size_t buf_len);
POKEREVAL_EXPORT int pe_deck_string_to_card(const pe_deck_spec_t *spec,
                                            const char *str, int *card);
POKEREVAL_EXPORT int pe_deck_mask_to_string(const pe_deck_spec_t *spec,
                                            pe_card_mask_t mask, char *buf,
                                            size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* __GENERALIZED_DECK_H__ */
