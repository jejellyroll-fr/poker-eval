/*
 * transition_distribution.h - Board transition & turn/river hand improvement
 * probability matrix.
 *
 * ISSUE-06 (#162): exact combinatorial enumeration of the turn and river
 * streets for a fixed Hero hand and current board, producing:
 *   - the probability of improving to specific hand categories (flush,
 *     straight, pair, set, full house) on the turn and by the river;
 *   - a category transition matrix M[turn_cat][river_cat] = P(river category
 *     | turn category), the Markov transition between the two board streets.
 *
 * The engine evaluates the best 5-card high hand among the combined
 * Hero + board + street cards using the standard StdDeck evaluator
 * (StdDeck_StdRules_EVAL_N), so it is correct for any high-hand game
 * (hold'em, omaha, stud, ...) simply by varying the number of cards held.
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

#ifndef POKER_EVAL_TRANSITION_DISTRIBUTION_H
#define POKER_EVAL_TRANSITION_DISTRIBUTION_H

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/pokereval_export.h>

/* Per-category improvement statistics. Each probability is in [0, 1].
 *
 * Flush/straight probabilities count a straight flush as completing the draw
 * (a straight flush is a flush and a straight), matching the standard
 * "outs" accounting used by the acceptance criteria. */
typedef struct {
    double prob_turn_flush;      /* P(completing a flush on the turn)   */
    double prob_river_flush;     /* P(completing a flush by the river)  */
    double prob_turn_straight;   /* P(completing a straight on the turn)*/
    double prob_river_straight;  /* P(completing a straight by the river)*/
    double prob_turn_pair;       /* P(turn hand is exactly a pair)      */
    double prob_river_pair;      /* P(river hand is exactly a pair)     */
    double prob_turn_set;        /* P(turn hand is exactly trips)       */
    double prob_river_fullhouse; /* P(river hand is exactly a full house)*/
    double prob_no_improvement;  /* P(river category not strictly better
                                      than the starting board category)   */
} pe_hand_improvement_stats_t;

/* Complete result of a transition analysis.
 *
 * transition_matrix is indexed by the standard hand types (see handval.h):
 *     0 = High Card ... 8 = Straight Flush.
 * Rows are the turn category, columns the river category, so
 * transition_matrix[i][j] = P(river category = j | turn category = i).
 * The matrix is 12x12 to allow for future extended category sets; only
 * indices 0..8 are populated by the standard evaluator. */
typedef struct {
    double transition_matrix[12][12]; /* [turn_cat][river_cat] */
    int   start_category;             /* category of Hero + current board  */
    int   num_remaining;              /* size of the remaining deck         */
    pe_hand_improvement_stats_t stats;
} pe_transition_result_t;

/* Category index helpers (identity mapping onto StdRules_HandType). */
#define PE_TRANSITION_MAX_CATEGORY 12

/* Human-readable name for a transition category index (NULL if unknown). */
POKEREVAL_EXPORT const char *pe_transition_category_name(int category);

/*
 * @brief Computes exact category transition and improvement probabilities from
 *        the current board.
 *
 * @param game          Game context (used for validation / future game-specific
 *                      handling; the evaluator itself is the standard best-5
 *                      high-hand evaluator over Hero + board + streets).
 * @param hero_hand     Hero's pocket cards (mask).
 * @param current_board The current board (flop or turn).
 * @param dead_cards    Additional cards known to be unavailable (mask).
 * @param out_result    Output structure, zero-initialised on success.
 *
 * @return 0 on success, -1 on invalid arguments.
 */
POKEREVAL_EXPORT int pe_compute_transition_distribution(
    enum_game_t game,
    StdDeck_CardMask hero_hand,
    StdDeck_CardMask current_board,
    StdDeck_CardMask dead_cards,
    pe_transition_result_t *out_result);

#endif /* POKER_EVAL_TRANSITION_DISTRIBUTION_H */
