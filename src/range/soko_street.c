/*
 * soko_street.c - Street-aware Sökö equity
 *
 * See soko_street.h.  The implementation deliberately does not touch the
 * enumerator: a street is expressed as a range of complete hands, and the
 * shared game_soko enumeration deals the rest exactly as it does for any
 * other range.
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
#include <stdlib.h>
#include <string.h>

#include <poker_eval/games/soko_street.h>

/* Binomial coefficient for the small numbers involved (n <= 52, k <= 3). */
static size_t binom(size_t n, size_t k) {
    size_t result = 1;
    size_t i;

    if (k > n)
        return 0;
    if (k > n - k)
        k = n - k;
    for (i = 1; i <= k; ++i)
        result = result * (n - k + i) / i;
    return result;
}

/* Every card of the standard deck that is in `mask`. */
static int collect_cards(StdDeck_CardMask mask, int *out) {
    int card;
    int n = 0;

    for (card = 0; card < StdDeck_N_CARDS; ++card) {
        StdDeck_CardMask one;
        StdDeck_CardMask_RESET(one);
        StdDeck_CardMask_SET(one, card);
        if (StdDeck_CardMask_ANY_SET(mask, one))
            out[n++] = card;
    }
    return n;
}

static void fill_combos(const int *cards, int n, int need, int start, int depth,
                        StdDeck_CardMask base, pe_combo_t *combos,
                        size_t *next) {
    int i;

    if (depth == need) {
        combos[*next].hand = base;
        combos[*next].weight = 1.0;
        ++*next;
        return;
    }
    for (i = start; i <= n - (need - depth); ++i) {
        StdDeck_CardMask hand = base;
        StdDeck_CardMask_SET(hand, cards[i]);
        fill_combos(cards, n, need, i + 1, depth + 1, hand, combos, next);
    }
}

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

/* Build the range of five-card completions of `known`, drawing only from
 * cards outside `excluded`. */
static pe_status_t build_range_from_known(StdDeck_CardMask known,
                                          StdDeck_CardMask excluded,
                                          pe_range_t **out_range) {
    pe_status_t status;
    pe_range_t *range;
    int free_cards[StdDeck_N_CARDS];
    size_t num_free, need, total, used = 0;
    int card;

    num_free = 0;
    for (card = 0; card < StdDeck_N_CARDS; ++card) {
        StdDeck_CardMask one;
        StdDeck_CardMask_RESET(one);
        StdDeck_CardMask_SET(one, card);
        if (!StdDeck_CardMask_ANY_SET(excluded, one))
            free_cards[num_free++] = card;
    }

    need = (size_t)(5 - StdDeck_numCards(known));
    total = binom(num_free, need);
    if (total == 0)
        return PE_STATUS_INVALID_ARG;

    status = pe_range_create(game_soko, &range);
    if (status != PE_STATUS_OK)
        return status;

    range->combos = (pe_combo_t *)calloc(total, sizeof(pe_combo_t));
    if (range->combos == NULL) {
        pe_range_free(range);
        return PE_STATUS_OUT_OF_MEMORY;
    }
    range->capacity = total;

    fill_combos(free_cards, (int)num_free, (int)need, 0, 0, known,
                range->combos, &used);
    range->count = used;
    range->total_weight = (double)used;
    range->game_type = game_soko;
    *out_range = range;
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
    range->combos = (pe_combo_t *)calloc(1, sizeof(pe_combo_t));
    if (range->combos == NULL) {
        pe_range_free(range);
        return PE_STATUS_OUT_OF_MEMORY;
    }
    range->combos[0].hand = known;
    range->combos[0].weight = 1.0;
    range->count = 1;
    range->capacity = 1;
    range->total_weight = 1.0;
    range->game_type = game_soko;
    *out_range = range;
    return PE_STATUS_OK;
}

pe_status_t pe_soko_street_range(const pe_soko_street_player_t *player,
                                 StdDeck_CardMask dead_cards,
                                 pe_range_t **out_range) {
    StdDeck_CardMask known, excluded;
    pe_status_t status;

    if (out_range == NULL)
        return PE_STATUS_INVALID_ARG;
    *out_range = NULL;

    status = known_cards(player, dead_cards, &known);
    if (status != PE_STATUS_OK)
        return status;

    StdDeck_CardMask_OR(excluded, dead_cards, known);
    return build_range_from_known(known, excluded, out_range);
}

pe_status_t pe_equity_soko_street(const pe_soko_street_player_t *players,
                                  int num_players,
                                  StdDeck_CardMask dead_cards,
                                  const pe_equity_opts_t *opts,
                                  pe_equity_result_multi_t *out_result) {
    const pe_range_t *ranges[10];
    pe_range_t *owned[10];
    StdDeck_CardMask all_known, known;
    StdDeck_CardMask board;
    pe_status_t status = PE_STATUS_OK;
    int i, j;

    if (players == NULL || out_result == NULL)
        return PE_STATUS_INVALID_ARG;
    if (num_players < 2 || num_players > 10)
        return PE_STATUS_INVALID_ARG;

    StdDeck_CardMask_RESET(all_known);
    for (i = 0; i < num_players; ++i) {
        status = known_cards(&players[i], dead_cards, &known);
        if (status != PE_STATUS_OK)
            return status;
        /* Two players cannot hold the same card. */
        if (StdDeck_CardMask_ANY_SET(all_known, known))
            return PE_STATUS_INVALID_ARG;
        StdDeck_CardMask_OR(all_known, all_known, known);
    }

    /* Build each range against every known card, so no completion can deal a
     * card another player is already showing or holding. */
    for (i = 0; i < 10; ++i) {
        ranges[i] = NULL;
        owned[i] = NULL;
    }
    for (i = 0; i < num_players; ++i) {
        status = known_cards(&players[i], dead_cards, &known);
        if (status != PE_STATUS_OK)
            goto cleanup;
        status = build_known_range(known, &owned[i]);
        if (status != PE_STATUS_OK)
            goto cleanup;
        ranges[i] = owned[i];
    }

    StdDeck_CardMask_RESET(board);
    status = pe_equity_calculate(game_soko, ranges, num_players, board,
                                 dead_cards, opts, out_result);

cleanup:
    for (j = 0; j < num_players; ++j)
        pe_range_free(owned[j]);
    return status;
}
