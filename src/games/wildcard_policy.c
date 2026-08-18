/*
 * wildcard_policy.c - Generalized wildcard policy
 *
 * ISSUE-05 (#161): see wildcard_policy.h. The evaluator separates the wild
 * cards from the natural cards, then searches over all distinct substitutions
 * of the wild cards with standard (non-joker) deck cards, returning the
 * maximum HandVal reported by the configurable ranking evaluator
 * (pe_eval_configurable_hand).
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
#include <poker_eval/games/wildcard_policy.h>

/* These switches intentionally group several categories under one path. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

/* Global rank of a card index (0 = deck's lowest rank). Mirrors
 * pe_deck_min_rank() from generalized_deck.c. */
static int pe_wc_min_rank(const pe_deck_spec_t *spec) {
    int r;
    for (r = 0; r < 13; r++) {
        if (spec->active_rank_mask & (uint16_t)((uint16_t)1u << r)) {
            return r;
        }
    }
    return 0;
}

static int pe_wc_global_rank(const pe_deck_spec_t *spec, int card) {
    return pe_wc_min_rank(spec) + (card % spec->num_ranks);
}

static int pe_wc_suit(const pe_deck_spec_t *spec, int card) {
    return card / spec->num_ranks;
}

/* A joker card is any index past the (suit, rank) block. */
static int pe_wc_is_joker(const pe_deck_spec_t *spec, int card) {
    return card >= spec->num_suits * spec->num_ranks;
}

/* Build the ranking config for a policy. Five-of-a-Kind is inserted as the
 * strongest category when allowed; otherwise the standard order is used. */
static void pe_wc_build_config(const pe_wildcard_policy_t *policy,
                              pe_hand_ranking_config_t *cfg) {
    pe_ranking_config_init_default(cfg);
    if (policy->allow_five_of_a_kind) {
        cfg->category_order[cfg->num_active_categories] = PE_CAT_FIVE_OF_A_KIND;
        cfg->num_active_categories++;
    }
}

int pe_wildcard_policy_init(pe_wild_behavior_t behavior,
                           uint16_t wild_ranks,
                           int num_jokers,
                           pe_wildcard_policy_t *out_policy) {
    if (out_policy == NULL) {
        return -1;
    }
    if (behavior != PE_WILD_BEHAVIOR_FULLY_WILD &&
        behavior != PE_WILD_BEHAVIOR_BUG_RULE &&
        behavior != PE_WILD_BEHAVIOR_LOW_CARD) {
        return -1;
    }
    if (num_jokers < 0) {
        return -1;
    }
    memset(out_policy, 0, sizeof(*out_policy));
    out_policy->behavior = behavior;
    out_policy->wild_rank_mask = wild_ranks;
    out_policy->num_jokers = num_jokers;

    /* Defaults: Bug rule forbids 5-of-a-Kind; the other behaviors allow it
     * whenever any wild card is present. */
    if (behavior == PE_WILD_BEHAVIOR_BUG_RULE) {
        out_policy->allow_five_of_a_kind = 0;
    } else {
        out_policy->allow_five_of_a_kind =
            (wild_ranks != 0 || num_jokers > 0) ? 1 : 0;
    }
    return 0;
}

/* Is the natural (non-joker) card c wild under the effective rank mask?
 * The optional extra_rank (used by the LOW_CARD behavior) is OR-ed in. */
static int pe_wc_natural_is_wild(const pe_deck_spec_t *spec,
                                 const pe_wildcard_policy_t *policy,
                                 int c, int extra_rank) {
    int r = pe_wc_global_rank(spec, c);
    int s = pe_wc_suit(spec, c);
    uint16_t mask = (uint16_t)(policy->wild_rank_mask | (extra_rank >= 0
                                   ? (uint16_t)((uint16_t)1u << extra_rank) : 0u));

    if (mask & (uint16_t)((uint16_t)1u << r)) {
        return 1;
    }
    /* One-Eyed Jacks: Jacks (global rank 9) of the one-eyed suits (1, 2). */
    if (policy->is_one_eyed_jacks_wild && r == 9 && (s == 1 || s == 2)) {
        return 1;
    }
    /* Suicide King: King (global rank 11) of the suicide suit (3). */
    if (policy->is_suicide_king_wild && r == 11 && s == 3) {
        return 1;
    }
    return 0;
}

