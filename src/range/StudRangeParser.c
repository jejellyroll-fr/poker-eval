/*
 * Copyright (C) 2025-2026
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

#include <ctype.h>
#include <math.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/range/StudRangeParser.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum number of upcards for multi-street Stud (3rd→7th: 5 upcards) */
#define MAX_STUD_UP_CARDS 5

/* Cards in a full hand: 2 hole cards + the upcards */
#define STUD_MAX_HAND_CARDS (2 + MAX_STUD_UP_CARDS)

/* ============================================================================
 * Internal Helpers
 * ============================================================================
 */

static int stud_char_to_rank(char c) {
  switch (toupper(c)) {
  case '2':
    return StdDeck_Rank_2;
  case '3':
    return StdDeck_Rank_3;
  case '4':
    return StdDeck_Rank_4;
  case '5':
    return StdDeck_Rank_5;
  case '6':
    return StdDeck_Rank_6;
  case '7':
    return StdDeck_Rank_7;
  case '8':
    return StdDeck_Rank_8;
  case '9':
    return StdDeck_Rank_9;
  case 'T':
    return StdDeck_Rank_TEN;
  case 'J':
    return StdDeck_Rank_JACK;
  case 'Q':
    return StdDeck_Rank_QUEEN;
  case 'K':
    return StdDeck_Rank_KING;
  case 'A':
    return StdDeck_Rank_ACE;
  default:
    return -1;
  }
}

static int stud_char_to_suit(char c) {
  switch (tolower(c)) {
  case 'c':
    return StdDeck_Suit_CLUBS;
  case 'd':
    return StdDeck_Suit_DIAMONDS;
  case 'h':
    return StdDeck_Suit_HEARTS;
  case 's':
    return StdDeck_Suit_SPADES;
  default:
    return -1;
  }
}

/* ============================================================================
 * Duplicate Index
 * ============================================================================
 *
 * Hands are deduplicated on insertion. A linear rescan of the range makes that
 * quadratic, which is fine for 3rd-street patterns (a few thousand hands) but
 * not for multi-street ones: "(xx)AKQJT" enumerates over a million 7-card
 * hands. This open-addressing table keyed on the card mask keeps insertion
 * O(1). If it cannot be allocated, lookups fall back to the linear scan.
 */

typedef struct {
  uint64_t key; /* card mask + 1; 0 marks an empty slot */
  size_t hand;  /* index into range->hands */
} stud_index_entry_t;

typedef struct {
  stud_index_entry_t *entries; /* NULL = degraded to linear scan */
  size_t mask;                 /* capacity - 1 (capacity is a power of two) */
  size_t count;
} stud_index_t;

/* Pack the four 13-bit suit fields into a 52-bit key. Done through the public
 * accessors so it holds whether or not the union has a 64-bit member. */
static uint64_t stud_mask_key(StdDeck_CardMask m) {
  return ((uint64_t)StdDeck_CardMask_SPADES(m)) |
         ((uint64_t)StdDeck_CardMask_CLUBS(m) << 13) |
         ((uint64_t)StdDeck_CardMask_DIAMONDS(m) << 26) |
         ((uint64_t)StdDeck_CardMask_HEARTS(m) << 39);
}

/* splitmix64 finalizer — the raw key has very poor low-bit entropy */
static uint64_t stud_hash(uint64_t k) {
  k ^= k >> 30;
  k *= 0xbf58476d1ce4e5b9ULL;
  k ^= k >> 27;
  k *= 0x94d049bb133111ebULL;
  k ^= k >> 31;
  return k;
}

static void stud_index_free(stud_index_t *ix) {
  free(ix->entries);
  ix->entries = NULL;
  ix->mask = 0;
  ix->count = 0;
}

/* Insert into a table known to have a free slot. */
static void stud_index_put(stud_index_t *ix, uint64_t stored_key, size_t hand) {
  size_t i = (size_t)stud_hash(stored_key - 1) & ix->mask;
  while (ix->entries[i].key)
    i = (i + 1) & ix->mask;
  ix->entries[i].key = stored_key;
  ix->entries[i].hand = hand;
}

