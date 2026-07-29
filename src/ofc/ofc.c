/*
 * ofc.c - Open Face Chinese Poker implementation
 *
 * Copyright (C) 2024
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <poker_eval/core/eval.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h>

/* Internal function prototypes */
static HandVal OFC_Evaluate3CardHand(StdDeck_CardMask cards);
static float ofc_calculate_foul_risk_cpu(const ofc_player_hand_t *partial_hand,
                                         int card, ofc_position_t position);

/* Exposed for SIMD helpers to access the pure CPU routine without recursion */
float OFC_CalculateFoulRiskCPUOnly(const ofc_player_hand_t *partial_hand,
                                   int card, ofc_position_t position);

/* Initialize an OFC hand structure */
int OFC_InitializeHand(ofc_player_hand_t *hand) {
  int i;

  if (!hand)
    return -1;

  /* Clear all hands */
  for (i = 0; i < OFC_NUM_HANDS; i++) {
    StdDeck_CardMask_RESET(hand->hands[i]);
    hand->card_count[i] = 0;
  }

  hand->is_foul = 0;
  hand->is_fantasyland = 0;
  hand->stay_fantasyland = 0;

  return 0;
}

/* Place a card in specified position */
int OFC_PlaceCard(ofc_player_hand_t *hand, int card, ofc_position_t position) {
  int max_cards;

  if (!hand || position >= OFC_NUM_HANDS)
    return -1;

  /* Check position capacity */
  if (position == OFC_TOP)
    max_cards = OFC_TOP_CARDS;
  else
    max_cards = OFC_MIDDLE_CARDS; /* Both middle and bottom have 5 */

  if (hand->card_count[position] >= max_cards) {
    return -1; /* Position full */
  }

  /* Add card to the hand */
  StdDeck_CardMask_SET(hand->hands[position], card);
  hand->card_count[position]++;

  return 0;
}

/* Validate hand hierarchy: Bottom >= Middle >= Top */
int OFC_ValidateHierarchy(const ofc_player_hand_t *hand) {
  HandVal top_val, middle_val, bottom_val;

  if (!hand)
    return -1;

  /* Can only validate complete hands */
  if (hand->card_count[OFC_TOP] != OFC_TOP_CARDS ||
      hand->card_count[OFC_MIDDLE] != OFC_MIDDLE_CARDS ||
      hand->card_count[OFC_BOTTOM] != OFC_BOTTOM_CARDS) {
    return 0; /* Not complete yet, consider valid */
  }

  /* Evaluate each hand */
  top_val = OFC_EvaluateHand(hand->hands[OFC_TOP], OFC_TOP_CARDS);
  middle_val = OFC_EvaluateHand(hand->hands[OFC_MIDDLE], OFC_MIDDLE_CARDS);
  bottom_val = OFC_EvaluateHand(hand->hands[OFC_BOTTOM], OFC_BOTTOM_CARDS);

  /* Check hierarchy: bottom >= middle >= top */
  if (bottom_val >= middle_val && middle_val >= top_val) {
    return 1; /* Valid hierarchy */
  }

  return 0; /* Foul - invalid hierarchy */
}

/* Check if hand is complete (all 13 cards placed) */
int OFC_IsComplete(const ofc_player_hand_t *hand) {
  if (!hand)
    return 0;

  return (hand->card_count[OFC_TOP] == OFC_TOP_CARDS &&
          hand->card_count[OFC_MIDDLE] == OFC_MIDDLE_CARDS &&
          hand->card_count[OFC_BOTTOM] == OFC_BOTTOM_CARDS);
}

/* Evaluate an OFC hand (handles 3-card hands specially) */
HandVal OFC_EvaluateHand(StdDeck_CardMask cards, int num_cards) {
  if (num_cards == 3) {
    /* Special 3-card evaluation for top hand */
    return OFC_Evaluate3CardHand(cards);
  } else if (num_cards == 5) {
    /* Standard 5-card evaluation */
    return StdDeck_StdRules_EVAL_N(cards, 5);
  }

  return HandVal_NOTHING;
}

/* Special 3-card hand evaluation for top position */
static HandVal OFC_Evaluate3CardHand(StdDeck_CardMask cards) {
  int card_ranks[3];
  int card_suits[3];
  int i, card_count = 0;
  int ranks[StdDeck_Rank_COUNT] = {0};
  int suits[StdDeck_Suit_COUNT] = {0};

  /* Extract cards */
  for (i = 0; i < StdDeck_N_CARDS && card_count < 3; i++) {
    if (StdDeck_CardMask_CARD_IS_SET(cards, i)) {
      card_ranks[card_count] = StdDeck_RANK(i);
      card_suits[card_count] = StdDeck_SUIT(i);
      ranks[card_ranks[card_count]]++;
      suits[card_suits[card_count]]++;
      card_count++;
    }
  }

  if (card_count != 3)
    return HandVal_NOTHING;

  /* Check for three of a kind */
  for (i = 0; i < StdDeck_Rank_COUNT; i++) {
    if (ranks[i] == 3) {
      return HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS) + (i << 16);
    }
  }

  /* Check for pair */
  for (i = StdDeck_Rank_COUNT - 1; i >= 0; i--) {
    if (ranks[i] == 2) {
      /* Find kicker */
      int kicker;
      for (kicker = StdDeck_Rank_COUNT - 1; kicker >= 0; kicker--) {
        if (ranks[kicker] == 1) {
          return HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR) + (i << 16) +
                 (kicker << 12);
        }
      }
    }
  }

  /* High card - find three highest cards */
  int high_cards[3] = {-1, -1, -1};
  int found = 0;
  for (i = StdDeck_Rank_COUNT - 1; i >= 0 && found < 3; i--) {
    if (ranks[i] == 1) {
      high_cards[found++] = i;
    }
  }

  if (found == 3) {
    return HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR) +
           (high_cards[0] << 16) + (high_cards[1] << 12) + (high_cards[2] << 8);
  }

  return HandVal_NOTHING;
}

/* Compare two hands for a specific position */
int OFC_CompareHands(StdDeck_CardMask hand1, int cards1, StdDeck_CardMask hand2,
                     int cards2, ofc_position_t position) {
  HandVal val1, val2;

  val1 = OFC_EvaluateHand(hand1, cards1);
  val2 = OFC_EvaluateHand(hand2, cards2);

  if (val1 > val2)
    return 1; /* hand1 wins */
  if (val1 < val2)
    return -1; /* hand2 wins */
  return 0;    /* tie */
}

/* Utility function to convert position to string */
const char *OFC_PositionToString(ofc_position_t position) {
  switch (position) {
  case OFC_TOP:
    return "Top";
  case OFC_MIDDLE:
    return "Middle";
  case OFC_BOTTOM:
    return "Bottom";
  case OFC_NUM_HANDS:
    return "Invalid";
  default:
    return "Unknown";
  }
}

