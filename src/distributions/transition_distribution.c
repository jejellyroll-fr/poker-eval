/*
 * transition_distribution.c - Board transition & turn/river hand improvement
 * probability matrix. See transition_distribution.h for the description.
 *
 * ISSUE-06 (#162): exact combinatorial enumeration of the turn (47
 * evaluations from a flop) and river (C(47, 2) = 1081 evaluations) streets for
 * a fixed Hero hand and current board, classifying every resulting best-5 high
 * hand into its standard category and accumulating improvement statistics and a
 * turn -> river category transition matrix.
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of the
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
#include <poker_eval/distributions/transition_distribution.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/handval.h>

const char *pe_transition_category_name(int category) {
    switch (category) {
        case StdRules_HandType_NOPAIR:    return "High Card";
        case StdRules_HandType_ONEPAIR:   return "Pair";
        case StdRules_HandType_TWOPAIR:   return "Two Pair";
        case StdRules_HandType_TRIPS:     return "Trips";
        case StdRules_HandType_STRAIGHT:  return "Straight";
        case StdRules_HandType_FLUSH:     return "Flush";
        case StdRules_HandType_FULLHOUSE: return "Full House";
        case StdRules_HandType_QUADS:     return "Quads";
        case StdRules_HandType_STFLUSH:   return "Straight Flush";
        default:                          return NULL;
    }
}

/* Number of cards set in a standard 52-card mask. */
static int pe_mask_card_count(StdDeck_CardMask mask) {
    int n = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            n++;
        }
    }
    return n;
}

/* A flush (or straight flush) draw is considered "completed" by FLUSH/STFLUSH;
 * a straight draw by STRAIGHT/STFLUSH. The straight flush naturally lives in
 * both families, which is the standard "outs" accounting. */
static int pe_is_flush_family(int cat) {
    return cat == StdRules_HandType_FLUSH || cat == StdRules_HandType_STFLUSH;
}
static int pe_is_straight_family(int cat) {
    return cat == StdRules_HandType_STRAIGHT || cat == StdRules_HandType_STFLUSH;
}

