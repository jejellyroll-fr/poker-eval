/*
 * generalized_deck.c - Implementation of the generalized deck abstraction.
 *
 * ISSUE-03 (#159): a dynamic pe_deck_spec_t descriptor replaces the hardcoded
 * standard/joker pair for decks of up to 64 cards. This module is strictly
 * additive; it does not modify the existing StdDeck/JokerDeck/Universal APIs.
 *
 * Card index layout: the first num_suits * num_ranks indices encode
 * (suit, rank) pairs with suit = index / num_ranks and
 * rank = pe_deck_min_rank + (index % num_ranks); the remaining num_jokers
 * indices are the extra (joker/filler) cards.
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
#include <string.h>
#include <poker_eval/deck/generalized_deck.h>

/* Rank (index 0 = rank 2 ... index 12 = Ace) and suit display tables. */
static const char pe_rank_chars[13] = {'2', '3', '4', '5', '6',
                                       '7', '8', '9', 'T', 'J',
                                       'Q', 'K', 'A'};
static const char pe_suit_chars[4] = {'h', 'd', 'c', 's'};

/* Copy a bounded name into a deck spec, always NUL-terminating the array. */
static void pe_spec_set_name(pe_deck_spec_t *spec, const char *name) {
  size_t i;
  for (i = 0; i < sizeof(spec->deck_name) - 1 && name[i] != '\0'; i++) {
    spec->deck_name[i] = name[i];
  }
  spec->deck_name[i] = '\0';
}

/* Lowest set bit of active_rank_mask interpreted as a rank index (0..12). */
static int pe_deck_min_rank(const pe_deck_spec_t *spec) {
  int r;
  for (r = 0; r < 13; r++) {
    if (spec->active_rank_mask & (uint16_t)((uint16_t)1u << r)) {
      return r;
    }
  }
  return 0;
}

/* "Xx" / "xX" is the canonical joker notation (matches UniversalDeck). */
static int pe_deck_is_joker_text(const char *s) {
  if (s[0] != 'X' && s[0] != 'x') {
    return 0;
  }
  return s[1] == 'X' || s[1] == 'x';
}

/* Core builder shared by pe_deck_create_custom() and pe_deck_get_predefined(). */
static int pe_deck_build(int min_rank, int max_rank, int num_suits,
                         int num_jokers, const char *name,
                         pe_deck_spec_t *out_spec) {
  int i;
  uint16_t rank_mask;

  if (out_spec == NULL) {
    return -1;
  }
  if (num_suits < 1 || num_suits > PE_DECK_MAX_SUITS) {
    return -1;
  }
  if (num_jokers < 0) {
    return -1;
  }
  if (min_rank < 0 || max_rank < min_rank || max_rank > 12) {
    return -1;
  }

  rank_mask = 0;
  for (i = min_rank; i <= max_rank; i++) {
    rank_mask = (uint16_t)(rank_mask | (uint16_t)((uint16_t)1u << i));
  }

  out_spec->active_rank_mask = rank_mask;
  out_spec->num_ranks = max_rank - min_rank + 1;
  out_spec->num_suits = num_suits;
  out_spec->num_jokers = num_jokers;
  out_spec->num_cards = num_suits * out_spec->num_ranks + num_jokers;
  if (out_spec->num_cards < 1 || out_spec->num_cards > PE_DECK_MAX_CARDS) {
    return -1;
  }

  pe_spec_set_name(out_spec, name);
  return 0;
}

int pe_deck_create_custom(int min_rank, int max_rank, int num_suits,
                          int num_jokers, pe_deck_spec_t *out_spec) {
  return pe_deck_build(min_rank, max_rank, num_suits, num_jokers, "custom",
                       out_spec);
}

/* Preset table: name, min_rank, max_rank, num_suits, num_jokers. */
typedef struct {
  const char *name;
  int min_rank;
  int max_rank;
  int num_suits;
  int num_jokers;
} pe_preset_entry_t;

static const pe_preset_entry_t pe_presets[] = {
    {PE_DECK_PRESET_ROYAL,      8, 12, 4, 0}, /* T..A * 4 suits   = 20  */
    {PE_DECK_PRESET_SPANISH,    5, 12, 4, 0}, /* 7..A * 4 suits   = 32  */
    {PE_DECK_PRESET_SHORT,      4, 12, 4, 0}, /* 6..A * 4 suits   = 36  */
    {PE_DECK_PRESET_STD,        0, 12, 4, 0}, /* 2..A * 4 suits   = 52  */
    {PE_DECK_PRESET_JOKER_53,   0, 12, 4, 1}, /* 52 + 1 joker     = 53  */
    {PE_DECK_PRESET_JOKER_54,   0, 12, 4, 2}, /* 52 + 2 jokers    = 54  */
    {PE_DECK_PRESET_CALIFORNIA, 0, 12, 4, 8}, /* 52 + 8 extras    = 60  */
};