static int stud_index_grow(stud_index_t *ix, size_t min_count) {
  size_t cap = 1024;
  while (cap < min_count * 2)
    cap <<= 1;

  stud_index_entry_t *entries = calloc(cap, sizeof(*entries));
  if (!entries)
    return 0; /* ix untouched */

  stud_index_entry_t *old = ix->entries;
  size_t old_cap = old ? ix->mask + 1 : 0;
  ix->entries = entries;
  ix->mask = cap - 1;

  for (size_t i = 0; i < old_cap; i++)
    if (old[i].key)
      stud_index_put(ix, old[i].key, old[i].hand);
  free(old);
  return 1;
}

static void stud_index_add(stud_index_t *ix, uint64_t key, size_t hand) {
  if (!ix->entries)
    return;
  /* Keep the load factor under 70% so probe chains stay short. */
  if ((ix->count + 1) * 10 >= (ix->mask + 1) * 7) {
    if (!stud_index_grow(ix, ix->count + 1)) {
      stud_index_free(ix); /* degrade to linear scan rather than overfill */
      return;
    }
  }
  stud_index_put(ix, key + 1, hand);
  ix->count++;
}

/* Seed the index with whatever the range already holds — a range can be shared
 * across several patterns ("(AA)K, (KK)Q") and dedup must span all of them. */
static void stud_index_init(stud_index_t *ix, const arp_range_t *range) {
  ix->entries = NULL;
  ix->mask = 0;
  ix->count = 0;

  /* range->count is only meaningful once the hand array exists. */
  size_t existing = range->hands ? range->count : 0;

  if (!stud_index_grow(ix, existing + 1))
    return;
  for (size_t i = 0; i < existing; i++)
    stud_index_add(ix, stud_mask_key(range->hands[i]), i);
}

/* Returns 1 and sets *out when `hand` is already in the range. */
static int stud_find_hand(const arp_range_t *range, const stud_index_t *ix,
                          uint64_t key, StdDeck_CardMask hand, size_t *out) {
  if (ix->entries) {
    size_t i = (size_t)stud_hash(key) & ix->mask;
    while (ix->entries[i].key) {
      if (ix->entries[i].key == key + 1) {
        *out = ix->entries[i].hand;
        return 1;
      }
      i = (i + 1) & ix->mask;
    }
    return 0;
  }

  for (size_t i = 0; i < range->count; i++) {
    if (StdDeck_CardMask_EQUAL(range->hands[i], hand)) {
      *out = i;
      return 1;
    }
  }
  return 0;
}

static int stud_add_hand_to_range(arp_range_t *range, stud_index_t *ix,
                                  StdDeck_CardMask hand, double weight) {
  if (!range)
    return 0;

  /* Initialize arrays if not already allocated */
  if (!range->hands || !range->weights) {
    size_t initial_capacity = 256;
    range->hands = malloc(initial_capacity * sizeof(StdDeck_CardMask));
    range->weights = malloc(initial_capacity * sizeof(double));
    if (!range->hands || !range->weights) {
      free(range->hands);
      free(range->weights);
      range->hands = NULL;
      range->weights = NULL;
      return 0;
    }
    range->capacity = initial_capacity;
    range->count = 0;
    range->total_weight = 0.0;
    range->has_weights = false;
  }

  if (fabs(weight) < 1e-9)
    return 1;

  /* Check if hand already exists (dedup) */
  uint64_t key = stud_mask_key(hand);
  size_t existing;
  if (stud_find_hand(range, ix, key, hand, &existing)) {
    /* Already present — keep max weight */
    if (weight > range->weights[existing]) {
      range->total_weight += (weight - range->weights[existing]);
      range->weights[existing] = weight;
    }
    return 1;
  }

  /* Expand capacity if needed */
  if (range->count >= range->capacity) {
    size_t new_capacity = range->capacity * 2;
    StdDeck_CardMask *new_hands =
        realloc(range->hands, new_capacity * sizeof(StdDeck_CardMask));
    double *new_weights =
        realloc(range->weights, new_capacity * sizeof(double));
    if (!new_hands || !new_weights) {
      if (new_hands)
        range->hands = new_hands;
      if (new_weights)
        range->weights = new_weights;
      return 0;
    }
    range->hands = new_hands;
    range->weights = new_weights;
    range->capacity = new_capacity;
  }

  range->hands[range->count] = hand;
  range->weights[range->count] = weight;
  stud_index_add(ix, key, range->count);
  range->count++;
  range->total_weight += weight;
  if (fabs(weight - 1.0) > 1e-9)
    range->has_weights = true;

  return 1;
}