/* Calculate royalties for top hand (3 cards) */
int OFC_CalculateTopRoyalties(StdDeck_CardMask top_hand) {
  HandVal hand_val;
  int hand_type;

  hand_val = OFC_Evaluate3CardHand(top_hand);
  hand_type = HandVal_HANDTYPE(hand_val);

  /* Top hand royalties:
   * 6,6,6: +20 points
   * 5,5,5: +18 points
   * 4,4,4: +16 points
   * 3,3,3: +14 points
   * 2,2,2: +12 points
   * A,A,A: +22 points
   * K,K,K: +10 points
   * Q,Q,Q: +8 points
   * J,J,J: +6 points
   * T,T,T: +4 points
   * 9,9,9: +2 points
   * A,A: +9 points
   * K,K: +8 points
   * Q,Q: +7 points
   * J,J: +6 points
   * T,T: +5 points
   * 9,9: +4 points
   * 8,8: +3 points
   * 7,7: +2 points
   * 6,6: +1 point
   */

  if (hand_type == StdRules_HandType_TRIPS) {
    int rank = (hand_val >> 16) & 0xF;
    if (rank == StdDeck_Rank_ACE)
      return 22;
    if (rank == StdDeck_Rank_KING)
      return 10;
    if (rank == StdDeck_Rank_QUEEN)
      return 8;
    if (rank == StdDeck_Rank_JACK)
      return 6;
    if (rank == StdDeck_Rank_TEN)
      return 4;
    if (rank == StdDeck_Rank_9)
      return 2;
    if (rank >= StdDeck_Rank_6)
      return 12 + (rank - StdDeck_Rank_6) * 2;
    return 0;
  } else if (hand_type == StdRules_HandType_ONEPAIR) {
    int rank = (hand_val >> 16) & 0xF;
    if (rank == StdDeck_Rank_ACE)
      return 9;
    if (rank == StdDeck_Rank_KING)
      return 8;
    if (rank == StdDeck_Rank_QUEEN)
      return 7;
    if (rank == StdDeck_Rank_JACK)
      return 6;
    if (rank == StdDeck_Rank_TEN)
      return 5;
    if (rank == StdDeck_Rank_9)
      return 4;
    if (rank == StdDeck_Rank_8)
      return 3;
    if (rank == StdDeck_Rank_7)
      return 2;
    if (rank == StdDeck_Rank_6)
      return 1;
    return 0;
  }

  return 0; /* No royalties for high card */
}

/* Calculate royalties for middle hand (5 cards) */
int OFC_CalculateMiddleRoyalties(StdDeck_CardMask middle_hand) {
  HandVal hand_val;
  int hand_type;

  hand_val = StdDeck_StdRules_EVAL_N(middle_hand, 5);
  hand_type = HandVal_HANDTYPE(hand_val);

  /* Middle hand royalties:
   * Royal flush: +50 points
   * Straight flush: +30 points
   * Four of a kind: +20 points
   * Full house: +12 points
   * Flush: +8 points
   * Straight: +4 points
   * Three of a kind: +2 points
   */

  switch (hand_type) {
  case StdRules_HandType_STFLUSH:
    /* Check if royal flush */
    if (((hand_val >> 16) & 0xF) == StdDeck_Rank_ACE) {
      return 50; /* Royal flush */
    }
    return 30; /* Straight flush */
  case StdRules_HandType_QUADS:
    return 20;
  case StdRules_HandType_FULLHOUSE:
    return 12;
  case StdRules_HandType_FLUSH:
    return 8;
  case StdRules_HandType_STRAIGHT:
    return 4;
  case StdRules_HandType_TRIPS:
    return 2;
  default:
    return 0;
  }
}

/* Calculate royalties for bottom hand (5 cards) */
int OFC_CalculateBottomRoyalties(StdDeck_CardMask bottom_hand) {
  HandVal hand_val;
  int hand_type;

  hand_val = StdDeck_StdRules_EVAL_N(bottom_hand, 5);
  hand_type = HandVal_HANDTYPE(hand_val);

  /* Bottom hand royalties:
   * Royal flush: +25 points
   * Straight flush: +15 points
   * Four of a kind: +10 points
   * Full house: +6 points
   * Flush: +4 points
   * Straight: +2 points
   */

  switch (hand_type) {
  case StdRules_HandType_STFLUSH:
    /* Check if royal flush */
    if (((hand_val >> 16) & 0xF) == StdDeck_Rank_ACE) {
      return 25; /* Royal flush */
    }
    return 15; /* Straight flush */
  case StdRules_HandType_QUADS:
    return 10;
  case StdRules_HandType_FULLHOUSE:
    return 6;
  case StdRules_HandType_FLUSH:
    return 4;
  case StdRules_HandType_STRAIGHT:
    return 2;
  default:
    return 0;
  }
}

/* Calculate all royalties for a hand */
int OFC_CalculateRoyalties(const ofc_player_hand_t *hand,
                           ofc_royalties_t *royalties) {
  if (!hand || !royalties)
    return -1;

  royalties->top_royalties = 0;
  royalties->middle_royalties = 0;
  royalties->bottom_royalties = 0;

  /* Only calculate for complete hands */
  if (hand->card_count[OFC_TOP] == OFC_TOP_CARDS) {
    royalties->top_royalties = OFC_CalculateTopRoyalties(hand->hands[OFC_TOP]);
  }
  if (hand->card_count[OFC_MIDDLE] == OFC_MIDDLE_CARDS) {
    royalties->middle_royalties =
        OFC_CalculateMiddleRoyalties(hand->hands[OFC_MIDDLE]);
  }
  if (hand->card_count[OFC_BOTTOM] == OFC_BOTTOM_CARDS) {
    royalties->bottom_royalties =
        OFC_CalculateBottomRoyalties(hand->hands[OFC_BOTTOM]);
  }

  return 0;
}

