/*
 * soko_street.h - Street-aware Sökö equity
 *
 * A Sökö player holds one downcard and four upcards.  On third street only the
 * first two upcards are out, on fourth street three, on fifth street four.
 * The upcards are visible to the table; the downcard is not.
 *
 * pe_equity_soko_street() turns that description into ordinary ranges of
 * complete five-card hands, so the shared equity engine can enumerate the
 * cards still to come without knowing anything about stud streets:
 *
 *   - each player's range is a single pocket of the cards they are known to
 *     hold (visible upcards plus a known downcard);
 *   - the shared enumerator deals the missing cards from the deck minus every
 *     card held or shown and minus the caller's dead cards, so the iteration
 *     count means what it says on third and fourth street;
 *   - the resulting ranges are handed to pe_equity_calculate(game_soko, ...).
 *
 * Omitting a player's downcard models "I do not know their hole card": the
 * enumeration then fills it together with the future upcards.  This is a
 * showdown-equity model (what the pot is worth given the information now),
 * not a street-by-street betting model.
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
#ifndef POKER_EVAL_GAMES_SOKO_STREET_H
#define POKER_EVAL_GAMES_SOKO_STREET_H

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity.h>
#include <poker_eval/range.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of visible cards a player has on each street (their value is the
 * number of cards dealt so far, which is also the street number). */
typedef enum {
    PE_SOKO_STREET_THIRD = 3,
    PE_SOKO_STREET_FOURTH = 4,
    PE_SOKO_STREET_FIFTH = 5
} pe_soko_street_t;

/*
 * One player's information at a street.
 *
 * `up_cards` must hold exactly street - 1 cards, `down_cards` zero or one.
 * Leaving `down_cards` empty means the hole card is unknown, which is the
 * usual description of an opponent.
 */
typedef struct {
    pe_soko_street_t street;
    StdDeck_CardMask up_cards;
    StdDeck_CardMask down_cards;
} pe_soko_street_player_t;

/*
 * Build every complete five-card Sökö hand consistent with `player` and
 * `dead_cards`.  Combos are unweighted (each completion is one equally likely
 * showdown hand).  The returned range must be released with pe_range_free().
 *
 * Returns PE_STATUS_OK, PE_STATUS_INVALID_ARG for a street/upcard mismatch,
 * overlapping up/down cards or a dead card that is also held, and
 * PE_STATUS_OUT_OF_MEMORY on allocation failure.
 */
POKEREVAL_EXPORT pe_status_t pe_soko_street_range(
    const pe_soko_street_player_t *player,
    StdDeck_CardMask dead_cards,
    pe_range_t **out_range
);

/*
 * Showdown equity for 2..10 Sökö players at the same street.
 *
 * Every player's visible cards are removed from the deck for every other
 * player, so an upcard can never be dealt twice.  `opts` follows
 * pe_equity_calculate(): NULL means the automatic method choice with its
 * default iteration count; iterations > 0 with is_monte_carlo = 1 forces
 * sampling (useful on third street, where the exact ranges are large).
 *
 * Returns PE_STATUS_OK, PE_STATUS_INVALID_ARG for an invalid street, an
 * upcard count that does not match the street, two players sharing a card or
 * a dead card that is also held, or whatever pe_equity_calculate() returns.
 */
POKEREVAL_EXPORT pe_status_t pe_equity_soko_street(
    const pe_soko_street_player_t *players,
    int num_players,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *out_result
);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GAMES_SOKO_STREET_H */
