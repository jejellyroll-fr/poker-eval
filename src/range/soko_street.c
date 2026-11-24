/*
 * soko_street.c - Street-aware Sökö equity
 *
 * See soko_street.h.  A street is only a description of the cards a player is
 * known to hold, so this file builds one pocket per player and leaves the
 * dealing to the shared enumerator.
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
#include <poker_eval/games/soko_street.h>
#include <poker_eval/range.h>

/*
 * Validate one player's street and return their known cards.  A dead card
 * that a player holds is an argument error: the shared engine also adds
 * selected hands to its dead set, and a card present in both makes the
 * matchup count collapse to zero instead of producing a clear error.
 */
static pe_status_t known_cards(const pe_soko_street_player_t *player,
                               StdDeck_CardMask dead_cards,
                               StdDeck_CardMask *known) {
    StdDeck_CardMask up_cards, down_cards;
    int up, down;

    if (player == NULL || known == NULL)
        return PE_STATUS_INVALID_ARG;
    if (player->street < PE_SOKO_STREET_THIRD ||
        player->street > PE_SOKO_STREET_FIFTH)
        return PE_STATUS_INVALID_ARG;

    /* StdDeck_numCards takes a non-const mask pointer. */
    up_cards = player->up_cards;
    down_cards = player->down_cards;
    up = StdDeck_numCards(up_cards);
    down = StdDeck_numCards(down_cards);
    if (up != (int)player->street - 1 || down > 1)
        return PE_STATUS_INVALID_ARG;
    if (StdDeck_CardMask_ANY_SET(player->up_cards, player->down_cards))
        return PE_STATUS_INVALID_ARG;
    if (StdDeck_CardMask_ANY_SET(dead_cards, player->up_cards) ||
        StdDeck_CardMask_ANY_SET(dead_cards, player->down_cards))
        return PE_STATUS_INVALID_ARG;

    StdDeck_CardMask_RESET(*known);
    StdDeck_CardMask_OR(*known, player->up_cards, player->down_cards);
    return PE_STATUS_OK;
}

/* A one-combo range holding only the cards the player is known to hold.  The
 * equity engine deals the missing cards for each matchup, which is both
 * cheaper than pre-expanding C(49,2) completions and the only form in which
 * the iteration count means what it says. */
static pe_status_t build_known_range(StdDeck_CardMask known,
                                     pe_range_t **out_range) {
    pe_status_t status;
    pe_range_t *range;

    status = pe_range_create(game_soko, &range);
    if (status != PE_STATUS_OK)
        return status;
    /* pe_range_create() already owns a combo array: fill its first slot
     * rather than replacing it, which would leak the allocation. */
    range->combos[0].hand = known;
    range->combos[0].weight = 1.0;
    range->count = 1;
    range->capacity = 1;
    range->total_weight = 1.0;
    range->game_type = game_soko;
    *out_range = range;
    return PE_STATUS_OK;
}

pe_status_t pe_equity_soko_street(const pe_soko_street_player_t *players,
                                  int num_players,
                                  StdDeck_CardMask dead_cards,
                                  const pe_equity_opts_t *opts,
                                  pe_equity_result_multi_t *out_result) {
    const pe_range_t *ranges[10];
    pe_range_t *owned[10] = {NULL};
    StdDeck_CardMask used, known, board;
    pe_status_t status = PE_STATUS_OK;
    int i;

    if (players == NULL || out_result == NULL)
        return PE_STATUS_INVALID_ARG;
    if (num_players < 2 || num_players > 10)
        return PE_STATUS_INVALID_ARG;

    /* Validate and build in one pass.  A card held by two players is an
     * argument error here: the shared engine would otherwise just skip the
     * matchup and report zero samples. */
    StdDeck_CardMask_RESET(used);
    for (i = 0; i < num_players; ++i) {
        status = known_cards(&players[i], dead_cards, &known);
        if (status != PE_STATUS_OK)
            break;
        if (StdDeck_CardMask_ANY_SET(used, known)) {
            status = PE_STATUS_INVALID_ARG;
            break;
        }
        StdDeck_CardMask_OR(used, used, known);
        status = build_known_range(known, &owned[i]);
        if (status != PE_STATUS_OK)
            break;
        ranges[i] = owned[i];
    }

    if (status == PE_STATUS_OK) {
        StdDeck_CardMask_RESET(board);
        status = pe_equity_calculate(game_soko, ranges, num_players, board,
                                     dead_cards, opts, out_result);
    }

    for (i = 0; i < num_players; ++i)
        pe_range_free(owned[i]);
    return status;
}