/* Score hands between two players */
int OFC_ScoreHands(const ofc_player_hand_t *hand1,
                   const ofc_player_hand_t *hand2, ofc_score_t *score1,
                   ofc_score_t *score2) {
  int i;
  ofc_royalties_t royalties1, royalties2;

  if (!hand1 || !hand2 || !score1 || !score2)
    return -1;

  /* Initialize scores */
  memset(score1, 0, sizeof(*score1));
  memset(score2, 0, sizeof(*score2));

  /* Check if hands are complete */
  if (!OFC_IsComplete(hand1) || !OFC_IsComplete(hand2)) {
    return -1; /* Cannot score incomplete hands */
  }

  /* Check for fouls */
  int hand1_valid = OFC_ValidateHierarchy(hand1);
  int hand2_valid = OFC_ValidateHierarchy(hand2);

  if (!hand1_valid) {
    score1->foul_penalty = -6;
    score1->total_score = -6;
    score2->total_score = 6;
    return 0;
  }

  if (!hand2_valid) {
    score2->foul_penalty = -6;
    score2->total_score = -6;
    score1->total_score = 6;
    return 0;
  }

  /* Compare each hand */
  int wins1 = 0, wins2 = 0;
  for (i = 0; i < OFC_NUM_HANDS; i++) {
    int result = OFC_CompareHands(hand1->hands[i],
                                  (i == OFC_TOP)      ? OFC_TOP_CARDS
                                  : (i == OFC_MIDDLE) ? OFC_MIDDLE_CARDS
                                                      : OFC_BOTTOM_CARDS,
                                  hand2->hands[i],
                                  (i == OFC_TOP)      ? OFC_TOP_CARDS
                                  : (i == OFC_MIDDLE) ? OFC_MIDDLE_CARDS
                                                      : OFC_BOTTOM_CARDS,
                                  (ofc_position_t)i);

    if (result > 0) {
      score1->points[i] = 1;
      score2->points[i] = -1;
      wins1++;
    } else if (result < 0) {
      score1->points[i] = -1;
      score2->points[i] = 1;
      wins2++;
    }
    /* Ties remain 0 */
  }

  /* Scoop bonus */
  if (wins1 == 3) {
    score1->scoop_bonus = 3;
    score2->scoop_bonus = -3;
  } else if (wins2 == 3) {
    score1->scoop_bonus = -3;
    score2->scoop_bonus = 3;
  }

  /* Calculate royalties */
  OFC_CalculateRoyalties(hand1, &royalties1);
  OFC_CalculateRoyalties(hand2, &royalties2);

  score1->royalties = royalties1;
  score2->royalties = royalties2;

  /* Calculate total scores */
  int total_royalties1 = royalties1.top_royalties +
                         royalties1.middle_royalties +
                         royalties1.bottom_royalties;
  int total_royalties2 = royalties2.top_royalties +
                         royalties2.middle_royalties +
                         royalties2.bottom_royalties;

  score1->total_score = score1->points[0] + score1->points[1] +
                        score1->points[2] + score1->scoop_bonus +
                        score1->foul_penalty + total_royalties1;

  score2->total_score = score2->points[0] + score2->points[1] +
                        score2->points[2] + score2->scoop_bonus +
                        score2->foul_penalty + total_royalties2;

  return 0;
}

/* Print an OFC hand for debugging */
void OFC_PrintHand(const ofc_player_hand_t *hand) {
  int i;
  char card_str[128];

  if (!hand) {
    printf("NULL hand\n");
    return;
  }

  printf("OFC Hand:\n");
  for (i = 0; i < OFC_NUM_HANDS; i++) {
    printf("  %s (%d cards): ", OFC_PositionToString((ofc_position_t)i),
           hand->card_count[i]);
    if (hand->card_count[i] > 0) {
      StdDeck_CardMask temp_mask = hand->hands[i];
      StdDeck_maskToString(temp_mask, card_str);
      printf("%s", card_str);
    } else {
      printf("(empty)");
    }
    printf("\n");
  }

  printf("  Foul: %s\n", hand->is_foul ? "Yes" : "No");
  printf("  Fantasyland: %s\n", hand->is_fantasyland ? "Yes" : "No");
}

/* Print an OFC score for debugging */
void OFC_PrintScore(const ofc_score_t *score) {
  if (!score) {
    printf("NULL score\n");
    return;
  }

  printf("OFC Score:\n");
  printf("  Points: %d/%d/%d (Top/Middle/Bottom)\n", score->points[OFC_TOP],
         score->points[OFC_MIDDLE], score->points[OFC_BOTTOM]);
  printf("  Royalties: %d/%d/%d\n", score->royalties.top_royalties,
         score->royalties.middle_royalties, score->royalties.bottom_royalties);
  printf("  Scoop bonus: %d\n", score->scoop_bonus);
  printf("  Foul penalty: %d\n", score->foul_penalty);
  printf("  Total: %d\n", score->total_score);
}

/* Check if a hand qualifies for fantasyland */
int OFC_CheckFantasyland(const ofc_player_hand_t *hand) {
  if (!hand)
    return 0;

  /* Must have complete top hand */
  if (hand->card_count[OFC_TOP] != OFC_TOP_CARDS)
    return 0;

  /* Check if top hand has QQ or better */
  HandVal top_val = OFC_Evaluate3CardHand(hand->hands[OFC_TOP]);
  int hand_type = HandVal_HANDTYPE(top_val);

  if (hand_type == StdRules_HandType_TRIPS) {
    return 1; /* Any trips qualifies */
  }

  if (hand_type == StdRules_HandType_ONEPAIR) {
    int rank = (top_val >> 16) & 0xF;
    return (rank >= StdDeck_Rank_QUEEN); /* QQ or better */
  }

  return 0; /* High card doesn't qualify */
}

/* Check if a hand qualifies to stay in fantasyland */
int OFC_CheckStayFantasyland(const ofc_player_hand_t *hand) {
  if (!hand)
    return 0;

  /* Must be complete hand */
  if (!OFC_IsComplete(hand))
    return 0;

  /* Check various criteria for staying in fantasyland:
   * - Top: Trips or better
   * - Middle: Full house or better
   * - Bottom: Four of a kind or better
   * - Any two pair in top (special rule)
   */

  /* Check top hand for trips */
  if (hand->card_count[OFC_TOP] == OFC_TOP_CARDS) {
    HandVal top_val = OFC_Evaluate3CardHand(hand->hands[OFC_TOP]);
    int hand_type = HandVal_HANDTYPE(top_val);
    if (hand_type == StdRules_HandType_TRIPS) {
      return 1;
    }
  }

  /* Check middle hand for full house or better */
  if (hand->card_count[OFC_MIDDLE] == OFC_MIDDLE_CARDS) {
    HandVal middle_val = StdDeck_StdRules_EVAL_N(hand->hands[OFC_MIDDLE], 5);
    int hand_type = HandVal_HANDTYPE(middle_val);
    if (hand_type >= StdRules_HandType_FULLHOUSE) {
      return 1;
    }
  }

  /* Check bottom hand for four of a kind or better */
  if (hand->card_count[OFC_BOTTOM] == OFC_BOTTOM_CARDS) {
    HandVal bottom_val = StdDeck_StdRules_EVAL_N(hand->hands[OFC_BOTTOM], 5);
    int hand_type = HandVal_HANDTYPE(bottom_val);
    if (hand_type >= StdRules_HandType_QUADS) {
      return 1;
    }
  }

  return 0; /* Doesn't qualify to stay */
}

/* ===== OFC EQUITY AND ANALYSIS FUNCTIONS ===== */

/* Forward declaration for helper function */
static int OFC_RandomlyCompleteHand(ofc_player_hand_t *hand,
                                    int *available_cards, int num_available);

/* Helper: Check if a specific card is available (not already used in partial
 * hand) */
int OFC_IsCardAvailable(const ofc_player_hand_t *partial_hand, int card) {
  int i;

  if (!partial_hand)
    return 1;

  /* Check all positions for this card */
  for (i = 0; i < OFC_NUM_HANDS; i++) {
    if (StdDeck_CardMask_CARD_IS_SET(partial_hand->hands[i], card)) {
      return 0; /* Card already used */
    }
  }

  return 1; /* Card available */
}

/* Helper: Get list of available cards for simulation */
int OFC_GetAvailableCards(const ofc_player_hand_t *partial_hand,
                          int *available_cards) {
  int count = 0;
  int card;

  if (!available_cards)
    return 0;

  /* Check all 52 cards */
  for (card = 0; card < StdDeck_N_CARDS; card++) {
    if (OFC_IsCardAvailable(partial_hand, card)) {
      available_cards[count++] = card;
    }
  }

  return count; /* Number of available cards */
}