/* ============================================================================
 * Hole Card Parsing
 * ============================================================================
 */

/* Hole card type codes */
#define HOLE_SPECIFIC_RANKS 0 /* (AK) — two specific ranks */
#define HOLE_PAIR 1           /* (AA) — pocket pair */
#define HOLE_SUITED 2         /* (ss) — suited wildcard */
#define HOLE_WILD 3           /* (xx) — any two cards */

typedef struct {
  int type;   /* HOLE_* constant */
  int r1, r2; /* ranks (-1 if wildcard) */
  int s1, s2; /* suits (-1 if any) */
} hole_info_t;

/*
 * Parse hole cards: (AA), (AK), (AsKh), (ss), (xx)
 * Returns number of chars consumed, or 0 on error.
 */
static int parse_hole_cards(const char *str, hole_info_t *info) {
  if (str[0] != '(')
    return 0;

  const char *p = str + 1;
  int len = 0;
  while (p[len] && p[len] != ')')
    len++;
  if (!p[len])
    return 0; /* No closing paren */

  info->r1 = info->r2 = -1;
  info->s1 = info->s2 = -1;
  info->type = HOLE_SPECIFIC_RANKS;

  if (len == 2) {
    char c1 = p[0], c2 = p[1];

    /* (ss) — suited wildcard */
    if (tolower(c1) == 's' && tolower(c2) == 's' && stud_char_to_rank(c1) < 0) {
      /* 's' is not a valid rank char, so this is the suited keyword */
      info->type = HOLE_SUITED;
      return 4;
    }

    /* (xx) — any two cards */
    if ((c1 == 'x' || c1 == 'X') && (c2 == 'x' || c2 == 'X')) {
      info->type = HOLE_WILD;
      return 4;
    }

    /* (RR) — two rank chars */
    int rank1 = stud_char_to_rank(c1);
    int rank2 = stud_char_to_rank(c2);
    if (rank1 >= 0 && rank2 >= 0) {
      info->r1 = rank1;
      info->r2 = rank2;
      info->type = (rank1 == rank2) ? HOLE_PAIR : HOLE_SPECIFIC_RANKS;
      return 4;
    }
  }

  /* (RsRs) — specific cards with suits, e.g. (AsKh) */
  if (len == 4) {
    int rank1 = stud_char_to_rank(p[0]);
    int suit1 = stud_char_to_suit(p[1]);
    int rank2 = stud_char_to_rank(p[2]);
    int suit2 = stud_char_to_suit(p[3]);

    if (rank1 >= 0 && suit1 >= 0 && rank2 >= 0 && suit2 >= 0) {
      info->r1 = rank1;
      info->s1 = suit1;
      info->r2 = rank2;
      info->s2 = suit2;
      info->type = (rank1 == rank2) ? HOLE_PAIR : HOLE_SPECIFIC_RANKS;
      return 6;
    }
  }

  return 0;
}

/* ============================================================================
 * Upcard Parsing
 * ============================================================================
 */

typedef struct {
  int rank;         /* -1 = wildcard, >= 0 = specific rank */
  int suit;         /* -1 = any suit, >= 0 = specific suit */
  int suit_binding; /* 1 = upcard must share suit with hole cards (three-flush)
                     */
} upcard_info_t;

/*
 * Parse a single upcard from the front of str.
 * Patterns: K, Ks, x, s (suit-binding suffix)
 * Returns number of chars consumed.
 */
