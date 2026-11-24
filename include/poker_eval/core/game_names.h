/*
 * game_names.h - Single table mapping game names to enum_game_t
 *
 * Every front end used to carry its own spelling table: pokenum's option
 * chain, the Python binding's name list and the documentation.  That is how
 * a new game (game_soko) could be registered in the core and still be
 * rejected by one of the front ends.  This module is the one place where a
 * spelling is defined.
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
#ifndef POKER_EVAL_GAME_NAMES_H
#define POKER_EVAL_GAME_NAMES_H

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve `name` to a game.  Matching is case-insensitive, ignores '-', '_'
 * and ASCII spaces, and folds the UTF-8 spellings of "ö" (as in "Sökö") to
 * 'o'.  Both the canonical names and the historical aliases (pokenum's "-h",
 * "-7s", "-soko"... short flags, "sokoless" spellings such as "canadianstud")
 * are accepted.  Returns 0 and writes *out on success, -1 otherwise.
 */
POKEREVAL_EXPORT int pe_game_from_name(const char *name, enum_game_t *out);

/* Canonical display name for a game, or NULL if the game has no entry. */
POKEREVAL_EXPORT const char *pe_game_canonical_name(enum_game_t game);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GAME_NAMES_H */