/* Calculate foul risk for placing a card in a position - SIMD optimized version
 */
float OFC_CalculateFoulRisk(const ofc_player_hand_t *partial_hand, int card,
                            ofc_position_t position) {
  /* Try SIMD-accelerated version first if available and enabled */
  if (OFC_IsSIMDEnabled() && OFC_GetOptimalBatchSize() > 1) {
    return OFC_CalculateFoulRiskSIMDSingle(partial_hand, card, position, 1000);
  }

  /* Fallback to original CPU implementation */
  return ofc_calculate_foul_risk_cpu(partial_hand, card, position);
}

float OFC_CalculateFoulRiskCPUOnly(const ofc_player_hand_t *partial_hand,
                                   int card, ofc_position_t position) {
  return ofc_calculate_foul_risk_cpu(partial_hand, card, position);
}

/* Original CPU-only implementation */
static float ofc_calculate_foul_risk_cpu(const ofc_player_hand_t *partial_hand,
                                         int card, ofc_position_t position) {
  ofc_player_hand_t test_hand;
  int available_cards[StdDeck_N_CARDS];
  int num_available;
  int total_simulations = 0;
  int foul_simulations = 0;
  int remaining_cards_needed;
  int i;

  if (!partial_hand || position >= OFC_NUM_HANDS) {
    return 1.0f; /* Invalid input = high risk */
  }

  /* Check if card is available */
  if (!OFC_IsCardAvailable(partial_hand, card)) {
    return 1.0f; /* Card already used = impossible */
  }

  /* Copy the partial hand and add the proposed card */
  memcpy(&test_hand, partial_hand, sizeof(ofc_player_hand_t));

  if (OFC_PlaceCard(&test_hand, card, position) != 0) {
    return 1.0f; /* Placement failed (overfill) = high risk */
  }

  /* If hand is already complete, just check if it's foul */
  if (OFC_IsComplete(&test_hand)) {
    return OFC_ValidateHierarchy(&test_hand) ? 0.0f : 1.0f;
  }

  /* Calculate how many more cards are needed */
  remaining_cards_needed = OFC_TOTAL_CARDS - (test_hand.card_count[OFC_TOP] +
                                              test_hand.card_count[OFC_MIDDLE] +
                                              test_hand.card_count[OFC_BOTTOM]);

  /* Get available cards for simulation */
  num_available = OFC_GetAvailableCards(&test_hand, available_cards);

  if (num_available < remaining_cards_needed) {
    return 1.0f; /* Not enough cards available */
  }

  /* For efficiency, limit simulations based on remaining cards */
  int max_simulations = 1000; /* Reasonable sample size */

  if (remaining_cards_needed <= 3) {
    /* If few cards left, we can be more exhaustive */
    max_simulations = 5000;
  } else if (remaining_cards_needed >= 8) {
    /* If many cards left, sample fewer scenarios */
    max_simulations = 500;
  }

  /* Monte Carlo simulation */
  for (i = 0; i < max_simulations && i < 10000; i++) {
    ofc_player_hand_t sim_hand;
    memcpy(&sim_hand, &test_hand, sizeof(ofc_player_hand_t));

    /* Randomly complete the hand */
    if (OFC_RandomlyCompleteHand(&sim_hand, available_cards, num_available) ==
        0) {
      total_simulations++;

      /* Check if this completion results in a foul */
      if (!OFC_ValidateHierarchy(&sim_hand)) {
        foul_simulations++;
      }
    }
  }

  /* Return foul probability */
  if (total_simulations == 0) {
    return 1.0f; /* Couldn't simulate = high risk */
  }

  return (float)foul_simulations / (float)total_simulations;
}

/* Helper: Randomly complete a partial OFC hand */
static int OFC_RandomlyCompleteHand(ofc_player_hand_t *hand,
                                    int *available_cards, int num_available) {
  int remaining_cards_needed;
  int cards_to_use[StdDeck_N_CARDS];
  int i, j, temp;

  if (!hand || !available_cards || num_available <= 0)
    return -1;

  /* Calculate remaining cards needed */
  remaining_cards_needed = OFC_TOTAL_CARDS - (hand->card_count[OFC_TOP] +
                                              hand->card_count[OFC_MIDDLE] +
                                              hand->card_count[OFC_BOTTOM]);

  if (remaining_cards_needed <= 0)
    return 0; /* Already complete */
  if (remaining_cards_needed > num_available)
    return -1; /* Not enough cards */

  /* Shuffle available cards (Fisher-Yates shuffle) */
  memcpy(cards_to_use, available_cards, sizeof(int) * num_available);

  for (i = num_available - 1; i > 0; i--) {
    j = rand() % (i + 1);
    temp = cards_to_use[i];
    cards_to_use[i] = cards_to_use[j];
    cards_to_use[j] = temp;
  }

  /* Place cards to complete the hand */
  int card_index = 0;

  /* Fill top hand first */
  while (hand->card_count[OFC_TOP] < OFC_TOP_CARDS &&
         card_index < remaining_cards_needed) {
    OFC_PlaceCard(hand, cards_to_use[card_index++], OFC_TOP);
  }

  /* Fill middle hand */
  while (hand->card_count[OFC_MIDDLE] < OFC_MIDDLE_CARDS &&
         card_index < remaining_cards_needed) {
    OFC_PlaceCard(hand, cards_to_use[card_index++], OFC_MIDDLE);
  }

  /* Fill bottom hand */
  while (hand->card_count[OFC_BOTTOM] < OFC_BOTTOM_CARDS &&
         card_index < remaining_cards_needed) {
    OFC_PlaceCard(hand, cards_to_use[card_index++], OFC_BOTTOM);
  }

  return OFC_IsComplete(hand) ? 0 : -1;
}

/* Calculate fantasyland probability for current hand state */
float OFC_CalculateFantasylandProbability(
    const ofc_player_hand_t *partial_hand) {
  const int NUM_SIMULATIONS = 2000;
  int available_cards[52];
  int num_available;
  int fantasyland_simulations = 0;
  int total_simulations = 0;
  int i;

  if (!partial_hand) {
    return 0.0f;
  }

  /* If hand is already complete, just check current state */
  if (OFC_IsComplete(partial_hand)) {
    return OFC_CheckFantasyland(partial_hand) ? 1.0f : 0.0f;
  }

  /* Get available cards */
  num_available = OFC_GetAvailableCards(partial_hand, available_cards);
  if (num_available == 0) {
    return 0.0f;
  }

  /* Run Monte Carlo simulations */
  for (i = 0; i < NUM_SIMULATIONS; i++) {
    ofc_player_hand_t sim_hand = *partial_hand;

    /* Randomly complete the hand */
    if (OFC_RandomlyCompleteHand(&sim_hand, available_cards, num_available) ==
        0) {
      total_simulations++;

      /* Check if this completion qualifies for fantasyland */
      if (OFC_CheckFantasyland(&sim_hand)) {
        fantasyland_simulations++;
      }
    }
  }

  /* Return fantasyland probability */
  if (total_simulations == 0) {
    return 0.0f;
  }

  return (float)fantasyland_simulations / (float)total_simulations;
}