static int parse_single_upcard(const char *str, int hole_type, upcard_info_t *info) {
  info->rank = -1;
  info->suit = -1;
  info->suit_binding = 0;

  if (!str[0])
    return 0;

  int consumed = 0;

  if (tolower(str[0]) == 'x') {
    /* Wildcard upcard — any rank */
    info->rank = -1;
    consumed = 1;
  } else {
    int rank = stud_char_to_rank(str[0]);
    if (rank < 0)
      return 0;
    info->rank = rank;
    consumed = 1;
  }

  /* Check for suit suffix */
  if (str[consumed]) {
    int suit = stud_char_to_suit(str[consumed]);
    if (suit >= 0) {
      info->suit = suit;
      consumed++;

      /* For (ss) hole type, a suit on the upcard means three-flush binding */
      if (hole_type == HOLE_SUITED)
        info->suit_binding = 1;
    }
  }

  return consumed;
}

/*
 * Parse multiple upcards from str. Each upcard is a rank and optional suit.
 * suit_binding only applies to the first upcard (3rd street three-flush).
 * Returns number of upcards parsed, or 0 on error.
 * consumed_out: set to total number of chars consumed if non-NULL.
 */
static int parse_upcards(const char *str, int hole_type,
                         upcard_info_t *upcards, int max_upcards,
                         int *consumed_out) {
  int count = 0;
  int total = 0;
  const char *p = str;

  while (*p && count < max_upcards) {
    upcard_info_t *up = &upcards[count];
    int consumed = parse_single_upcard(p, hole_type, up);
    if (consumed == 0)
      break;
    /* Only first upcard can have three-flush binding */
    if (count > 0)
      up->suit_binding = 0;
    p += consumed;
    total += consumed;
    count++;
  }

  if (consumed_out)
    *consumed_out = total;
  return count;
}

/* ============================================================================
 * Hand Generation — Core Engine
 * ============================================================================
 */

/*
 * Check if card c1 matches hole position 1 constraints AND
 * card c2 matches hole position 2 constraints.
 */
static int match_hole_assignment(const hole_info_t *hole, int r1, int s1,
                                 int r2, int s2) {
  /* Check rank constraints */
  if (hole->r1 >= 0 && r1 != hole->r1)
    return 0;
  if (hole->r2 >= 0 && r2 != hole->r2)
    return 0;
  /* Check suit constraints */
  if (hole->s1 >= 0 && s1 != hole->s1)
    return 0;
  if (hole->s2 >= 0 && s2 != hole->s2)
    return 0;
  return 1;
}

/* Hand generation is handled by generate_hands_multi() below */

/* ============================================================================
 * Multi-Street Hand Generation — Recursive Card Placer
 * ============================================================================
 */

/*
 * Recursively place upcards starting from upcard_idx.
 * cards[0..1] are hole cards, cards[2..] are upcards.
 * At leaf (idx == num_upcards), build the CardMask and add to range.
 */
static int place_upcards_rec(const upcard_info_t *upcards, int num_upcards,
                             int idx, int *cards, int used_count,
                             StdDeck_CardMask dead_cards,
                             arp_range_t *range, stud_index_t *ix,
                             double weight) {
  /* cards[] holds STUD_MAX_HAND_CARDS entries. Callers never exceed that, but
   * stating it explicitly is also what lets GCC bound the indices below once
   * it inlines the recursion (-Werror=array-bounds). */
  if (used_count < 0 || used_count > STUD_MAX_HAND_CARDS)
    return 0;

  if (idx >= num_upcards) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    for (int i = 0; i < used_count; i++)
      StdDeck_CardMask_SET(hand, cards[i]);

    if (StdDeck_CardMask_ANY_SET(dead_cards, hand))
      return 0;

    if (!stud_add_hand_to_range(range, ix, hand, weight))
      return -1;
    return 1;
  }

  /* A further upcard is about to be appended, so a free slot is required. */
  if (used_count >= STUD_MAX_HAND_CARDS)
    return 0;

  const upcard_info_t *up = &upcards[idx];
  int count = 0;

  for (int c = 0; c < 52; c++) {
    /* Skip cards already placed (hole or earlier upcards) */
    int already_used = 0;
    for (int i = 0; i < used_count; i++) {
      if (cards[i] == c) {
        already_used = 1;
        break;
      }
    }
    if (already_used)
      continue;

    int r = StdDeck_RANK(c);
    int s = StdDeck_SUIT(c);

    /* Filter by rank */
    if (up->rank >= 0 && r != up->rank)
      continue;

    /* Filter by suit */
    if (up->suit >= 0 && s != up->suit)
      continue;

    /* Three-flush binding: first upcard's suit must match first hole card */
    if (up->suit_binding && s != StdDeck_SUIT(cards[0]))
      continue;

    cards[used_count] = c;
    int result = place_upcards_rec(upcards, num_upcards, idx + 1,
                                   cards, used_count + 1,
                                   dead_cards, range, ix, weight);
    if (result < 0)
      return -1;
    count += result;
  }

  return count;
}

