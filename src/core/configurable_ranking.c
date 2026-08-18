/*
 * configurable_ranking.c - Configurable hand ranking rules
 *
 * ISSUE-04 (#160): data-driven category ordering and 4-card category
 * detectors for poker variants (Short Deck, Manila / Italian, Canadian
 * Stud, New York Stud, Joker games). See configurable_ranking.h.
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
#include <poker_eval/core/configurable_ranking.h>

/* These switches intentionally group several categories under a shared code
 * path, so the exhaustive -Wswitch-enum check would be noise here. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

/* Forward declaration; defined further below. */
static int pe_category_rank(const pe_hand_ranking_config_t *cfg,
                            pe_hand_category_t cat);

/* Lowest set bit of a deck's active_rank_mask, interpreted as a global rank
 * index (0 = rank "2"). Mirrors pe_deck_min_rank() from generalized_deck.c. */
static int pe_min_rank(const pe_deck_spec_t *spec) {
    int r;
    for (r = 0; r < 13; r++) {
        if (spec->active_rank_mask & (uint16_t)((uint16_t)1u << r)) {
            return r;
        }
    }
    return 0;
}

static int pe_global_rank(const pe_deck_spec_t *spec, int card) {
    return pe_min_rank(spec) + (card % spec->num_ranks);
}

static int pe_suit_of(const pe_deck_spec_t *spec, int card) {
    return card / spec->num_ranks;
}

/* Pack at most 5 global ranks (most significant card first) into the low
 * 20 bits of a HandVal, reusing the standard TOP..FIFTH card slots. */
static HandVal pe_pack_ranks(const int *ranks_desc, int n) {
    HandVal v = 0;
    int shift = HandVal_TOP_CARD_SHIFT;
    int i;
    for (i = 0; i < n && i < 5; i++) {
        v |= ((HandVal)(ranks_desc[i] & 0xF)) << shift;
        shift -= 4;
    }
    return v;
}

/*
 * Canonical tiebreak ordering for the standard (non-straight, non-flush)
 * categories: groups are emitted by descending multiplicity (quads, trips,
 * pairs, then singletons), and within a group by descending rank. This is the
 * identical convention used by the fast hardcoded evaluators, so e.g. a set
 * of deuces full of aces (222AA) compares as [2,2,2,A,A], correctly losing to
 * 333KK.
 */
static void pe_build_canonical(const int *counts, int num_cards, int *out) {
    int count, rank, k = 0;
    for (count = 4; count >= 1; count--) {
        for (rank = 12; rank >= 0; rank--) {
            int t = counts[rank];
            if (t == count) {
                int j;
                for (j = 0; j < count && k < num_cards && k < 5; j++) {
                    out[k++] = rank;
                }
            }
        }
    }
}

/* Fill category_order with `n` categories and set num_active_categories. */
static void pe_fill_order(pe_hand_ranking_config_t *cfg,
                          const pe_hand_category_t *cats, int n) {
    int i;
    cfg->num_active_categories = n;
    for (i = 0; i < n && i < 16; i++) {
        cfg->category_order[i] = cats[i];
    }
}

int pe_ranking_config_init_default(pe_hand_ranking_config_t *config) {
    static const pe_hand_category_t std[] = {
        PE_CAT_HIGH_CARD, PE_CAT_ONE_PAIR, PE_CAT_TWO_PAIR,
        PE_CAT_THREE_OF_A_KIND, PE_CAT_STRAIGHT, PE_CAT_FLUSH,
        PE_CAT_FULL_HOUSE, PE_CAT_FOUR_OF_A_KIND, PE_CAT_STRAIGHT_FLUSH
    };
    if (config == NULL) {
        return -1;
    }
    memset(config, 0, sizeof(*config));
    pe_fill_order(config, std, (int)(sizeof(std) / sizeof(std[0])));
    config->allow_4card_straights = 0;
    config->allow_4card_flushes = 0;
    config->flush_beats_fullhouse = 0;
    config->ace_low_straight_allowed = 1;
    return 0;
}