/* Helper function to get risk level string */
static const char *get_risk_level_string(float risk) {
  if (risk < 0.20)
    return "LOW";
  else if (risk < 0.50)
    return "MEDIUM";
  else if (risk < 0.80)
    return "HIGH";
  else
    return "VERY HIGH";
}

/* Compare foul risk for placing a card in different positions */
int OFC_ComparePlacements(const ofc_player_hand_t *partial_hand, int card,
                          ofc_position_t positions[], int num_positions,
                          ofc_placement_result_t results[]) {
  int i;

  if (!partial_hand || !positions || !results || num_positions <= 0) {
    return -1;
  }

  /* Check if card is already used */
  if (!OFC_IsCardAvailable(partial_hand, card)) {
    return -1;
  }

  for (i = 0; i < num_positions; i++) {
    ofc_position_t pos = positions[i];
    results[i].position = pos;

    /* Check if position is valid (not overfilled) */
    int max_cards = (pos == OFC_TOP)      ? OFC_TOP_CARDS
                    : (pos == OFC_MIDDLE) ? OFC_MIDDLE_CARDS
                                          : OFC_BOTTOM_CARDS;

    if (partial_hand->card_count[pos] >= max_cards) {
      /* Position is full */
      results[i].is_valid = 0;
      results[i].foul_risk = 1.0f; /* 100% foul if can't place */
      results[i].risk_level = "INVALID";
    } else {
      /* Position is valid, calculate foul risk */
      results[i].is_valid = 1;
      results[i].foul_risk = OFC_CalculateFoulRisk(partial_hand, card, pos);
      results[i].risk_level = get_risk_level_string(results[i].foul_risk);
    }
  }

  return 0;
}

/* Get recommended position (lowest foul risk among valid positions) */
ofc_position_t
OFC_GetRecommendedPlacement(const ofc_placement_result_t results[],
                            int num_results) {
  int i;
  ofc_position_t best_position = OFC_TOP; /* Default fallback */
  float best_risk = 1.0f;                 /* Start with worst possible risk */
  int found_valid = 0;

  for (i = 0; i < num_results; i++) {
    if (results[i].is_valid && results[i].foul_risk < best_risk) {
      best_risk = results[i].foul_risk;
      best_position = results[i].position;
      found_valid = 1;
    }
  }

  /* If no valid position found, return -1 cast to position type */
  return found_valid ? best_position : (ofc_position_t)-1;
}

/* Calculate expected royalties for a partial hand in a specific position */
float OFC_CalculateExpectedRoyalties(const ofc_player_hand_t *partial_hand,
                                     ofc_position_t position) {
  const int NUM_SIMULATIONS = 2000;
  int available_cards[52];
  int num_available;
  float total_royalties = 0.0f;
  int valid_simulations = 0;
  int i;

  if (!partial_hand || position < OFC_TOP || position > OFC_BOTTOM) {
    return 0.0f;
  }

  /* If hand is already complete, calculate current royalties */
  if (OFC_IsComplete(partial_hand)) {
    ofc_royalties_t royalties;
    if (OFC_CalculateRoyalties(partial_hand, &royalties) == 0) {
      switch (position) {
      case OFC_TOP:
        return (float)royalties.top_royalties;
      case OFC_MIDDLE:
        return (float)royalties.middle_royalties;
      case OFC_BOTTOM:
        return (float)royalties.bottom_royalties;
      case OFC_NUM_HANDS:
      default:
        return 0.0f;
      }
    }
    return 0.0f;
  }

  /* Get available cards */
  num_available = OFC_GetAvailableCards(partial_hand, available_cards);
  if (num_available == 0) {
    return 0.0f;
  }

  /* Run Monte Carlo simulations */
  for (i = 0; i < NUM_SIMULATIONS; i++) {
    ofc_player_hand_t sim_hand = *partial_hand;

    /* Randomly complete the hand */
    if (OFC_RandomlyCompleteHand(&sim_hand, available_cards, num_available) ==
        0) {
      /* Only count valid hands (no fouls) */
      if (OFC_ValidateHierarchy(&sim_hand)) {
        ofc_royalties_t royalties;
        if (OFC_CalculateRoyalties(&sim_hand, &royalties) == 0) {
          valid_simulations++;

          /* Add royalties for the specified position */
          switch (position) {
          case OFC_TOP:
            total_royalties += (float)royalties.top_royalties;
            break;
          case OFC_MIDDLE:
            total_royalties += (float)royalties.middle_royalties;
            break;
          case OFC_BOTTOM:
            total_royalties += (float)royalties.bottom_royalties;
            break;
          case OFC_NUM_HANDS:
          default:
            break;
          }
        }
      }
    }
  }

  /* Return expected royalties */
  if (valid_simulations == 0) {
    return 0.0f;
  }

  return total_royalties / (float)valid_simulations;
}

/* ===== COMPLETE HAND ANALYSIS IMPLEMENTATION ===== */

/* Get hand strength rating (0-100) for a specific position */
int OFC_GetHandStrength(StdDeck_CardMask cards, int num_cards,
                        ofc_position_t position) {
  if (num_cards == 0) {
    return 0;
  }

  HandVal hand_val = OFC_EvaluateHand(cards, num_cards);
  int hand_type = HandVal_HANDTYPE(hand_val);

  switch (position) {
  case OFC_TOP: /* 3 cards */
    switch (hand_type) {
    case HandType_TRIPS:
      return 95; /* Very rare */
    case HandType_ONEPAIR:
      return 70; /* Strong in top */
    case HandType_NOPAIR:
      /* High cards are valuable in top */
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_ACE)
        return 60;
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_KING)
        return 40;
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_QUEEN)
        return 25;
      return 15;
    default:
      return 20;
    }
    break;

  case OFC_MIDDLE: /* 5 cards */
    switch (hand_type) {
    case HandType_STFLUSH:
      return 100; /* Perfect */
    case HandType_QUADS:
      return 98;
    case HandType_FULLHOUSE:
      return 95;
    case HandType_FLUSH:
      return 85;
    case HandType_STRAIGHT:
      return 80;
    case HandType_TRIPS:
      return 65;
    case HandType_TWOPAIR:
      return 45;
    case HandType_ONEPAIR:
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_ACE)
        return 35;
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_JACK)
        return 25;
      return 15;
    case HandType_NOPAIR:
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_ACE)
        return 20;
      return 10;
    default:
      return 25;
    }
    break;

  case OFC_BOTTOM: /* 5 cards */
    switch (hand_type) {
    case HandType_STFLUSH:
      return 100;
    case HandType_QUADS:
      return 98;
    case HandType_FULLHOUSE:
      return 90;
    case HandType_FLUSH:
      return 80;
    case HandType_STRAIGHT:
      return 75;
    case HandType_TRIPS:
      return 55;
    case HandType_TWOPAIR:
      return 40;
    case HandType_ONEPAIR:
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_ACE)
        return 30;
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_JACK)
        return 20;
      return 12;
    case HandType_NOPAIR:
      if (HandVal_TOP_CARD(hand_val) >= StdDeck_Rank_ACE)
        return 15;
      return 8;
    default:
      return 20;
    }
    break;

  case OFC_NUM_HANDS:
  default:
    return 0;
  }
}