/*
 * Generate all N-card hands matching hole + upcards constraints.
 * hole: parsed hole card info
 * upcards: array of parsed upcard constraints
 * num_upcards: number of upcards (1–5 for 3rd–7th street)
 */
static int generate_hands_multi(const hole_info_t *hole,
                                const upcard_info_t *upcards,
                                int num_upcards,
                                StdDeck_CardMask dead_cards,
                                arp_range_t *range, stud_index_t *ix,
                                double weight) {
  int count = 0;

  if (num_upcards < 0 || num_upcards > MAX_STUD_UP_CARDS)
    return -1;

  for (int c1 = 0; c1 < 52; c1++) {
    int r1 = StdDeck_RANK(c1);
    int s1 = StdDeck_SUIT(c1);

    /* Quick pre-filter: c1 must match at least one hole position */
    if (hole->type == HOLE_PAIR) {
      if (r1 != hole->r1)
        continue;
      if (hole->s1 >= 0 && s1 != hole->s1)
        continue;
    } else if (hole->type == HOLE_SPECIFIC_RANKS) {
      int match1 =
          (hole->r1 < 0 || r1 == hole->r1) && (hole->s1 < 0 || s1 == hole->s1);
      int match2 =
          (hole->r2 < 0 || r1 == hole->r2) && (hole->s2 < 0 || s1 == hole->s2);
      if (!match1 && !match2)
        continue;
    }

    for (int c2 = c1 + 1; c2 < 52; c2++) {
      int r2 = StdDeck_RANK(c2);
      int s2 = StdDeck_SUIT(c2);

      int hole_ok = 0;
      switch (hole->type) {
      case HOLE_PAIR:
        if (r2 == hole->r2 && (hole->s2 < 0 || s2 == hole->s2))
          hole_ok = 1;
        break;
      case HOLE_SPECIFIC_RANKS:
        if (match_hole_assignment(hole, r1, s1, r2, s2))
          hole_ok = 1;
        if (!hole_ok && match_hole_assignment(hole, r2, s2, r1, s1))
          hole_ok = 1;
        break;
      case HOLE_SUITED:
        if (s1 == s2)
          hole_ok = 1;
        break;
      case HOLE_WILD:
        hole_ok = 1;
        break;
      default:
        break;
      }

      if (!hole_ok)
        continue;

      /* Place hole cards and recurse for upcards */
      int cards[STUD_MAX_HAND_CARDS];
      cards[0] = c1;
      cards[1] = c2;

      int result = place_upcards_rec(upcards, num_upcards, 0,
                                     cards, 2,
                                     dead_cards, range, ix, weight);
      if (result < 0)
        return -1;
      count += result;
    }
  }

  return count;
}

/* ============================================================================
 * Public API: ARP_IsStudPattern
 * ============================================================================
 */

