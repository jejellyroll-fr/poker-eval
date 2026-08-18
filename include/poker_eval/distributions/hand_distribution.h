/*
 * hand_distribution.h - Automated hand distribution & combinatorial
 * probability engine.
 *
 * ISSUE-02 (#158): exact combinatorial enumeration of every k-card hand
 * of a deck and classification of each hand into a ranking category,
 * producing the probability distribution described in Mark Bollman's
 * "Intermediate Poker Mathematics".
 *
 * This module builds directly on the ISSUE-03 (#159) generalized deck
 * abstraction (pe_deck_spec_t) and the ISSUE-04 (#160) configurable
 * ranking rules (pe_hand_ranking_config_t). A deck of N cards and a
 * hand size k yield C(N, k) hands; each is evaluated once and tallied
 * into its category, so the reported counts are exact (sampled estimation
 * is intentionally out of scope for the single-hand distribution).
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

#ifndef POKER_EVAL_HAND_DISTRIBUTION_H
#define POKER_EVAL_HAND_DISTRIBUTION_H

#include <stdio.h>
#include <stdint.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/pokereval_export.h>
#include <poker_eval/deck/generalized_deck.h>
#include <poker_eval/core/configurable_ranking.h>

/* Maximum number of distinct categories a distribution can report. The
 * configurable ranking table is bounded well below this. */
#define PE_HAND_DIST_MAX_CATEGORIES 16

/* A single category's contribution to the overall distribution. */
typedef struct {
    int         category_id;   /* configured rank index (0 = weakest) */
    const char *category_name; /* human-readable category name */
    uint64_t    count;         /* number of hands of this category */
    double      probability;   /* count / total_combinations */
    double      cumulative_probability; /* cumulative from strongest down */
} pe_category_stat_t;

/* Complete combinatorial distribution for one (deck, ranking, hand size)
 * combination. Categories are stored strongest-first so the table reads
 * top-to-bottom the way Bollman's reference tables are presented. */
typedef struct {
    enum_game_t   game;        /* originating game, or game_NUMGAMES if none */
    char          deck_name[32];   /* deck preset / spec name */
    char          ranking_name[32];/* ranking preset name */
    int           deck_size;
    int           hand_size;
    uint64_t      total_combinations;
    int           num_categories;
    pe_category_stat_t categories[PE_HAND_DIST_MAX_CATEGORIES];
} pe_hand_distribution_t;

/*
 * Core: enumerate every C(deck_size, hand_size) hand and tally it into its
 * ranking category. Exact (no sampling). Returns 0 on success or -1 on
 * invalid arguments. deck_name/ranking_name are left as-is (NULL) by this
 * function; the preset/game helpers fill them in.
 */
POKEREVAL_EXPORT int pe_compute_hand_distribution_ex(
    const pe_deck_spec_t *deck_spec,
    const pe_hand_ranking_config_t *config,
    int hand_size,
    pe_hand_distribution_t *out_dist);

/*
 * Convenience: compute the distribution for a named deck preset and ranking
 * preset. deck_name/ranking_name are populated from the preset names. Returns
 * 0 on success, -1 on an unknown preset or invalid arguments.
 */
POKEREVAL_EXPORT int pe_compute_hand_distribution_for_preset(
    const char *deck_preset,
    const char *ranking_preset,
    int hand_size,
    pe_hand_distribution_t *out_dist);

/*
 * Convenience: map a game to its (deck, ranking, hand size) and compute the
 * distribution. Supported: standard-deck stud/hold'em/omaha/lowball variants,
 * short-deck hold'em, and Manila. Returns 0 on success or -1 for an
 * unsupported game.
 */
POKEREVAL_EXPORT int pe_compute_hand_distribution(
    enum_game_t game,
    pe_hand_distribution_t *out_dist);

/*
 * Formatting helpers. Both return 0 on success or -1 on error. The markdown
 * helper emits a Bollman-style table; the JSON helper emits a single JSON
 * object describing the distribution.
 */
POKEREVAL_EXPORT int pe_hand_distribution_print_markdown(
    const pe_hand_distribution_t *dist, FILE *out);
POKEREVAL_EXPORT int pe_hand_distribution_print_json(
    const pe_hand_distribution_t *dist, FILE *out);

#endif /* POKER_EVAL_HAND_DISTRIBUTION_H */