int pe_ranking_config_set_preset(const char *preset_name,
                                 pe_hand_ranking_config_t *config) {
    static const pe_hand_category_t standard[] = {
        PE_CAT_HIGH_CARD, PE_CAT_ONE_PAIR, PE_CAT_TWO_PAIR,
        PE_CAT_THREE_OF_A_KIND, PE_CAT_STRAIGHT, PE_CAT_FLUSH,
        PE_CAT_FULL_HOUSE, PE_CAT_FOUR_OF_A_KIND, PE_CAT_STRAIGHT_FLUSH
    };
    /* Flush above Full House (Short Deck / Six Plus, Manila, Italian). */
    static const pe_hand_category_t flush_over_fh[] = {
        PE_CAT_HIGH_CARD, PE_CAT_ONE_PAIR, PE_CAT_TWO_PAIR,
        PE_CAT_THREE_OF_A_KIND, PE_CAT_STRAIGHT, PE_CAT_FULL_HOUSE,
        PE_CAT_FLUSH, PE_CAT_FOUR_OF_A_KIND, PE_CAT_STRAIGHT_FLUSH
    };
    /* Canadian Stud: 4-card categories sit between Two Pair and Straight. */
    static const pe_hand_category_t canadian[] = {
        PE_CAT_HIGH_CARD, PE_CAT_ONE_PAIR, PE_CAT_TWO_PAIR,
        PE_CAT_THREE_OF_A_KIND, PE_CAT_FOUR_CARD_STRAIGHT,
        PE_CAT_FOUR_CARD_FLUSH, PE_CAT_STRAIGHT, PE_CAT_FLUSH,
        PE_CAT_FULL_HOUSE, PE_CAT_FOUR_OF_A_KIND, PE_CAT_STRAIGHT_FLUSH
    };
    /* New York Stud: 4-card flush is a strong intermediate (above Straight). */
    static const pe_hand_category_t new_york[] = {
        PE_CAT_HIGH_CARD, PE_CAT_ONE_PAIR, PE_CAT_TWO_PAIR,
        PE_CAT_THREE_OF_A_KIND, PE_CAT_FOUR_CARD_STRAIGHT, PE_CAT_STRAIGHT,
        PE_CAT_FOUR_CARD_FLUSH, PE_CAT_FLUSH, PE_CAT_FULL_HOUSE,
        PE_CAT_FOUR_OF_A_KIND, PE_CAT_STRAIGHT_FLUSH
    };

    if (config == NULL) {
        return -1;
    }
    memset(config, 0, sizeof(*config));

    if (preset_name == NULL) {
        return -1;
    } else if (strcmp(preset_name, "standard") == 0) {
        pe_fill_order(config, standard, (int)(sizeof(standard) / sizeof(standard[0])));
        config->flush_beats_fullhouse = 0;
        config->ace_low_straight_allowed = 1;
    } else if (strcmp(preset_name, "short_deck") == 0) {
        pe_fill_order(config, flush_over_fh, (int)(sizeof(flush_over_fh) / sizeof(flush_over_fh[0])));
        config->flush_beats_fullhouse = 1;
        config->ace_low_straight_allowed = 0;
    } else if (strcmp(preset_name, "italian_manila") == 0) {
        pe_fill_order(config, flush_over_fh, (int)(sizeof(flush_over_fh) / sizeof(flush_over_fh[0])));
        config->flush_beats_fullhouse = 1;
        config->ace_low_straight_allowed = 0;
    } else if (strcmp(preset_name, "canadian_stud") == 0) {
        pe_fill_order(config, canadian, (int)(sizeof(canadian) / sizeof(canadian[0])));
        config->allow_4card_straights = 1;
        config->allow_4card_flushes = 1;
        config->flush_beats_fullhouse = 0;
        config->ace_low_straight_allowed = 1;
    } else if (strcmp(preset_name, "new_york_stud") == 0) {
        pe_fill_order(config, new_york, (int)(sizeof(new_york) / sizeof(new_york[0])));
        config->allow_4card_straights = 1;
        config->allow_4card_flushes = 1;
        config->flush_beats_fullhouse = 0;
        config->ace_low_straight_allowed = 1;
    } else {
        return -1;
    }
    return 0;
}

int pe_ranking_category_rank(HandVal handval) {
    return (int)HandVal_HANDTYPE(handval);
}

int pe_ranking_config_category_rank(const pe_hand_ranking_config_t *config,
                                     pe_hand_category_t category) {
    if (config == NULL) {
        return -1;
    }
    return pe_category_rank(config, category);
}

pe_hand_category_t pe_ranking_category_from_rank(int rank) {
    if (rank < 0 || rank >= (int)PE_CAT_COUNT) {
        return (pe_hand_category_t)(-1);
    }
    return (pe_hand_category_t)rank;
}

const char *pe_ranking_category_name(pe_hand_category_t category) {
    switch (category) {
        case PE_CAT_HIGH_CARD:        return "HighCard";
        case PE_CAT_ONE_PAIR:         return "OnePair";
        case PE_CAT_TWO_PAIR:         return "TwoPair";
        case PE_CAT_THREE_OF_A_KIND:  return "ThreeOfAKind";
        case PE_CAT_FOUR_CARD_STRAIGHT: return "FourCardStraight";
        case PE_CAT_FOUR_CARD_FLUSH:  return "FourCardFlush";
        case PE_CAT_STRAIGHT:         return "Straight";
        case PE_CAT_FLUSH:            return "Flush";
        case PE_CAT_FULL_HOUSE:       return "FullHouse";
        case PE_CAT_FOUR_OF_A_KIND:   return "FourOfAKind";
        case PE_CAT_STRAIGHT_FLUSH:   return "StraightFlush";
        case PE_CAT_FIVE_OF_A_KIND:   return "FiveOfAKind";
        default:                      return "Unknown";
    }
}