int ARP_IsStudPattern(const char *str, size_t len) {
  if (!str || len < 3)
    return 0;

  /* Case 1: (hole)up... format — variable number of upcards */
  if (str[0] == '(') {
    const char *close_paren = memchr(str, ')', len);
    if (!close_paren)
      return 0;

    /* The hole spec is exactly two chars — (AA), (AK), (ss), (xx) — or four
     * with explicit suits — (AsKh). Anything else, in particular a comma, is a
     * grouped Hold'em range such as "(AA,KK)" and must not be claimed here. */
    size_t inner_len = (size_t)(close_paren - str) - 1;
    if (inner_len != 2 && inner_len != 4)
      return 0;
    for (size_t i = 1; i <= inner_len; i++) {
      char c = str[i];
      if (stud_char_to_rank(c) < 0 && stud_char_to_suit(c) < 0 &&
          tolower(c) != 'x')
        return 0;
    }

    size_t after = (size_t)(close_paren - str) + 1;
    if (after >= len)
      return 0; /* Nothing after ')' */

    /* At least one upcard must follow immediately; an operator or separator
     * right after ')' means the parens were a group, not a Stud hole spec. */
    if (stud_char_to_rank(str[after]) < 0 && tolower(str[after]) != 'x')
      return 0;

    /* All chars after ')' must be rank chars, 'x', or suit chars */
    for (size_t i = after; i < len; i++) {
      char c = str[i];
      if (c == '\0')
        break;
      /* Range/plus notation: check the terminator */
      if (c == '-' || c == '+' || c == ',' || c == '!' ||
          isspace((unsigned char)c))
        break; /* Valid pattern — operator follows */
      if (stud_char_to_rank(c) < 0 && stud_char_to_suit(c) < 0 &&
          tolower(c) != 'x')
        return 0;
    }
    return 1;
  }

  /* Case 2: Triplet format RRR (e.g. AKQ) — 3rd street only */
  if (len >= 3) {
    int r1 = stud_char_to_rank(str[0]);
    int r2 = stud_char_to_rank(str[1]);
    int r3 = stud_char_to_rank(str[2]);

    if (r1 >= 0 && r2 >= 0 && r3 >= 0) {
      /* Next char must be end, separator, or operator */
      if (len == 3)
        return 1;
      char next = str[3];
      if (next == '\0' || next == ',' || next == '+' || next == '-' ||
          next == '!' || isspace((unsigned char)next))
        return 1;
    }
  }

  return 0;
}

/* ============================================================================
 * Public API: ARP_ParseStudPattern
 * ============================================================================
 */

/*
 * Parse a single Stud pattern and generate matching hands.
 *
 * Supported patterns (3rd street):
 *   (AA)K       — pair of aces, king door card
 *   (AK)Q       — ace+king hole, queen door
 *   (AsKh)Qd    — specific suits
 *   (AA)x       — pair of aces, any door card
 *   (ss)Ks      — suited hole cards, king of that suit (three-flush)
 *   (ss)A       — suited hole cards, ace door (any suit)
 *   (xx)A       — any hole cards, ace door
 *   AKQ         — triplet shorthand = (AK)Q
 *   (AA)K-T     — range: (AA)K + (AA)Q + (AA)J + (AA)T
 *   (AA)K+      — plus notation: (AA)K through (AA)A
 *
 * Multi-street (4th–7th):
 *   (AA)KQ      — pair of aces, king 3rd, queen 4th
 *   (AA)KQJ     — pair, then king, queen, jack
 *   (AsKh)QdJc  — specific suits up to 4th street
 *   (xx)KQJT9   — any hole, up to 7th street (5 upcards)
 *   (AA)KQ-T    — range on last upcard: (AA)KQ...KT
 *   (AA)KQ+     — plus on last upcard: (AA)KQ...KA
 */