/* Analyze a complete 13-card OFC hand */
int OFC_AnalyzeCompleteHand(const ofc_player_hand_t *hand,
                            ofc_complete_analysis_t *analysis) {
  if (!hand || !analysis) {
    return 1;
  }

  /* Clear analysis structure */
  memset(analysis, 0, sizeof(ofc_complete_analysis_t));

  /* Basic validation */
  analysis->is_foul = !OFC_ValidateHierarchy(hand);

  /* Calculate royalties */
  if (OFC_CalculateRoyalties(hand, &analysis->royalties) == 0) {
    analysis->total_royalties = analysis->royalties.top_royalties +
                                analysis->royalties.middle_royalties +
                                analysis->royalties.bottom_royalties;
  }

  /* Fantasyland checks */
  analysis->is_fantasyland = OFC_CheckFantasyland(hand);
  analysis->stay_fantasyland = OFC_CheckStayFantasyland(hand);

  /* Hand strength analysis */
  analysis->top_strength = OFC_GetHandStrength(
      hand->hands[OFC_TOP], hand->card_count[OFC_TOP], OFC_TOP);
  analysis->middle_strength = OFC_GetHandStrength(
      hand->hands[OFC_MIDDLE], hand->card_count[OFC_MIDDLE], OFC_MIDDLE);
  analysis->bottom_strength = OFC_GetHandStrength(
      hand->hands[OFC_BOTTOM], hand->card_count[OFC_BOTTOM], OFC_BOTTOM);

  /* Overall strength (weighted average) */
  analysis->overall_strength =
      (analysis->top_strength * 20 + analysis->middle_strength * 40 +
       analysis->bottom_strength * 40) /
      100;

  /* Risk assessment */
  if (analysis->is_foul) {
    analysis->foul_margin = 1.0f;
    analysis->risk_level = "FOULED";
  } else {
    /* Calculate foul margin based on hierarchy gaps */
    HandVal top_val =
        OFC_EvaluateHand(hand->hands[OFC_TOP], hand->card_count[OFC_TOP]);
    HandVal middle_val =
        OFC_EvaluateHand(hand->hands[OFC_MIDDLE], hand->card_count[OFC_MIDDLE]);
    HandVal bottom_val =
        OFC_EvaluateHand(hand->hands[OFC_BOTTOM], hand->card_count[OFC_BOTTOM]);

    /* Simple gap analysis */
    int middle_bottom_gap = bottom_val - middle_val;
    int top_middle_gap = middle_val - top_val;

    if (middle_bottom_gap > 1000 && top_middle_gap > 1000) {
      analysis->foul_margin = 0.0f;
      analysis->risk_level = "SAFE";
    } else if (middle_bottom_gap > 500 && top_middle_gap > 500) {
      analysis->foul_margin = 0.2f;
      analysis->risk_level = "MODERATE";
    } else {
      analysis->foul_margin = 0.8f;
      analysis->risk_level = "HIGH";
    }
  }

  /* Strategy rating based on overall performance */
  if (analysis->is_foul) {
    analysis->strategy_rating = "POOR";
    analysis->main_weakness = "Hand is fouled";
  } else if (analysis->overall_strength >= 80) {
    analysis->strategy_rating = "EXCELLENT";
    analysis->main_strength = "Strong hands across all positions";
  } else if (analysis->overall_strength >= 60) {
    analysis->strategy_rating = "GOOD";
    if (analysis->total_royalties >= 10) {
      analysis->main_strength = "Good royalty potential";
    } else {
      analysis->main_strength = "Solid hand hierarchy";
    }
  } else if (analysis->overall_strength >= 40) {
    analysis->strategy_rating = "AVERAGE";
    if (analysis->top_strength < 30) {
      analysis->main_weakness = "Weak top hand";
    } else if (analysis->middle_strength < 40) {
      analysis->main_weakness = "Weak middle hand";
    } else {
      analysis->main_weakness = "Weak bottom hand";
    }
  } else {
    analysis->strategy_rating = "POOR";
    analysis->main_weakness = "Overall weak hand construction";
  }

  return 0;
}

/* Calculate equity vs random opponent */
float OFC_CalculateEquityVsRandom(const ofc_player_hand_t *hand,
                                  int num_simulations) {
  const int NUM_SIMULATIONS = (num_simulations > 0) ? num_simulations : 1000;
  int wins = 0;
  int ties = 0;
  int total_games = 0;
  int i;

  /* Generate random opponent hands and compare */
  for (i = 0; i < NUM_SIMULATIONS; i++) {
    ofc_player_hand_t opponent_hand;
    StdDeck_CardMask dead;
    int available_cards[52];
    int num_available;

    /* Initialize opponent hand */
    OFC_InitializeHand(&opponent_hand);

    /* Mark our cards as dead */
    StdDeck_CardMask_OR(dead, hand->hands[OFC_TOP], hand->hands[OFC_MIDDLE]);
    StdDeck_CardMask_OR(dead, dead, hand->hands[OFC_BOTTOM]);

    /* Get available cards */
    num_available = OFC_GetAvailableCards(hand, available_cards);
    if (num_available < 13)
      continue; /* Not enough cards */

    /* Randomly complete opponent hand */
    if (OFC_RandomlyCompleteHand(&opponent_hand, available_cards,
                                 num_available) == 0) {
      /* Score the hands */
      ofc_score_t our_score, opponent_score;
      if (OFC_ScoreHands(hand, &opponent_hand, &our_score, &opponent_score) ==
          0) {
        total_games++;
        if (our_score.total_score > opponent_score.total_score) {
          wins++;
        } else if (our_score.total_score == opponent_score.total_score) {
          ties++;
        }
      }
    }
  }

  if (total_games == 0) {
    return 0.5f; /* Default to 50% if no games played */
  }

  return ((float)wins + 0.5f * (float)ties) / (float)total_games;
}

/* Calculate scoop probability vs random opponent */
float OFC_CalculateScoopProbability(const ofc_player_hand_t *hand,
                                    int num_simulations) {
  const int NUM_SIMULATIONS = (num_simulations > 0) ? num_simulations : 1000;
  int scoops = 0;
  int total_games = 0;
  int i;

  /* If hand is fouled, can't scoop */
  if (!OFC_ValidateHierarchy(hand)) {
    return 0.0f;
  }

  for (i = 0; i < NUM_SIMULATIONS; i++) {
    ofc_player_hand_t opponent_hand;
    int available_cards[52];
    int num_available;

    /* Initialize opponent hand */
    OFC_InitializeHand(&opponent_hand);

    /* Get available cards */
    num_available = OFC_GetAvailableCards(hand, available_cards);
    if (num_available < 13)
      continue;

    /* Randomly complete opponent hand */
    if (OFC_RandomlyCompleteHand(&opponent_hand, available_cards,
                                 num_available) == 0) {
      /* Check if we win all three hands */
      int top_win =
          OFC_CompareHands(hand->hands[OFC_TOP], hand->card_count[OFC_TOP],
                           opponent_hand.hands[OFC_TOP],
                           opponent_hand.card_count[OFC_TOP], OFC_TOP) > 0;
      int middle_win = OFC_CompareHands(hand->hands[OFC_MIDDLE],
                                        hand->card_count[OFC_MIDDLE],
                                        opponent_hand.hands[OFC_MIDDLE],
                                        opponent_hand.card_count[OFC_MIDDLE],
                                        OFC_MIDDLE) > 0;
      int bottom_win = OFC_CompareHands(hand->hands[OFC_BOTTOM],
                                        hand->card_count[OFC_BOTTOM],
                                        opponent_hand.hands[OFC_BOTTOM],
                                        opponent_hand.card_count[OFC_BOTTOM],
                                        OFC_BOTTOM) > 0;

      total_games++;
      if (top_win && middle_win && bottom_win) {
        scoops++;
      }
    }
  }

  if (total_games == 0) {
    return 0.0f;
  }

  return (float)scoops / (float)total_games;
}

