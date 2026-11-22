/*
 * soko.h - Sökö (Canadian Stud) showdown description
 *
 * The generic evaluators (best(), best_hand_value(), evaln()) answer with the
 * standard five-card HandVal, which has no way to encode a four-card flush or
 * a four-card straight.  For Sökö that answer is wrong for exactly the hands
 * the game is about, so this module exposes the Sökö category and its
 * tie-break ranks directly instead of forcing them into a standard HandVal.
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
#ifndef POKER_EVAL_GAMES_SOKO_H
#define POKER_EVAL_GAMES_SOKO_H

#include <poker_eval/core/configurable_ranking.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Description of a five-card Sökö hand.
 *
 * `category` is the semantic category and `category_name` its stable spelling
 * ("FourCardFlush", "FourCardStraight", ...).  `category_rank` is the
 * configured strength index: larger always beats smaller.
 *
 * `tiebreak_ranks` holds the ranks that decide ties inside the category, most
 * significant first, as display ranks 2..14 (11 = jack, 14 = ace).  A
 * four-card flush lists its four suited ranks only; a four-card straight
 * lists the high card of the run only.  Those are the two places where Sökö
 * differs from the standard order.
 */
typedef struct {
    pe_hand_category_t category;
    const char *category_name;
    int category_rank;
    int num_tiebreak_ranks;
    int tiebreak_ranks[5];
} pe_soko_hand_description_t;

/*
 * Describe a complete five-card Sökö hand under the official Sökö ranking.
 *
 * Returns PE_STATUS_OK on success, PE_STATUS_INVALID_ARG when `hand` does not
 * hold exactly five cards or `out` is NULL.
 */
POKEREVAL_EXPORT pe_status_t pe_soko_describe_hand(
    StdDeck_CardMask hand,
    pe_soko_hand_description_t *out
);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GAMES_SOKO_H */