/* Advance a combination of k indices in [0, n). Returns 0 when exhausted. */
static int pe_wc_next_comb(int *idx, int k, int n) {
    int i = k - 1;
    if (k <= 0) {
        return 0;
    }
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

/*
 * Evaluate the hand. fixed_mask holds the natural (non-wild) cards. The w
 * wild cards are substituted by every distinct combination of w candidates
 * drawn from cand[] (npool entries), and the best HandVal wins.
 */
static HandVal pe_wc_search(const pe_deck_spec_t *spec,
                            const pe_hand_ranking_config_t *cfg,
                            pe_card_mask_t fixed_mask,
                            const int *cand, int npool,
                            int w, int num_cards) {
    HandVal best = HandVal_NOTHING;
    int idx[16];

    if (w <= 0) {
        return pe_eval_configurable_hand(spec, cfg, fixed_mask, num_cards);
    }
    if (w > 16 || npool < w) {
        return HandVal_NOTHING;
    }

    for (int i = 0; i < w; i++) {
        idx[i] = i;
    }

    do {
        pe_card_mask_t trial = fixed_mask;
        for (int i = 0; i < w; i++) {
            pe_deck_mask_set(spec, &trial, cand[idx[i]]);
        }
        HandVal v = pe_eval_configurable_hand(spec, cfg, trial, num_cards);
        if (v > best) {
            best = v;
        }
    } while (pe_wc_next_comb(idx, w, npool));

    return best;
}

HandVal pe_eval_wildcard_hand(const pe_deck_spec_t *deck_spec,
                              const pe_wildcard_policy_t *policy,
                              pe_card_mask_t hand_mask,
                              int num_cards) {
    pe_hand_ranking_config_t cfg;
    int n_nat, n_std_cards;
    int fixed[16], n_fixed = 0;
    int wild[16], n_wild = 0;
    int cand[64], npool = 0;
    pe_card_mask_t fixed_mask = 0;

    if (deck_spec == NULL || policy == NULL) {
        return HandVal_NOTHING;
    }
    if (num_cards <= 0 || num_cards > deck_spec->num_cards) {
        return HandVal_NOTHING;
    }
    n_nat = pe_deck_mask_count(deck_spec, hand_mask);
    if (n_nat != num_cards) {
        return HandVal_NOTHING;
    }

    pe_wc_build_config(policy, &cfg);

    n_std_cards = deck_spec->num_suits * deck_spec->num_ranks;

    /* For LOW_CARD behavior the lowest natural rank present becomes wild. */
    int extra_rank = -1;
    if (policy->behavior == PE_WILD_BEHAVIOR_LOW_CARD) {
        int min_r = 13;
        for (int c = 0; c < deck_spec->num_cards; c++) {
            if (!pe_deck_mask_is_set(deck_spec, hand_mask, c)) {
                continue;
            }
            if (pe_wc_is_joker(deck_spec, c)) {
                continue;
            }
            int r = pe_wc_global_rank(deck_spec, c);
            if (r < min_r) {
                min_r = r;
            }
        }
        if (min_r < 13) {
            extra_rank = min_r;
        }
    }

    /* Partition the hand into fixed natural cards and wild cards. */
    for (int c = 0; c < deck_spec->num_cards; c++) {
        if (!pe_deck_mask_is_set(deck_spec, hand_mask, c)) {
            continue;
        }
        if (pe_wc_is_joker(deck_spec, c)) {
            if (n_wild < 16) {
                wild[n_wild++] = c;
            }
            continue;
        }
        if (pe_wc_natural_is_wild(deck_spec, policy, c, extra_rank)) {
            if (n_wild < 16) {
                wild[n_wild++] = c;
            }
            continue;
        }
        if (n_fixed < 16) {
            fixed[n_fixed++] = c;
        }
        pe_deck_mask_set(deck_spec, &fixed_mask, c);
    }

    if (n_wild == 0) {
        return pe_eval_configurable_hand(deck_spec, &cfg, fixed_mask, num_cards);
    }

    /* Five-of-a-Kind is handled directly: a wild can complete a rank beyond the
     * four physical cards of that rank, which the distinct-card substitution
     * below cannot express. If any rank reaches five cards with the wilds, it
     * is by definition the strongest possible hand. */
    if (policy->allow_five_of_a_kind) {
        int counts[13];
        int five_rank = pe_ranking_config_category_rank(&cfg, PE_CAT_FIVE_OF_A_KIND);
        if (five_rank >= 0) {
            memset(counts, 0, sizeof(counts));
            for (int c = 0; c < deck_spec->num_cards; c++) {
                if (!pe_deck_mask_is_set(deck_spec, fixed_mask, c)) {
                    continue;
                }
                counts[pe_wc_global_rank(deck_spec, c)]++;
            }
            for (int r = 12; r >= 0; r--) {
                if (counts[r] + n_wild >= 5) {
                    return HandVal_HANDTYPE_VALUE((HandVal)five_rank) |
                           HandVal_TOP_CARD_VALUE((HandVal)(r & 0xF));
                }
            }
        }
    }

    /* Build the candidate pool of standard cards not already fixed. */
    for (int c = 0; c < n_std_cards; c++) {
        if (pe_deck_mask_is_set(deck_spec, fixed_mask, c)) {
            continue;
        }
        if (policy->behavior == PE_WILD_BEHAVIOR_BUG_RULE) {
            /* Bug: may act only as an Ace, or as a filler that completes a
             * Straight / Flush / Straight Flush. */
            int r = pe_wc_global_rank(deck_spec, c);
            if (r == 12) { /* Ace */
                cand[npool++] = c;
                continue;
            }
            pe_card_mask_t filler = fixed_mask;
            pe_deck_mask_set(deck_spec, &filler, c);
            HandVal v = pe_eval_configurable_hand(deck_spec, &cfg, filler,
                                                  num_cards);
            int cat = pe_ranking_category_rank(v);
            if (cat == pe_ranking_config_category_rank(&cfg, PE_CAT_STRAIGHT) ||
                cat == pe_ranking_config_category_rank(&cfg, PE_CAT_FLUSH) ||
                cat == pe_ranking_config_category_rank(&cfg, PE_CAT_STRAIGHT_FLUSH)) {
                cand[npool++] = c;
            }
        } else {
            cand[npool++] = c;
        }
    }

    if (npool < n_wild) {
        return HandVal_NOTHING;
    }

    return pe_wc_search(deck_spec, &cfg, fixed_mask, cand, npool,
                        n_wild, num_cards);
}
