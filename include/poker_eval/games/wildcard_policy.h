/*
 * wildcard_policy.h - Generalized wildcard policy
 *
 * ISSUE-05 (#161): a flexible policy describing wildcard rules used in home
 * games, casino poker and video poker: multiple jokers, designated wild
 * ranks (Deuces Wild / One-Eyed Jacks / Suicide King), the Bug rule, and
 * low-card-in-hand wild. The policy drives pe_eval_wildcard_hand, which
 * performs a substitution search over the deck and returns the best possible
 * HandVal for the supplied cards.
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
#ifndef POKER_EVAL_WILDCARD_POLICY_H
#define POKER_EVAL_WILDCARD_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/deck/generalized_deck.h>
#include <poker_eval/core/configurable_ranking.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a wild card may behave when substituted. */
typedef enum {
    PE_WILD_BEHAVIOR_FULLY_WILD = 0, /* Evaluates to the best possible rank/suit */
    PE_WILD_BEHAVIOR_BUG_RULE   = 1, /* Acts as Ace, or filler for Straight/Flush */
    PE_WILD_BEHAVIOR_LOW_CARD   = 2  /* Lowest natural rank in hand is wild */
} pe_wild_behavior_t;

/*
 * Generalized wildcard policy.
 *
 * wild_rank_mask uses the same 0-based global rank indexing as
 * pe_deck_spec_t::active_rank_mask: bit 0 = lowest rank ("2" in a standard
 * deck), bit 12 = Ace. For example Deuces Wild uses PE_WILD_DEUCES.
 */
typedef struct {
    pe_wild_behavior_t behavior;
    uint16_t wild_rank_mask;       /* Bitmask of wild ranks (bit r = global rank r) */
    int num_jokers;                /* Number of joker cards treated as wild */
    int is_one_eyed_jacks_wild;    /* Jacks of the one-eyed suits are wild */
    int is_suicide_king_wild;      /* King of the suicide suit is wild */
    int allow_five_of_a_kind;      /* Recognize the Five-of-a-Kind category */
} pe_wildcard_policy_t;

/* Helpers for building wild_rank_mask. r is the 0-based global rank. */
#define PE_WILD_RANK_BIT(r)  ((uint16_t)(1u << (r)))
#define PE_WILD_DEUCES       PE_WILD_RANK_BIT(0)   /* rank "2" */
#define PE_WILD_TREYS        PE_WILD_RANK_BIT(1)   /* rank "3" */
#define PE_WILD_FOURS        PE_WILD_RANK_BIT(2)   /* rank "4" */
#define PE_WILD_ACES         PE_WILD_RANK_BIT(12)  /* rank "A" */

/*
 * Initialize a policy with the given behavior. wild_ranks seeds
 * wild_rank_mask, num_jokers seeds num_jokers. The remaining flags default
 * sensibly (allow_five_of_a_kind is enabled for fully-wild / low-card
 * policies and disabled for the Bug rule) and may be overridden after init.
 * Returns 0 on success, -1 on invalid input.
 */
POKEREVAL_EXPORT int pe_wildcard_policy_init(
    pe_wild_behavior_t behavior,
    uint16_t wild_ranks,
    int num_jokers,
    pe_wildcard_policy_t *out_policy
);

/*
 * Evaluate a hand (hand_mask with num_cards cards) under the wildcard policy
 * for the given deck spec. Returns the best possible HandVal: a larger value
 * always corresponds to a stronger hand. Returns HandVal_NOTHING on invalid
 * input (NULL pointers or an empty hand).
 */
POKEREVAL_EXPORT HandVal pe_eval_wildcard_hand(
    const pe_deck_spec_t *deck_spec,
    const pe_wildcard_policy_t *policy,
    pe_card_mask_t hand_mask,
    int num_cards
);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_WILDCARD_POLICY_H */