/* Find the configured rank index of a category, or -1 if not active. */
static int pe_category_rank(const pe_hand_ranking_config_t *cfg,
                            pe_hand_category_t cat) {
    int i;
    for (i = 0; i < cfg->num_active_categories; i++) {
        if (cfg->category_order[i] == cat) {
            return i;
        }
    }
    return -1;
}

static inline int pe_pop_lowest_card(pe_card_mask_t *m) {
#if defined(__GNUC__) || defined(__clang__)
    int bit = __builtin_ctzll(*m);
#elif defined(_MSC_VER) && defined(_WIN64)
    unsigned long bit;
    _BitScanForward64(&bit, *m);
#else
    int bit = 0;
    uint64_t temp = *m;
    while ((temp & 1) == 0) { temp >>= 1; bit++; }
#endif
    *m &= *m - 1;
    return bit;
}

/*
 * Evaluate the hand described by (spec, hand_mask, num_cards) against cfg.
 * Returns a normalized HandVal with HANDTYPE == configured rank of the best
 * matching category.
 */
HandVal pe_eval_configurable_hand(const pe_deck_spec_t *deck_spec,
                                  const pe_hand_ranking_config_t *config,
                                  pe_card_mask_t hand_mask,
                                  int num_cards) {
    int counts[13];
    int present[13];
    int suit_count[4];
    int flush_ranks[5];
    int ranks_sorted[7];
    int n = 0;            /* number of cards collected */
    int max_suit = 0, max_suit_count = 0;
    int straight5_high = -1;
    int straight4_high = -1;
    int quads_rank = -1, trips_rank = -1;
    int num_pairs = 0;
    int best_rank = -1;
    pe_hand_category_t best_cat = (pe_hand_category_t)(-1);
    int i, c, r, s;
    pe_card_mask_t m;

    if (deck_spec == NULL || config == NULL || num_cards <= 0) {
        return HandVal_NOTHING;
    }
    if (num_cards > deck_spec->num_cards) {
        return HandVal_NOTHING;
    }

    memset(counts, 0, sizeof(counts));
    memset(present, 0, sizeof(present));
    memset(suit_count, 0, sizeof(suit_count));

    m = hand_mask;
    while (m != 0) {
        c = pe_pop_lowest_card(&m);
        if (c >= deck_spec->num_cards) {
            return HandVal_NOTHING;
        }
        r = pe_global_rank(deck_spec, c);
        s = pe_suit_of(deck_spec, c);
        if (r < 0 || r > 12 || s < 0 || s >= deck_spec->num_suits) {
            return HandVal_NOTHING;
        }
        counts[r]++;
        present[r] = 1;
        suit_count[s]++;
        n++;
    }
    if (n == 0) {
        return HandVal_NOTHING;
    }

    for (s = 0; s < deck_spec->num_suits; s++) {
        if (suit_count[s] > max_suit_count) {
            max_suit_count = suit_count[s];
            max_suit = s;
        }
    }

    /* Copy the ranks of the dominant suit (for flush signatures). */
    {
        int fc = 0;
        m = hand_mask;
        while (m != 0 && fc < 5) {
            c = pe_pop_lowest_card(&m);
            if (pe_suit_of(deck_spec, c) == max_suit) {
                flush_ranks[fc++] = pe_global_rank(deck_spec, c);
            }
        }
        /* sort flush_ranks descending (insertion, small n) */
        for (i = 1; i < fc; i++) {
            int key = flush_ranks[i], j = i - 1;
            while (j >= 0 && flush_ranks[j] < key) {
                flush_ranks[j + 1] = flush_ranks[j];
                j--;
            }
            flush_ranks[j + 1] = key;
        }
    }

    /* Canonical tiebreak ordering (groups by count, then rank descending). */
    pe_build_canonical(counts, n, ranks_sorted);

    /* Detect 5-card straight (and wheel). */
    {
        int ok = 1;
        for (r = 4; r <= 12; r++) {
            int k;
            ok = 1;
            for (k = 0; k < 5; k++) {
                if (!present[r - k]) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                straight5_high = r;
                break;
            }
        }
        if (!ok && config->ace_low_straight_allowed &&
            present[0] && present[1] && present[2] && present[3] && present[12]) {
            straight5_high = 3; /* wheel plays Ace as the lowest card, high = 5 */
        }
    }

    /* Detect 4-card straight (and 4-card wheel) when allowed. */
    if (config->allow_4card_straights && num_cards >= 4 && straight5_high < 0) {
        int ok = 1;
        for (r = 3; r <= 12; r++) {
            int k;
            ok = 1;
            for (k = 0; k < 4; k++) {
                if (!present[r - k]) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                straight4_high = r;
                break;
            }
        }
        if (!ok && config->ace_low_straight_allowed &&
            present[0] && present[1] && present[2] && present[12]) {
            straight4_high = 2; /* A-2-3-4 wheel, high = 4 */
        }
    }

    /* Classify rank multiplicities. */
    for (r = 12; r >= 0; r--) {
        if (counts[r] >= 5 && quads_rank < 0) {
            quads_rank = r; /* treat 5+ as the top five-of-a-kind rank */
        } else if (counts[r] == 4 && quads_rank < 0) {
            quads_rank = r;
        } else if (counts[r] == 3 && trips_rank < 0) {
            trips_rank = r;
        } else if (counts[r] == 2) {
            num_pairs++;
        }
    }

    /* Pick the highest-ranked qualifying category. */
#define TRY_CAT(cat, sig) do {                                            \
        int _rk = pe_category_rank(config, (cat));                         \
        if (_rk >= 0 && _rk > best_rank) { best_rank = _rk; best_cat = (cat); (void)(sig); } \
    } while (0)

    if (quads_rank >= 0 && counts[quads_rank] >= 5) {
        TRY_CAT(PE_CAT_FIVE_OF_A_KIND, 0);
    }
    if (straight5_high >= 0 && max_suit_count >= 5) {
        TRY_CAT(PE_CAT_STRAIGHT_FLUSH, 0);
    }
    if (quads_rank >= 0 && counts[quads_rank] == 4) {
        TRY_CAT(PE_CAT_FOUR_OF_A_KIND, 0);
    }
    if (trips_rank >= 0 && num_pairs >= 1) {
        TRY_CAT(PE_CAT_FULL_HOUSE, 0);
    }
    if (max_suit_count >= 5 && num_cards >= 5) {
        TRY_CAT(PE_CAT_FLUSH, 0);
    }
    if (straight5_high >= 0) {
        TRY_CAT(PE_CAT_STRAIGHT, 0);
    }
    if (config->allow_4card_flushes && max_suit_count == 4 &&
        num_cards >= 4 && num_cards <= 5) {
        TRY_CAT(PE_CAT_FOUR_CARD_FLUSH, 0);
    }
    if (config->allow_4card_straights && straight4_high >= 0) {
        TRY_CAT(PE_CAT_FOUR_CARD_STRAIGHT, 0);
    }
    if (trips_rank >= 0) {
        TRY_CAT(PE_CAT_THREE_OF_A_KIND, 0);
    }
    if (num_pairs >= 2) {
        TRY_CAT(PE_CAT_TWO_PAIR, 0);
    }
    if (num_pairs >= 1) {
        TRY_CAT(PE_CAT_ONE_PAIR, 0);
    }
    TRY_CAT(PE_CAT_HIGH_CARD, 0);

