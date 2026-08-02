/*
 * Copyright (C) 1999-2002
 *           Michael Maurer <mjmaurer@yahoo.com>
 *           Brian Goetz <brian@quiotix.com>
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

#ifndef __EVAL_LOW_H__
#define __EVAL_LOW_H__

#include <assert.h>
#include <poker_eval/core/handval_low.h>

/*
 * Lowball evaluator.  Assumes that n_cards >= 5
 */

static inline uint32
_bottomNCards(uint32 cards, int howMany) {
  int i;
  uint32 retval, t;

  retval = 0;
  for (i=0; i<howMany; i++) {
    t = bottomCardTable[cards];
    retval += t << (i*LowHandVal_CARD_WIDTH);
    cards ^= (1 << t);
  }

  return retval;
}

/* Helper function to count occurrences of each rank */
static inline void
_countRanks(uint32 ranks, int rankCounts[]) {
  int i;
  for (i = 0; i < 13; i++) {
    rankCounts[i] = 0;
  }
  
  for (i = 0; i < 13; i++) {
    if (ranks & (1 << i)) {
      uint32 mask = (1 << i);
      int count = ((ranks & mask) != 0) ? 1 : 0;
      // Count how many times this rank appears across suits
      rankCounts[i] = count;
    }
  }
}

/* Helper to count actual duplicates across suits */
static inline void
_countDuplicates(uint32 ss, uint32 sc, uint32 sd, uint32 sh, int rankCounts[]) {
  int i;
  for (i = 0; i < 13; i++) {
    rankCounts[i] = 0;
    uint32 mask = (1 << i);
    if (ss & mask) rankCounts[i]++;
    if (sc & mask) rankCounts[i]++;
    if (sd & mask) rankCounts[i]++;
    if (sh & mask) rankCounts[i]++;
  }
}

/* Forward declaration to avoid implicit declaration warnings */
static inline LowHandVal StdDeck_Lowball_EVAL(StdDeck_CardMask cards, int n_cards);

/* Find the best lowball hand from available cards */
#include <string.h>
#include <stdint.h>
#include <stdlib.h> // For qsort

// Helper to get the N lowest unique ranks from a bitmask
static inline void get_lowest_unique_ranks(uint32 rank_mask, int count, int* out_ranks) {
    int found = 0;
    for (int i = 0; i < 13 && found < count; ++i) {
        if (rank_mask & (1 << i)) {
            out_ranks[found++] = i;
        }
    }
}

// Comparison function for qsort (ascending order)
static int compare_ints(const void* a, const void* b) {
    return (*(const int*)a - *(const int*)b);
}

static inline LowHandVal
_build_low_hand(int r[], int num_ranks) {
    // Assumes ranks r[] are already rotated for lowball (Ace = 0)
    // and sorted. Builds the LowHandVal for a NoPair hand.
    LowHandVal val = LowHandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR);
    val += LowHandVal_TOP_CARD_VALUE(r[0]);
    val += LowHandVal_SECOND_CARD_VALUE(r[1]);
    val += LowHandVal_THIRD_CARD_VALUE(r[2]);
    val += LowHandVal_FOURTH_CARD_VALUE(r[3]);
    val += LowHandVal_FIFTH_CARD_VALUE(r[4]);
    return val;
}


