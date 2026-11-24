/*
 * soko.c - Sökö (Canadian Stud) showdown description
 *
 * See soko.h.  The classification itself lives in the configurable ranking
 * engine, so this file only binds it to the Sökö preset and converts the deck
 * rank indexes (0 = deuce) to display ranks (2..14).  It cannot drift from
 * the evaluator the equity code uses: both call the same preset.
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
#include <string.h>

#include <poker_eval/games/soko.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/deck/generalized_deck.h>

pe_status_t pe_soko_describe_hand(StdDeck_CardMask hand,
                                  pe_soko_hand_description_t *out) {
    pe_deck_spec_t spec;
    pe_hand_ranking_config_t config;
    pe_hand_description_t description;
    HandVal value;
    int i;

    if (out == NULL)
        return PE_STATUS_INVALID_ARG;
    if (StdDeck_numCards(hand) != 5)
        return PE_STATUS_INVALID_ARG;

    if (pe_deck_get_predefined(PE_DECK_PRESET_STD, &spec) != 0)
        return PE_STATUS_ERROR;
    if (pe_ranking_config_set_preset("soko", &config) != 0)
        return PE_STATUS_ERROR;

    memset(&description, 0, sizeof(description));
    if (!pe_eval_configurable_hand_detailed(
            &spec, &config, (pe_card_mask_t)cardmask_to_mask_t(hand), 5,
            &description))
        return PE_STATUS_INVALID_ARG;

    value = pe_eval_configurable_hand(
        &spec, &config, (pe_card_mask_t)cardmask_to_mask_t(hand), 5);
    if (value == HandVal_NOTHING)
        return PE_STATUS_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    out->category = description.category;
    out->category_name = pe_ranking_category_name(description.category);
    out->category_rank = description.category_rank;
    out->value = (unsigned int)value;
    out->num_tiebreak_ranks = description.num_tiebreak_ranks;
    for (i = 0; i < description.num_tiebreak_ranks && i < 5; ++i)
        out->tiebreak_ranks[i] = description.tiebreak_ranks[i] + 2;
    return PE_STATUS_OK;
}