#undef TRY_CAT

    if (best_rank < 0) {
        return HandVal_NOTHING;
    }

    /* Build the normalized HandVal: HANDTYPE = configured rank, kicker in low bits. */
    {
        HandVal hv = HandVal_HANDTYPE_VALUE((HandVal)best_rank);
        switch (best_cat) {
            case PE_CAT_STRAIGHT:
            case PE_CAT_STRAIGHT_FLUSH:
                hv |= HandVal_TOP_CARD_VALUE((HandVal)(straight5_high & 0xF));
                break;
            case PE_CAT_FOUR_CARD_STRAIGHT:
                hv |= HandVal_TOP_CARD_VALUE((HandVal)(straight4_high & 0xF));
                break;
            case PE_CAT_FLUSH:
                hv |= pe_pack_ranks(flush_ranks,
                                    max_suit_count > 5 ? 5 : max_suit_count);
                break;
            case PE_CAT_FOUR_CARD_FLUSH: {
                int kicker = -1;
                /* find the off-suit kicker for a 5-card hand */
                m = hand_mask;
                while (m != 0) {
                    c = pe_pop_lowest_card(&m);
                    if (pe_suit_of(deck_spec, c) != max_suit) {
                        kicker = pe_global_rank(deck_spec, c);
                        break;
                    }
                }
                hv |= pe_pack_ranks(flush_ranks, max_suit_count);
                if (kicker >= 0 && num_cards == 5) {
                    hv |= HandVal_FIFTH_CARD_VALUE((HandVal)(kicker & 0xF));
                }
                break;
            }
            default:
                /* Group-based ordering (pairs/trips/quads/full house/high card). */
                hv |= pe_pack_ranks(ranks_sorted, n);
                break;
        }
        return hv;
    }
}
