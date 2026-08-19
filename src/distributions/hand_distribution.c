/*
 * hand_distribution.c - Automated hand distribution & combinatorial
 * probability engine. See hand_distribution.h for the description.
 *
 * ISSUE-02 (#158): exact combinatorial enumeration of every k-card hand of
 * a deck, classified via the configurable ranking rules (ISSUE-04 #160) on
 * top of the generalized deck abstraction (ISSUE-03 #159).
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
#include <math.h>
#include <poker_eval/distributions/hand_distribution.h>
#include <poker_eval/core/handval.h>

/* The game->deck/ranking mapping below intentionally groups many games under
 * a shared branch, so the exhaustive -Wswitch-enum check would be noise. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

/*
 * Advance a lexicographically-ordered k-combination of {0..n-1} held in idx
 * (each entry strictly increasing). Returns 1 while a next combination exists
 * and writes it into idx; returns 0 once the last combination is passed.
 */
static int pe_next_combination(int *idx, int n, int k) {
    int i = k - 1;
    while (i >= 0 && idx[i] == n - k + i) {
        i--;
    }
    if (i < 0) {
        return 0;
    }
    idx[i]++;
    for (int j = i + 1; j < k; j++) {
        idx[j] = idx[j - 1] + 1;
    }
    return 1;
}

int pe_compute_hand_distribution_ex(const pe_deck_spec_t *deck_spec,
                                    const pe_hand_ranking_config_t *config,
                                    int hand_size,
                                    pe_hand_distribution_t *out_dist) {
    int n, k, max_cat;
    int *idx;
    uint64_t counts[PE_HAND_DIST_MAX_CATEGORIES];
    uint64_t total = 0;

    if (deck_spec == NULL || config == NULL || out_dist == NULL) {
        return -1;
    }
    if (hand_size < 1 || hand_size > deck_spec->num_cards) {
        return -1;
    }

    n = deck_spec->num_cards;
    k = hand_size;
    max_cat = config->num_active_categories;
    if (max_cat > PE_HAND_DIST_MAX_CATEGORIES) {
        max_cat = PE_HAND_DIST_MAX_CATEGORIES;
    }
    if (max_cat <= 0) {
        return -1;
    }

    memset(out_dist, 0, sizeof(*out_dist));
    memset(counts, 0, sizeof(counts));
    out_dist->deck_size = n;
    out_dist->hand_size = k;
    snprintf(out_dist->deck_name, sizeof(out_dist->deck_name), "%s",
             deck_spec->deck_name);
    out_dist->ranking_name[0] = '\0';
    out_dist->game = game_NUMGAMES;

    idx = (int *)malloc((size_t)k * sizeof(int));
    if (idx == NULL) {
        return -1;
    }
    for (int i = 0; i < k; i++) {
        idx[i] = i;
    }

    do {
        pe_card_mask_t mask = 0;
        for (int i = 0; i < k; i++) {
            pe_deck_mask_set(deck_spec, &mask, idx[i]);
        }
        HandVal hv = pe_eval_configurable_hand(deck_spec, config, mask, k);
        if (hv != HandVal_NOTHING) {
            int rk = pe_ranking_category_rank(hv);
            if (rk >= 0 && rk < max_cat) {
                counts[rk]++;
            }
        }
        total++;
    } while (pe_next_combination(idx, n, k));

    free(idx);

    out_dist->total_combinations = total;
    out_dist->num_categories = max_cat;

    /* Categories stored strongest-first (rank max_cat-1 at index 0). The
     * configured rank index is NOT the same as the pe_hand_category_t value,
     * so we recover the semantic category via config->category_order[rank]. */
    for (int i = 0; i < max_cat; i++) {
        int rank = max_cat - 1 - i;
        pe_hand_category_t cat = config->category_order[rank];
        out_dist->categories[i].category_id = (int)cat;
        out_dist->categories[i].category_name = pe_ranking_category_name(cat);
        out_dist->categories[i].count = counts[rank];
        out_dist->categories[i].probability =
            total > 0 ? (double)counts[rank] / (double)total : 0.0;
    }

    double cum = 0.0;
    for (int i = 0; i < out_dist->num_categories; i++) {
        cum += out_dist->categories[i].probability;
        out_dist->categories[i].cumulative_probability = cum;
    }

    return 0;
}