int pe_deck_get_predefined(const char *preset_name, pe_deck_spec_t *out_spec) {
  size_t i;
  if (preset_name == NULL || out_spec == NULL) {
    return -1;
  }
  for (i = 0; i < sizeof(pe_presets) / sizeof(pe_presets[0]); i++) {
    if (strcmp(preset_name, pe_presets[i].name) == 0) {
      return pe_deck_build(pe_presets[i].min_rank, pe_presets[i].max_rank,
                           pe_presets[i].num_suits, pe_presets[i].num_jokers,
                           pe_presets[i].name, out_spec);
    }
  }
  return -1;
}

pe_card_mask_t pe_deck_mask_full(const pe_deck_spec_t *spec) {
  if (spec == NULL) {
    return 0;
  }
  if (spec->num_cards >= PE_DECK_MAX_CARDS) {
    return (pe_card_mask_t)~((pe_card_mask_t)0);
  }
  return ((pe_card_mask_t)1 << spec->num_cards) - (pe_card_mask_t)1;
}

void pe_deck_mask_set(const pe_deck_spec_t *spec, pe_card_mask_t *mask,
                      int card) {
  if (spec == NULL || mask == NULL || card < 0 || card >= spec->num_cards) {
    return;
  }
  *mask |= (pe_card_mask_t)1 << card;
}

void pe_deck_mask_unset(const pe_deck_spec_t *spec, pe_card_mask_t *mask,
                        int card) {
  if (spec == NULL || mask == NULL || card < 0 || card >= spec->num_cards) {
    return;
  }
  *mask &= ~((pe_card_mask_t)1 << card);
}

int pe_deck_mask_is_set(const pe_deck_spec_t *spec, pe_card_mask_t mask,
                        int card) {
  if (spec == NULL || card < 0 || card >= spec->num_cards) {
    return 0;
  }
  return (mask & ((pe_card_mask_t)1 << card)) != 0;
}

int pe_deck_mask_count(const pe_deck_spec_t *spec, pe_card_mask_t mask) {
  pe_card_mask_t v;
  int n;
  if (spec == NULL) {
    return 0;
  }
  v = mask;
  n = 0;
  while (v != 0) {
    v &= v - 1;
    n++;
  }
  return n;
}

int pe_deck_card_to_string(const pe_deck_spec_t *spec, int card, char *buf,
                           size_t buf_len) {
  int min_rank;
  size_t w;
  if (spec == NULL || buf == NULL || buf_len == 0) {
    return -1;
  }
  if (card < 0 || card >= spec->num_cards) {
    return -1;
  }
  min_rank = pe_deck_min_rank(spec);

  if (card >= spec->num_suits * spec->num_ranks) {
    buf[0] = 'X';
    buf[1] = 'x';
    w = 2;
  } else {
    int rank = min_rank + card % spec->num_ranks;
    int suit = card / spec->num_ranks;
    if (rank < 0 || rank > 12 || suit < 0 || suit > 3) {
      return -1;
    }
    buf[0] = pe_rank_chars[(size_t)rank];
    buf[1] = pe_suit_chars[(size_t)suit];
    w = 2;
  }
  if (w + 1 > buf_len) {
    return -1;
  }
  buf[w] = '\0';
  return (int)w;
}

int pe_deck_string_to_card(const pe_deck_spec_t *spec, const char *str,
                           int *card) {
  int min_rank;
  int r;
  int s;

  if (spec == NULL || str == NULL || card == NULL) {
    return 0;
  }
  if (spec->num_jokers > 0 && pe_deck_is_joker_text(str)) {
    *card = spec->num_suits * spec->num_ranks;
    return 2;
  }

  min_rank = pe_deck_min_rank(spec);
  {
    int ch = (unsigned char)toupper((unsigned char)str[0]);
    for (r = 0; r < 13; r++) {
      if ((spec->active_rank_mask & (uint16_t)((uint16_t)1u << r)) &&
          pe_rank_chars[(size_t)r] == (char)ch) {
        break;
      }
    }
  }
  if (r >= 13) {
    return 0;
  }
  {
    int ch = (unsigned char)tolower((unsigned char)str[1]);
    for (s = 0; s < 4; s++) {
      if (pe_suit_chars[(size_t)s] == (char)ch) {
        break;
      }
    }
  }
  if (s >= 4) {
    return 0;
  }

  *card = s * spec->num_ranks + (r - min_rank);
  return 2;
}

int pe_deck_mask_to_string(const pe_deck_spec_t *spec, pe_card_mask_t mask,
                           char *buf, size_t buf_len) {
  int card;
  size_t pos;

  if (spec == NULL || buf == NULL || buf_len == 0) {
    return -1;
  }
  buf[0] = '\0';
  pos = 0;
  for (card = 0; card < spec->num_cards; card++) {
    if (pe_deck_mask_is_set(spec, mask, card)) {
      char nc[3];
      int w = pe_deck_card_to_string(spec, card, nc, sizeof(nc));
      size_t extra;
      if (w < 0) {
        return -1;
      }
      extra = (pos == 0) ? (size_t)w : (size_t)w + 1;
      if (pos + extra + 1 > buf_len) {
        return -1;
      }
      if (pos != 0) {
        buf[pos++] = ' ';
      }
      {
        size_t i;
        for (i = 0; i < (size_t)w; i++) {
          buf[pos + i] = nc[i];
        }
        pos += (size_t)w;
      }
    }
  }
  buf[pos] = '\0';
  return (int)pos;
}