int pe_compute_transition_distribution(enum_game_t game,
                                       StdDeck_CardMask hero_hand,
                                       StdDeck_CardMask current_board,
                                       StdDeck_CardMask dead_cards,
                                       pe_transition_result_t *out_result) {
    (void)game;

    int hero_count, board_count, remaining;
    int used[StdDeck_N_CARDS];
    int rem[StdDeck_N_CARDS];
    int n_rem = 0;
    StdDeck_CardMask start_mask;
    StdDeck_CardMask rem_mask;
    HandVal start_val;
    int start_cat;
    double turn_flush = 0.0, turn_straight = 0.0;
    double turn_pair = 0.0, turn_set = 0.0;
    double river_flush = 0.0, river_straight = 0.0;
    double river_pair = 0.0, river_fullhouse = 0.0;
    double no_improvement = 0.0;
    uint64_t river_total = 0;

    if (out_result == NULL) {
        return -1;
    }
    memset(out_result, 0, sizeof(*out_result));

    hero_count  = pe_mask_card_count(hero_hand);
    board_count = pe_mask_card_count(current_board);
    if (hero_count == 0) {
        return -1;
    }

    /* Build the mask of all unavailable cards (hero + board + dead). */
    StdDeck_CardMask_RESET(start_mask);
    StdDeck_CardMask_OR(start_mask, start_mask, hero_hand);
    StdDeck_CardMask_OR(start_mask, start_mask, current_board);

    StdDeck_CardMask_RESET(rem_mask);
    StdDeck_CardMask_OR(rem_mask, rem_mask, start_mask);
    StdDeck_CardMask_OR(rem_mask, rem_mask, dead_cards);

    /* Enumerate the remaining deck. */
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        used[i] = StdDeck_CardMask_CARD_IS_SET(rem_mask, i) ? 1 : 0;
        if (!used[i]) {
            rem[n_rem++] = i;
        }
    }
    remaining = n_rem;
    out_result->num_remaining = remaining;

    /* Starting (current board) category. */
    start_val = StdDeck_StdRules_EVAL_N(start_mask,
                                        hero_count + board_count);
    start_cat = (int)HandVal_HANDTYPE(start_val);
    out_result->start_category = start_cat;

    if (remaining < 1) {
        return -1;
    }

    /* Turn street: one card from the remaining deck. */
    for (int a = 0; a < remaining; a++) {
        StdDeck_CardMask turn_mask;
        HandVal hv;
        int cat;
        StdDeck_CardMask_RESET(turn_mask);
        StdDeck_CardMask_OR(turn_mask, turn_mask, start_mask);
        StdDeck_CardMask_SET(turn_mask, rem[a]);
        hv = StdDeck_StdRules_EVAL_N(turn_mask, hero_count + board_count + 1);
        cat = (int)HandVal_HANDTYPE(hv);

        if (pe_is_flush_family(cat))    turn_flush += 1.0;
        if (pe_is_straight_family(cat))  turn_straight += 1.0;
        if (cat == StdRules_HandType_ONEPAIR)  turn_pair += 1.0;
        if (cat == StdRules_HandType_TRIPS)    turn_set += 1.0;

        /* River street: a second card from the remaining deck. */
        for (int b = a + 1; b < remaining; b++) {
            StdDeck_CardMask river_mask;
            HandVal rhv;
            int rcat;
            StdDeck_CardMask_RESET(river_mask);
            StdDeck_CardMask_OR(river_mask, river_mask, turn_mask);
            StdDeck_CardMask_SET(river_mask, rem[b]);
            rhv = StdDeck_StdRules_EVAL_N(river_mask,
                                          hero_count + board_count + 2);
            rcat = (int)HandVal_HANDTYPE(rhv);

            river_total++;
            if (pe_is_flush_family(rcat))    river_flush += 1.0;
            if (pe_is_straight_family(rcat)) river_straight += 1.0;
            if (rcat == StdRules_HandType_ONEPAIR)   river_pair += 1.0;
            if (rcat == StdRules_HandType_FULLHOUSE) river_fullhouse += 1.0;
            if (rcat <= start_cat)            no_improvement += 1.0;

            if (cat >= 0 && cat < PE_TRANSITION_MAX_CATEGORY &&
                rcat >= 0 && rcat < PE_TRANSITION_MAX_CATEGORY) {
                out_result->transition_matrix[cat][rcat] += 1.0;
            }
        }
    }

    out_result->stats.prob_turn_flush     = turn_flush / (double)remaining;
    out_result->stats.prob_turn_straight  = turn_straight / (double)remaining;
    out_result->stats.prob_turn_pair      = turn_pair / (double)remaining;
    out_result->stats.prob_turn_set       = turn_set / (double)remaining;

    if (river_total > 0) {
        double denom = (double)river_total;
        out_result->stats.prob_river_flush     = river_flush / denom;
        out_result->stats.prob_river_straight  = river_straight / denom;
        out_result->stats.prob_river_pair      = river_pair / denom;
        out_result->stats.prob_river_fullhouse = river_fullhouse / denom;
        out_result->stats.prob_no_improvement  = no_improvement / denom;
        for (int i = 0; i < PE_TRANSITION_MAX_CATEGORY; i++) {
            double row_sum = 0.0;
            for (int j = 0; j < PE_TRANSITION_MAX_CATEGORY; j++) {
                row_sum += out_result->transition_matrix[i][j];
            }
            if (row_sum > 0.0) {
                for (int j = 0; j < PE_TRANSITION_MAX_CATEGORY; j++) {
                    out_result->transition_matrix[i][j] /= row_sum;
                }
            }
        }
    }

    /* With only one card to come (e.g. a turn board) the river cannot add a
     * second street, so the river stats and the transition matrix are left
     * empty. */

    return 0;
}
