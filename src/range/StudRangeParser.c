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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum number of upcards for multi-street Stud (3rd→7th: 5 upcards) */
#define MAX_STUD_UP_CARDS 5

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

static int stud_add_hand_to_range(arp_range_t *range, StdDeck_CardMask hand,
                                  double weight) {
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
  for (size_t i = 0; i < range->count; i++) {
    if (StdDeck_CardMask_EQUAL(range->hands[i], hand)) {
      /* Already present — keep max weight */
      if (weight > range->weights[i]) {
        range->total_weight += (weight - range->weights[i]);
        range->weights[i] = weight;
      }
      return 1;
    }
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
                             arp_range_t *range, double weight) {
  if (idx >= num_upcards) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    for (int i = 0; i < used_count; i++)
      StdDeck_CardMask_SET(hand, cards[i]);

    if (StdDeck_CardMask_ANY_SET(dead_cards, hand))
      return 0;

    if (!stud_add_hand_to_range(range, hand, weight))
      return -1;
    return 1;
  }

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
                                   dead_cards, range, weight);
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
                                arp_range_t *range, double weight) {
  int count = 0;

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
      int cards[2 + MAX_STUD_UP_CARDS];
      cards[0] = c1;
      cards[1] = c2;

      int result = place_upcards_rec(upcards, num_upcards, 0,
                                     cards, 2,
                                     dead_cards, range, weight);
      if (result < 0)
        return -1;
      count += result;
    }
  }

  return count;
}

/* For backward compat: wrap generate_hands with single-upcard path */
static inline int generate_hands(const hole_info_t *hole, const upcard_info_t *up,
                                 StdDeck_CardMask dead_cards,
                                 arp_range_t *range, double weight) {
  return generate_hands_multi(hole, up, 1, dead_cards, range, weight);
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

    size_t after = (size_t)(close_paren - str) + 1;
    if (after >= len)
      return 0; /* Nothing after ')' */

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
int ARP_ParseStudPattern(const char *pattern, StdDeck_CardMask dead_cards,
                         arp_range_t *range) {
  if (!pattern || !range)
    return 0;

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
        if (generate_hands_multi(&hole, upcards, 1, dead_cards, range, 1.0) < 0)
          return 0;
      }
      return 1;
    }

    return generate_hands_multi(&hole, upcards, 1, dead_cards, range, 1.0) >= 0;
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
                               dead_cards, range, 1.0) < 0)
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
                               dead_cards, range, 1.0) < 0)
        return 0;
    }
    return 1;
  }

  /* --- Single pattern --- */
  return generate_hands_multi(&hole, upcards, num_upcards,
                              dead_cards, range, 1.0) >= 0;
}