/* Compare two complete hands head-to-head */
int OFC_CompareCompleteHands(const ofc_player_hand_t *hand1,
                             const ofc_player_hand_t *hand2,
                             ofc_complete_analysis_t *analysis1,
                             ofc_complete_analysis_t *analysis2,
                             ofc_score_t *score1, ofc_score_t *score2) {
  if (!hand1 || !hand2) {
    return 1;
  }

  /* Analyze both hands */
  if (analysis1 && OFC_AnalyzeCompleteHand(hand1, analysis1) != 0) {
    return 1;
  }
  if (analysis2 && OFC_AnalyzeCompleteHand(hand2, analysis2) != 0) {
    return 1;
  }

  /* Score the hands against each other */
  if (score1 && score2) {
    if (OFC_ScoreHands(hand1, hand2, score1, score2) != 0) {
      return 1;
    }
  }

  /* Add comparative analysis */
  if (analysis1 && analysis2) {
    /* Calculate equity vs this specific opponent (simplified) */
    if (score1->total_score > score2->total_score) {
      analysis1->equity_vs_average = 1.0f;
      analysis2->equity_vs_average = 0.0f;
    } else if (score1->total_score < score2->total_score) {
      analysis1->equity_vs_average = 0.0f;
      analysis2->equity_vs_average = 1.0f;
    } else {
      analysis1->equity_vs_average = 0.5f;
      analysis2->equity_vs_average = 0.5f;
    }

    /* Check for scoops */
    if (score1->points[OFC_TOP] > 0 && score1->points[OFC_MIDDLE] > 0 &&
        score1->points[OFC_BOTTOM] > 0) {
      analysis1->scoop_probability = 1.0f;
    }
    if (score2->points[OFC_TOP] > 0 && score2->points[OFC_MIDDLE] > 0 &&
        score2->points[OFC_BOTTOM] > 0) {
      analysis2->scoop_probability = 1.0f;
    }
  }

  return 0;
}

/* Print detailed analysis report */
void OFC_PrintCompleteAnalysis(const ofc_complete_analysis_t *analysis) {
  if (!analysis) {
    return;
  }

  printf("=== OFC Complete Hand Analysis ===\n\n");

  /* Basic Status */
  printf("Hand Status: %s\n", analysis->is_foul ? "FOULED" : "Valid");
  if (analysis->is_foul) {
    printf("Penalty: -6 points\n");
  }

  printf("Fantasyland: %s",
         analysis->is_fantasyland ? "Qualified" : "Not qualified");
  if (analysis->stay_fantasyland) {
    printf(" (Stays in FL)");
  }
  printf("\n\n");

  /* Royalties Breakdown */
  printf("Royalties Breakdown:\n");
  printf("  Top:    %2d points\n", analysis->royalties.top_royalties);
  printf("  Middle: %2d points\n", analysis->royalties.middle_royalties);
  printf("  Bottom: %2d points\n", analysis->royalties.bottom_royalties);
  printf("  Total:  %2d points\n\n", analysis->total_royalties);

  /* Hand Strength Analysis */
  printf("Hand Strength (0-100 scale):\n");
  printf("  Top:     %2d/100  ", analysis->top_strength);
  if (analysis->top_strength >= 80)
    printf("(Excellent)");
  else if (analysis->top_strength >= 60)
    printf("(Good)");
  else if (analysis->top_strength >= 40)
    printf("(Average)");
  else
    printf("(Weak)");
  printf("\n");

  printf("  Middle:  %2d/100  ", analysis->middle_strength);
  if (analysis->middle_strength >= 80)
    printf("(Excellent)");
  else if (analysis->middle_strength >= 60)
    printf("(Good)");
  else if (analysis->middle_strength >= 40)
    printf("(Average)");
  else
    printf("(Weak)");
  printf("\n");

  printf("  Bottom:  %2d/100  ", analysis->bottom_strength);
  if (analysis->bottom_strength >= 80)
    printf("(Excellent)");
  else if (analysis->bottom_strength >= 60)
    printf("(Good)");
  else if (analysis->bottom_strength >= 40)
    printf("(Average)");
  else
    printf("(Weak)");
  printf("\n");

  printf("  Overall: %2d/100  (%s)\n\n", analysis->overall_strength,
         analysis->strategy_rating);

  /* Risk Assessment */
  printf("Risk Assessment:\n");
  printf("  Foul Risk:   %s", analysis->risk_level);
  if (analysis->foul_margin > 0.0f) {
    printf(" (%.1f%% margin)", analysis->foul_margin * 100);
  }
  printf("\n\n");

  /* Strategy Analysis */
  printf("Strategy Analysis:\n");
  printf("  Rating: %s\n", analysis->strategy_rating);
  if (analysis->main_strength) {
    printf("  Main Strength: %s\n", analysis->main_strength);
  }
  if (analysis->main_weakness) {
    printf("  Main Weakness: %s\n", analysis->main_weakness);
  }

  /* Performance Metrics (if calculated) */
  if (analysis->equity_vs_average > 0.0f) {
    printf("\nPerformance vs Average Opponent:\n");
    printf("  Equity: %.1f%%\n", analysis->equity_vs_average * 100);
  }
  if (analysis->scoop_probability > 0.0f) {
    printf("  Scoop Probability: %.1f%%\n", analysis->scoop_probability * 100);
  }

  printf("\n");
}

/* Generate strategic advice based on analysis */
const char *OFC_GetStrategicAdvice(const ofc_complete_analysis_t *analysis) {
  if (!analysis) {
    return "Unable to analyze hand";
  }

  if (analysis->is_foul) {
    return "FOULED HAND: Focus on hierarchy validation. Bottom >= Middle >= "
           "Top is mandatory.";
  }

  if (analysis->overall_strength >= 80) {
    if (analysis->total_royalties >= 15) {
      return "EXCELLENT: Strong hand with good royalties. Play aggressively.";
    } else {
      return "EXCELLENT: Very solid construction. Consider more aggressive "
             "royalty plays.";
    }
  }

  if (analysis->overall_strength >= 60) {
    if (analysis->top_strength < 40) {
      return "GOOD: Strong overall but weak top. Focus on top hand quality in "
             "future hands.";
    }
    return "GOOD: Well-constructed hand with balanced strength across "
           "positions.";
  }

  if (analysis->overall_strength >= 40) {
    if (analysis->total_royalties >= 8) {
      return "AVERAGE: Decent royalties compensate for weak hands. Good "
             "risk/reward balance.";
    }
    if (analysis->top_strength < 30) {
      return "AVERAGE: Focus on improving top hand selection. Consider higher "
             "pairs earlier.";
    }
    return "AVERAGE: Solid fundamentals but room for improvement in hand "
           "strength.";
  }

  return "POOR: Major weaknesses in hand construction. Review OFC hierarchy "
         "and royalty strategy.";
}