static int stud_parse_pattern(const char *pattern, StdDeck_CardMask dead_cards,
                              arp_range_t *range, stud_index_t *ix) {
  hole_info_t hole;
  upcard_info_t upcards[MAX_STUD_UP_CARDS];
  int num_upcards = 0;
  int consumed = 0;
  const char *rest;

  /* --- Triplet format: AKQ = (AK)Q (3rd street only) --- */
  if (pattern[0] != '(') {
    int r1 = stud_char_to_rank(pattern[0]);
    int r2 = stud_char_to_rank(pattern[1]);
    int r3 = stud_char_to_rank(pattern[2]);

    if (r1 < 0 || r2 < 0 || r3 < 0)
      return 0;

    memset(&hole, 0, sizeof(hole));
    hole.r1 = r1;
    hole.r2 = r2;
    hole.s1 = hole.s2 = -1;
    hole.type = (r1 == r2) ? HOLE_PAIR : HOLE_SPECIFIC_RANKS;

    memset(&upcards[0], 0, sizeof(upcards[0]));
    upcards[0].rank = r3;
    upcards[0].suit = -1;
    upcards[0].suit_binding = 0;
    num_upcards = 1;

    rest = pattern + 3;

    /* Check for triplet range: AKQ-AKJ (only upcard varies) */
    if (rest[0] == '-') {
      int rr1 = stud_char_to_rank(rest[1]);
      int rr2 = stud_char_to_rank(rest[2]);
      int rr3 = stud_char_to_rank(rest[3]);
      if (rr1 < 0 || rr2 < 0 || rr3 < 0)
        return 0;

      int up_hi = r3;
      int up_lo = rr3;
      if (up_hi < up_lo) {
        int t = up_hi;
        up_hi = up_lo;
        up_lo = t;
      }

      for (int r = up_lo; r <= up_hi; r++) {
        upcards[0].rank = r;
        if (generate_hands_multi(&hole, upcards, 1, dead_cards, range, ix, 1.0) < 0)
          return 0;
      }
      return 1;
    }

    return generate_hands_multi(&hole, upcards, 1, dead_cards, range, ix, 1.0) >= 0;
  }

  /* --- Parenthesized format: (XX)Y... --- */
  consumed = parse_hole_cards(pattern, &hole);
  if (consumed == 0)
    return 0;

  rest = pattern + consumed;

  /* Parse upcards (1–5 for 3rd–7th street) */
  int up_consumed = 0;
  num_upcards = parse_upcards(rest, hole.type, upcards, MAX_STUD_UP_CARDS,
                              &up_consumed);
  if (num_upcards == 0)
    return 0;

  rest += up_consumed;

  /* --- Check for range interval: (AA)KQ-T (range on last upcard) --- */
  if (rest[0] == '-') {
    rest++;
    int end_rank = stud_char_to_rank(rest[0]);
    if (end_rank < 0)
      return 0;

    int last = num_upcards - 1;
    if (upcards[last].rank < 0)
      return 0; /* Can't range on wildcard */

    int up_hi = upcards[last].rank;
    int up_lo = end_rank;
    if (up_hi < up_lo) {
      int t = up_hi;
      up_hi = up_lo;
      up_lo = t;
    }

    for (int r = up_lo; r <= up_hi; r++) {
      upcards[last].rank = r;
      if (generate_hands_multi(&hole, upcards, num_upcards,
                               dead_cards, range, ix, 1.0) < 0)
        return 0;
    }
    return 1;
  }

  /* --- Check for plus notation: (AA)KQ+ --- */
  if (rest[0] == '+') {
    int last = num_upcards - 1;
    if (upcards[last].rank < 0)
      return 0;

    for (int r = upcards[last].rank; r <= StdDeck_Rank_ACE; r++) {
      upcards[last].rank = r;
      if (generate_hands_multi(&hole, upcards, num_upcards,
                               dead_cards, range, ix, 1.0) < 0)
        return 0;
    }
    return 1;
  }

  /* --- Single pattern --- */
  return generate_hands_multi(&hole, upcards, num_upcards,
                              dead_cards, range, ix, 1.0) >= 0;
}

int ARP_ParseStudPattern(const char *pattern, StdDeck_CardMask dead_cards,
                         arp_range_t *range) {
  if (!pattern || !range)
    return 0;
  /* Shortest legal pattern is a triplet ("AKQ"); the parser reads pattern[2]
   * unconditionally on that path. Short-circuiting on each byte stops at the
   * terminator instead of scanning the whole string. */
  if (!pattern[0] || !pattern[1] || !pattern[2])
    return 0;

  /* The dedup index lives for the duration of one parse; it indexes the hands
   * already in `range` plus everything this pattern adds. */
  stud_index_t ix;
  stud_index_init(&ix, range);
  int ok = stud_parse_pattern(pattern, dead_cards, range, &ix);
  stud_index_free(&ix);
  return ok;
}
