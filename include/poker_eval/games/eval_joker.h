/*
 * Copyright (C) 2002 Michael Maurer <mjmaurer@yahoo.com>
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
#ifndef __EVAL_JOKER_H__
#define __EVAL_JOKER_H__

#include <assert.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/games/rules_joker.h>
#include <poker_eval/core/eval.h>

/*
 * JokerDeck evaluator.
 */

#define SC sc
#define SD sd
#define SH sh
#define SS ss

// --- Start of new JokerDeck_JokerRules_EVAL_N ---
static inline HandVal
JokerDeck_JokerRules_EVAL_N(JokerDeck_CardMask joker_input_cards, int n_total_cards_in_hand)
{
    // If the joker bit is not set in the input mask, evaluate as a standard hand.
    if (!JokerDeck_CardMask_JOKER(joker_input_cards))
    {
        StdDeck_CardMask std_cards_only;
        JokerDeck_CardMask_toStd(joker_input_cards, std_cards_only); // Converts non-joker part
        return StdDeck_StdRules_EVAL_N(std_cards_only, n_total_cards_in_hand);
    }

    // Joker is present.
    int n_natural_cards = n_total_cards_in_hand - 1;
    StdDeck_CardMask natural_cards_std_mask;                             // Mask of non-joker cards
    JokerDeck_CardMask_toStd(joker_input_cards, natural_cards_std_mask); // Extracts only the standard cards part

    HandVal best_val_found = HandVal_NOTHING;

    // 1. Check for Five of a Kind (Quints)
    //    Quints are ranked higher than Straight Flushes in JokerRules_HandType.
    if (n_natural_cards >= 4)
    { // Need at least 4 natural cards to form Quints with a joker
        uint32 natural_ranks_count[JokerDeck_Rank_COUNT] = {0};
        for (int c_idx = 0; c_idx < StdDeck_N_CARDS; ++c_idx)
        { // Iterate through all 52 standard card indices
            if (StdDeck_CardMask_CARD_IS_SET(natural_cards_std_mask, c_idx))
            {
                natural_ranks_count[StdDeck_RANK(c_idx)]++;
            }
        }

        for (int r = JokerDeck_Rank_ACE; r >= JokerDeck_Rank_2; --r)
        {
            if (natural_ranks_count[r] == 4)
            { // Natural Four of a Kind for rank r
                // Joker makes it Quints of rank r.
                // This is the highest possible hand with a joker according to JokerRules_HandType.
                return HandVal_HANDTYPE_VALUE(JokerRules_HandType_QUINTS) +
                       HandVal_TOP_CARD_VALUE(r);
            }
        }
    }

    // 2. Iterate Joker as all possible standard cards to find the best standard hand type
    //    (Straight Flush, Quads, Full House, Flush, Straight, Trips, Two Pair, One Pair, No Pair)
    //    This loop will find the best hand if Quints were not made.
    for (int joker_as_card_idx = 0; joker_as_card_idx < StdDeck_N_CARDS; ++joker_as_card_idx)
    {
        // Joker cannot duplicate a card already naturally in hand.
        if (StdDeck_CardMask_CARD_IS_SET(natural_cards_std_mask, joker_as_card_idx))
        {
            continue;
        }

        StdDeck_CardMask current_trial_mask = natural_cards_std_mask;
        StdDeck_CardMask_SET(current_trial_mask, joker_as_card_idx); // Add joker as this specific card

        HandVal current_hand_val = StdDeck_StdRules_EVAL_N(current_trial_mask, n_natural_cards + 1);

        if (current_hand_val > best_val_found)
        {
            best_val_found = current_hand_val;
        }
    }

    // 3. Handle edge case: if only joker in hand (n_natural_cards == 0)
    //    or if all substitutions resulted in nothing (e.g. n_natural_cards + 1 was 0, though impossible here).
    //    The loop above should produce a valid HandVal if n_natural_cards + 1 >= 1.
    //    If n_natural_cards = 0, then n_total_cards_in_hand = 1 (only joker).
    //    The loop will try joker as all 52 cards. Best will be joker as Ace.
    if (n_natural_cards == 0 && n_total_cards_in_hand == 1)
    {
        // If best_val_found is still HandVal_NOTHING, it means loop didn't run or StdDeck_StdRules_EVAL_N returned NOTHING for single cards.
        // A single card (joker as Ace) is Ace High.
        // StdDeck_StdRules_EVAL_N should handle single cards correctly.
        // The loop should have set best_val_found to Ace High (e.g. joker as As).
        // If best_val_found is still NOTHING (which implies n_total_cards_in_hand was 0, or eval error),
        // then returning HandVal_NOTHING is appropriate.
    }

    return best_val_found;
}
// --- End of new JokerDeck_JokerRules_EVAL_N ---

#undef SC
#undef SH
#undef SD
#undef SS

#endif