#include <poker_eval/games/lowball_algorithm.h>
static inline LowHandVal
_findBestLowballHand(uint32 ss_orig, uint32 sc_orig, uint32 sd_orig, uint32 sh_orig, uint32 ranks_orig, int n_cards_total) {
    // Nouvelle logique hiérarchique Lowball :
    int rank_counts[StdDeck_Rank_COUNT] = {0};
    // Compter les occurrences de chaque rang (toutes couleurs confondues)
    for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
        if (ss_orig & (1 << r)) rank_counts[r]++;
        if (sc_orig & (1 << r)) rank_counts[r]++;
        if (sd_orig & (1 << r)) rank_counts[r]++;
        if (sh_orig & (1 << r)) rank_counts[r]++;
    }
    // Les selecteurs rendent les rangs par ordre croissant, alors que LowHandVal
    // range la carte la plus haute dans TOP_CARD (cf. le chemin 5 cartes plus bas
    // et LowHandVal_WORST_EIGHT). Les cartes sont donc reprises a l'envers.
    int out_ranks[5];
    // No Pair
    if (select_no_pair_ranks(rank_counts, out_ranks)) {
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)
            + LowHandVal_TOP_CARD_VALUE(out_ranks[4])
            + LowHandVal_SECOND_CARD_VALUE(out_ranks[3])
            + LowHandVal_THIRD_CARD_VALUE(out_ranks[2])
            + LowHandVal_FOURTH_CARD_VALUE(out_ranks[1])
            + LowHandVal_FIFTH_CARD_VALUE(out_ranks[0]);
    }
    // One Pair: la paire occupe TOP_CARD, puis les kickers du plus haut au plus bas
    int out_pair[4];
    if (select_one_pair_ranks(rank_counts, out_pair)) {
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR)
            + LowHandVal_TOP_CARD_VALUE(out_pair[0])
            + LowHandVal_SECOND_CARD_VALUE(out_pair[3])
            + LowHandVal_THIRD_CARD_VALUE(out_pair[2])
            + LowHandVal_FOURTH_CARD_VALUE(out_pair[1]);
    }
    // Two Pair: paire haute, paire basse, puis kicker
    int out_two_pair[3];
    if (select_two_pair_ranks(rank_counts, out_two_pair)) {
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR)
            + LowHandVal_TOP_CARD_VALUE(out_two_pair[1])
            + LowHandVal_SECOND_CARD_VALUE(out_two_pair[0])
            + LowHandVal_THIRD_CARD_VALUE(out_two_pair[2]);
    }
    // Trips
    int out_trips[3];
    if (select_trips_ranks(rank_counts, out_trips)) {
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS)
            + LowHandVal_TOP_CARD_VALUE(out_trips[0])
            + LowHandVal_SECOND_CARD_VALUE(out_trips[2])
            + LowHandVal_THIRD_CARD_VALUE(out_trips[1]);
    }
    // Full House
    int out_full[2];
    if (select_full_house_ranks(rank_counts, out_full)) {
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE)
            + LowHandVal_TOP_CARD_VALUE(out_full[0])
            + LowHandVal_SECOND_CARD_VALUE(out_full[1]);
    }
    // Quads
    int out_quads[4];
    if (select_quads_ranks(rank_counts, out_quads)) {
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS)
            + LowHandVal_TOP_CARD_VALUE(out_quads[0])
            + LowHandVal_SECOND_CARD_VALUE(out_quads[1])
            + LowHandVal_THIRD_CARD_VALUE(out_quads[2])
            + LowHandVal_FOURTH_CARD_VALUE(out_quads[3]);
    }
    // Si aucun cas trouvé (devrait être impossible)
    assert(!"No valid lowball hand found in _findBestLowballHand");
    return 0xFFFFFFFF;
}


static inline LowHandVal 
StdDeck_Lowball_EVAL(StdDeck_CardMask cards, int n_cards) {
  uint32 ranks, dups, trips, ss, sh, sd, sc, t, tt;

  ss = StdDeck_CardMask_SPADES(cards);
  sc = StdDeck_CardMask_CLUBS(cards);
  sd = StdDeck_CardMask_DIAMONDS(cards);
  sh = StdDeck_CardMask_HEARTS(cards);

  ss = Lowball_ROTATE_RANKS(ss);
  sc = Lowball_ROTATE_RANKS(sc);
  sd = Lowball_ROTATE_RANKS(sd);
  sh = Lowball_ROTATE_RANKS(sh);

  ranks = sc | ss | sd | sh;
  
  // For 6-7 card hands, use the new logic
  if (n_cards > 5) {
    return _findBestLowballHand(ss, sc, sd, sh, ranks, n_cards);
  }
  
  // For exactly 5 cards, use the original optimized logic
  if (nBitsTable[ranks] >= 5) 
    return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) 
      + bottomFiveCardsTable[ranks];
  else {
    dups = (sc&sd) | (sh & (sc|sd)) | (ss & (sh|sc|sd));
    t = bottomCardTable[dups];

    switch (nBitsTable[ranks]) {
    case 4:
      return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR) 
        + LowHandVal_TOP_CARD_VALUE(t)
        + (_bottomNCards(ranks ^ (1 << t), 3) << LowHandVal_CARD_WIDTH);
      break;

    case 3:
      if (nBitsTable[dups] == 2) {
        tt = bottomCardTable[dups ^ (1 << t)];
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR)
          + LowHandVal_TOP_CARD_VALUE(tt) 
          + LowHandVal_SECOND_CARD_VALUE(t) 
          + ((_bottomNCards(ranks ^ (1 << t) ^ (1 << tt), 1)) 
             << (2*LowHandVal_CARD_WIDTH));
      }
      else {
        t = bottomCardTable[dups];
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS) 
          + LowHandVal_TOP_CARD_VALUE(t)
          + (_bottomNCards(ranks ^ (1 << t), 2) 
             << (2*LowHandVal_CARD_WIDTH));
      }
      break;

    case 2:
      if (nBitsTable[dups] == 2) {
        trips = dups & (sc ^ ss ^ sd ^ sh);
        t = bottomCardTable[trips];
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE)
          + LowHandVal_TOP_CARD_VALUE(t) 
          + LowHandVal_SECOND_CARD_VALUE(bottomCardTable[ranks ^ (1 << t)]);
      }
      else {
        return LowHandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS)
          + LowHandVal_TOP_CARD_VALUE(t) 
          + LowHandVal_SECOND_CARD_VALUE(bottomCardTable[ranks ^ (1 << t)]);
      };
      break;
    
    default:
      /* Should not reach here */
      assert(!"Invalid number of ranks in eval_low");
      return 0;
    };
  };

  assert(!"Logic error in eval_low");
  return 0;
}

#endif