int pe_compute_hand_distribution_for_preset(const char *deck_preset,
                                           const char *ranking_preset,
                                           int hand_size,
                                           pe_hand_distribution_t *out_dist) {
    pe_deck_spec_t spec;
    pe_hand_ranking_config_t cfg;

    if (deck_preset == NULL || ranking_preset == NULL || out_dist == NULL) {
        return -1;
    }
    if (pe_deck_get_predefined(deck_preset, &spec) != 0) {
        return -1;
    }
    if (pe_ranking_config_set_preset(ranking_preset, &cfg) != 0) {
        return -1;
    }
    if (pe_compute_hand_distribution_ex(&spec, &cfg, hand_size, out_dist) != 0) {
        return -1;
    }
    snprintf(out_dist->deck_name, sizeof(out_dist->deck_name), "%s",
             deck_preset ? deck_preset : "");
    snprintf(out_dist->ranking_name, sizeof(out_dist->ranking_name), "%s",
             ranking_preset ? ranking_preset : "");
    return 0;
}

int pe_compute_hand_distribution(enum_game_t game,
                                 pe_hand_distribution_t *out_dist) {
    const char *deck_preset = NULL;
    const char *ranking_preset = NULL;
    int hand_size = 5;

    switch (game) {
        case game_holdem:
        case game_holdem8:
        case game_omaha:
        case game_omaha5:
        case game_omaha6:
        case game_omaha8:
        case game_omaha85:
        case game_omaha86:
        case game_7stud:
        case game_7stud8:
        case game_7studnsq:
        case game_razz:
        case game_5draw:
        case game_5draw8:
        case game_5drawnsq:
        case game_lowball:
        case game_lowball27:
        case game_doubleflop_holdem:
        case game_drawmaha:
        case game_pineapple:
        case game_pineapple8:
        case game_27_triple_draw:
        case game_a5_triple_draw:
        case game_badacey:
        case game_badeucy:
        case game_badugi:
        case game_fusion:
        case game_courchevel:
        case game_courchevel8:
        case game_irish:
        case game_ofc:
            deck_preset = PE_DECK_PRESET_STD;
            ranking_preset = "standard";
            hand_size = 5;
            break;
        case game_sdholdem:
            deck_preset = PE_DECK_PRESET_SHORT;
            ranking_preset = "short_deck";
            hand_size = 5;
            break;
        case game_manila:
            deck_preset = PE_DECK_PRESET_SPANISH;
            ranking_preset = "italian_manila";
            hand_size = 5;
            break;
        default:
            return -1;
    }

    if (pe_compute_hand_distribution_for_preset(deck_preset, ranking_preset,
                                                hand_size, out_dist) != 0) {
        return -1;
    }
    out_dist->game = game;
    return 0;
}

int pe_hand_distribution_print_markdown(const pe_hand_distribution_t *dist,
                                        FILE *out) {
    int i;
    if (dist == NULL || out == NULL) {
        return -1;
    }
    fprintf(out, "## Hand Distribution");
    if (dist->deck_name[0] != '\0') {
        fprintf(out, " — %s", dist->deck_name);
    }
    if (dist->ranking_name[0] != '\0') {
        fprintf(out, " (%s ranking)", dist->ranking_name);
    }
    fprintf(out, "\n\n");
    fprintf(out, "| Category | Count | Probability | Cumulative |\n");
    fprintf(out, "|----------|------:|------------:|------------:|\n");
    for (i = 0; i < dist->num_categories; i++) {
        const pe_category_stat_t *c = &dist->categories[i];
        fprintf(out, "| %s | %llu | %.4f%% | %.4f%% |\n",
                c->category_name ? c->category_name : "?",
                (unsigned long long)c->count,
                c->probability * 100.0,
                c->cumulative_probability * 100.0);
    }
    fprintf(out, "| **Total** | **%llu** | **100.0000%%** | |\n",
            (unsigned long long)dist->total_combinations);
    return 0;
}

int pe_hand_distribution_print_json(const pe_hand_distribution_t *dist,
                                    FILE *out) {
    int i;
    if (dist == NULL || out == NULL) {
        return -1;
    }
    fprintf(out, "{\n");
    fprintf(out, "  \"deck_name\": \"%s\",\n", dist->deck_name);
    fprintf(out, "  \"deck_size\": %d,\n", dist->deck_size);
    fprintf(out, "  \"hand_size\": %d,\n", dist->hand_size);
    fprintf(out, "  \"ranking_name\": \"%s\",\n", dist->ranking_name);
    fprintf(out, "  \"total_combinations\": %llu,\n",
            (unsigned long long)dist->total_combinations);
    fprintf(out, "  \"categories\": [\n");
    for (i = 0; i < dist->num_categories; i++) {
        const pe_category_stat_t *c = &dist->categories[i];
        fprintf(out,
                "    {\"category\": \"%s\", \"category_id\": %d, "
                "\"count\": %llu, \"probability\": %.10f, "
                "\"cumulative_probability\": %.10f}%s\n",
                c->category_name ? c->category_name : "?",
                c->category_id,
                (unsigned long long)c->count,
                c->probability,
                c->cumulative_probability,
                i + 1 < dist->num_categories ? "," : "");
    }
    fprintf(out, "  ]\n");
    fprintf(out, "}\n");
    return 0;
}