/* ===== SIMD-OPTIMIZED API FUNCTIONS ===== */

/**
 * Enhanced API functions that leverage SIMD optimizations for maximum
 * performance
 */

/* Batch foul risk calculation with SIMD acceleration */
static int
OFC_CalculateMultipleFoulRisks(const ofc_player_hand_t *partial_hands,
                               const int *cards,
                               const ofc_position_t *positions, int num_hands,
                               int simulations_per_hand, float *foul_risks) {
  /* Delegate to SIMD implementation */
  return OFC_CalculateMultipleFoulRisksSIMD(partial_hands, cards, positions,
                                            num_hands, simulations_per_hand,
                                            foul_risks);
}

/* Batch complete hand analysis with SIMD acceleration */
static int OFC_AnalyzeMultipleCompleteHands(const ofc_player_hand_t *hands,
                                            int num_hands,
                                            ofc_complete_analysis_t *analyses) {
  if (!hands || !analyses || num_hands <= 0) {
    return -1;
  }

  /* Use SIMD for hierarchy validation if available */
  if (OFC_IsSIMDEnabled() && num_hands >= OFC_GetOptimalBatchSize()) {
    int *valid_flags = malloc(num_hands * sizeof(int));
    if (valid_flags) {
      /* Batch hierarchy validation using SIMD */
      if (OFC_ValidateHierarchySIMD(hands, num_hands, valid_flags,
                                    OFC_PROCESS_SIMD_AUTO) == 0) {
        /* Process individual analyses */
        for (int i = 0; i < num_hands; i++) {
          analyses[i].is_foul = !valid_flags[i];

          /* Calculate other analysis components */
          if (OFC_CalculateRoyalties(&hands[i], &analyses[i].royalties) == 0) {
            analyses[i].total_royalties =
                analyses[i].royalties.top_royalties +
                analyses[i].royalties.middle_royalties +
                analyses[i].royalties.bottom_royalties;
          }

          analyses[i].is_fantasyland = OFC_CheckFantasyland(&hands[i]);
          analyses[i].stay_fantasyland = OFC_CheckStayFantasyland(&hands[i]);
        }
      }
      free(valid_flags);
      return 0;
    }
  }

  /* Fallback to individual analysis */
  for (int i = 0; i < num_hands; i++) {
    if (OFC_AnalyzeCompleteHand(&hands[i], &analyses[i]) != 0) {
      return -1;
    }
  }

  return 0;
}

/* Enhanced compare placements with SIMD acceleration */
static int OFC_ComparePlacementsSIMD(const ofc_player_hand_t *partial_hand,
                                     int card, const ofc_position_t *positions,
                                     int num_positions,
                                     ofc_placement_result_t *results) {
  if (!partial_hand || !positions || !results || num_positions <= 0) {
    return -1;
  }

  /* Use SIMD batch processing if beneficial */
  if (OFC_IsSIMDEnabled() && num_positions >= 2) {
    /* Create arrays for batch processing */
    ofc_player_hand_t *partial_hands =
        malloc(num_positions * sizeof(ofc_player_hand_t));
    int *cards = malloc(num_positions * sizeof(int));
    float *foul_risks = malloc(num_positions * sizeof(float));

    if (partial_hands && cards && foul_risks) {
      /* Prepare batch */
      for (int i = 0; i < num_positions; i++) {
        partial_hands[i] = *partial_hand;
        cards[i] = card;
      }

      /* Calculate foul risks using SIMD */
      if (OFC_CalculateMultipleFoulRisksSIMD(partial_hands, cards, positions,
                                             num_positions, 1000,
                                             foul_risks) == 0) {

        /* Fill results structure */
        for (int i = 0; i < num_positions; i++) {
          results[i].position = positions[i];
          results[i].foul_risk = foul_risks[i];
          results[i].is_valid = 1;
          results[i].risk_level = (foul_risks[i] < 0.2f)   ? "LOW"
                                  : (foul_risks[i] < 0.5f) ? "MEDIUM"
                                  : (foul_risks[i] < 0.8f) ? "HIGH"
                                                           : "VERY HIGH";
        }
      }
    }

    free(partial_hands);
    free(cards);
    free(foul_risks);
    return 0;
  }

  /* Fallback to individual calculations */
  for (int i = 0; i < num_positions; i++) {
    results[i].position = positions[i];
    results[i].foul_risk =
        OFC_CalculateFoulRisk(partial_hand, card, positions[i]);
    results[i].is_valid = 1;
    results[i].risk_level = (results[i].foul_risk < 0.2f)   ? "LOW"
                            : (results[i].foul_risk < 0.5f) ? "MEDIUM"
                            : (results[i].foul_risk < 0.8f) ? "HIGH"
                                                            : "VERY HIGH";
  }

  return 0;
}

/* Batch equity calculation against random opponents using parallel processing
 */
static int OFC_CalculateMultipleEquityVsRandom(const ofc_player_hand_t *hands,
                                               int num_hands,
                                               int simulations_per_hand,
                                               float *equity_values) {
  if (!hands || !equity_values || num_hands <= 0 || simulations_per_hand <= 0) {
    return -1;
  }

#ifdef _OPENMP
  /* Parallel equity calculation using OpenMP */
  int num_threads = omp_get_max_threads();
  if (num_threads > num_hands) {
    num_threads = num_hands;
  }

#pragma omp parallel for schedule(dynamic, 1) num_threads(num_threads)
  for (int i = 0; i < num_hands; i++) {
    equity_values[i] =
        OFC_CalculateEquityVsRandom(&hands[i], simulations_per_hand);
  }
#else
  /* Sequential fallback when OpenMP is not available */
  for (int i = 0; i < num_hands; i++) {
    equity_values[i] =
        OFC_CalculateEquityVsRandom(&hands[i], simulations_per_hand);
  }
#endif

  return 0;
}

/* Initialize SIMD subsystem */
int OFC_InitializeSIMD(void) {
  /* Detect SIMD capabilities */
  int capabilities = OFC_DetectSIMDCapabilities();

  printf("OFC SIMD Capabilities: ");
  if (capabilities & OFC_SIMD_AVX512)
    printf("AVX-512 ");
  if (capabilities & OFC_SIMD_AVX2)
    printf("AVX2 ");
  if (capabilities & OFC_SIMD_SSE2)
    printf("SSE2 ");
  if (capabilities == OFC_SIMD_NONE)
    printf("None ");
  printf("\n");

  printf("Optimal batch size: %d\n", OFC_GetOptimalBatchSize());

  return capabilities;
}
