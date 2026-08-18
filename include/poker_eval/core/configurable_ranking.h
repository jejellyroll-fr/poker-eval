/*
 * configurable_ranking.h - Configurable hand ranking rules
 *
 * ISSUE-04 (#160): a data-driven rule system where category ordering and
 * subset definitions are configurable rather than hardcoded in standalone
 * evaluator C files. This module builds on the generalized deck abstraction
 * from ISSUE-03 (#159) and evaluates a hand against a configurable ranking
 * configuration, returning a normalized HandVal whose HANDTYPE field encodes
 * the category's *rank* in the configured ordering (so that a numerically
 * larger HandVal always corresponds to a stronger hand).
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
#ifndef POKER_EVAL_CONFIGURABLE_RANKING_H
#define POKER_EVAL_CONFIGURABLE_RANKING_H

#include <stddef.h>
#include <stdint.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/deck/generalized_deck.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Semantic hand categories. These are *kind* identifiers, independent of any
 * particular ranking order. The configured order (pe_hand_ranking_config_t
 * below) decides which category beats which.
 */
typedef enum {
    PE_CAT_HIGH_CARD = 0,
    PE_CAT_ONE_PAIR,
    PE_CAT_TWO_PAIR,
    PE_CAT_THREE_OF_A_KIND,
    PE_CAT_FOUR_CARD_STRAIGHT, /* For Canadian / New York Stud */
    PE_CAT_FOUR_CARD_FLUSH,    /* For Canadian / New York Stud */
    PE_CAT_STRAIGHT,
    PE_CAT_FLUSH,
    PE_CAT_FULL_HOUSE,
    PE_CAT_FOUR_OF_A_KIND,
    PE_CAT_STRAIGHT_FLUSH,
    PE_CAT_FIVE_OF_A_KIND,      /* For Wildcard / Joker games */
    PE_CAT_COUNT
} pe_hand_category_t;

/*
 * Configurable ranking configuration.
 *
 * category_order lists the active categories in ascending strength order:
 * index 0 is the weakest category, index (num_active_categories - 1) is the
 * strongest. The evaluator normalizes the returned HandVal so that the
 * HANDTYPE field equals the category's index in this array; a larger index
 * therefore always means a stronger hand, and kickers are encoded below so
 * that stronger kickers within the same category compare larger too.
 *
 * The boolean flags are convenience knobs; presets populate both the flags
 * and the ordering consistently.
 */
typedef struct {
    pe_hand_category_t category_order[16];
    int num_active_categories;
    int allow_4card_straights;
    int allow_4card_flushes;
    int flush_beats_fullhouse;
    int ace_low_straight_allowed;
} pe_hand_ranking_config_t;

/* Initialize config to the standard (StdDeck) ranking order. Returns 0. */
POKEREVAL_EXPORT int pe_ranking_config_init_default(pe_hand_ranking_config_t *config);

/*
 * Populate config from a named preset. Supported names:
 *   "standard", "short_deck", "italian_manila", "canadian_stud", "new_york_stud"
 * Returns 0 on success, -1 if the preset name is unknown.
 */
POKEREVAL_EXPORT int pe_ranking_config_set_preset(
    const char *preset_name,
    pe_hand_ranking_config_t *config
);

/*
 * Evaluate a hand (whose cards are set in hand_mask, num_cards cards total)
 * against the configured ranking rules for the given deck spec. Returns a
 * normalized HandVal: larger is always better, and the HANDTYPE field equals
 * the category's rank index in config->category_order.
 *
 * For 5-card variants num_cards should be 5; for 4-card category variants
 * (Canadian / New York Stud) num_cards is 4 or 5 (the 4-card categories are
 * detected among the supplied cards). Returns HandVal_NOTHING on invalid
 * input (NULL pointers, empty hand, or a category not present in the config).
 */
POKEREVAL_EXPORT HandVal pe_eval_configurable_hand(
    const pe_deck_spec_t *deck_spec,
    const pe_hand_ranking_config_t *config,
    pe_card_mask_t hand_mask,
    int num_cards
);

/* Extract the configured category rank (index into category_order) from a HandVal. */
POKEREVAL_EXPORT int pe_ranking_category_rank(HandVal handval);

/* Return the configured rank of a category (its index in config->category_order),
 * or -1 if the category is not active in this configuration. */
POKEREVAL_EXPORT int pe_ranking_config_category_rank(const pe_hand_ranking_config_t *config,
                                                     pe_hand_category_t category);

/* Map a category rank back to its pe_hand_category_t (or -1 if out of range). */
POKEREVAL_EXPORT pe_hand_category_t pe_ranking_category_from_rank(int rank);

/* Human-readable name for a category (for debugging / tests). */
POKEREVAL_EXPORT const char *pe_ranking_category_name(pe_hand_category_t category);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CONFIGURABLE_RANKING_H */
