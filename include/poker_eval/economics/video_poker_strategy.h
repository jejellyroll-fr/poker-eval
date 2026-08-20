/*
 * video_poker_strategy.h - Optimal-strategy derivation for video poker.
 *
 * ISSUE-07 step 3 (#163 / #214): derive the optimal-hold outcome frequencies
 * of a video poker paytable with the draw optimizer instead of asserting the
 * published (Wizard of Odds) strategy table. This is what lets the library
 * validate paytables that are not one of the three hardcoded games (e.g.
 * 8/5 Jacks or Better) and answer "what is the optimal hold for this hand
 * under this paytable".
 *
 * Counting convention: combinations follow the Wizard of Odds tables, where
 * every deal contributes the same total weight. The per-deal weight is the
 * least common multiple of the draw counts C(deck-5, 0..5): 7,669,695 for a
 * 52-card deck (5 x C(47,5)) and 8,561,520 for the 53-card joker deck
 * (5 x C(48,5)). A completion of a k-card draw therefore counts
 * weight(k) = per-deal-weight / C(deck-5, k), which is always an integer
 * (7,669,695, 163,185, 7,095, 473, 43, 5 and 8,561,520, 178,365, 7,590,
 * 495, 44, 5 for k = 0..5). The total number of combinations is therefore
 * C(52,5) x 7,669,695 = 19,933,230,517,200 for 52-card games and
 * C(53,5) x 8,561,520 = 24,568,865,521,200 for joker poker, matching the
 * published tables exactly, and the probability of a category is its
 * combinations divided by the total.
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 */

#ifndef POKER_EVAL_VIDEO_POKER_STRATEGY_H
#define POKER_EVAL_VIDEO_POKER_STRATEGY_H

#include <poker_eval/economics/paytable_ev.h>
#include <poker_eval/deck/deck_std.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Video poker variants whose optimal strategy can be derived. The category
 * row order of each variant is the canonical order of the published (Wizard
 * of Odds) return tables; the derivation maps payouts onto this order and
 * returns outcome counts in it. */
typedef enum {
    PE_VP_JACKS_OR_BETTER = 0, /* 52-card deck, no wilds, 10 categories      */
    PE_VP_DEUCES_WILD,         /* 52-card deck, deuces wild, 11 categories   */
    PE_VP_JOKER_POKER,         /* 53-card deck (one joker), 12 categories    */
    PE_VP_VARIANT_COUNT
} pe_video_poker_variant_t;

/* Number of outcome categories for a variant (also the number of payouts the
 * derivation expects). Returns 0 for an invalid variant. */
POKEREVAL_EXPORT int pe_video_poker_num_categories(pe_video_poker_variant_t variant);

/* Canonical category indices for PE_VP_JACKS_OR_BETTER (payout order). */
enum {
    PE_VP_JOB_ROYAL_FLUSH = 0,     /* 800 */
    PE_VP_JOB_STRAIGHT_FLUSH,      /*  50 */
    PE_VP_JOB_FOUR_OF_A_KIND,      /*  25 */
    PE_VP_JOB_FULL_HOUSE,          /*   9 */
    PE_VP_JOB_FLUSH,               /*   6 */
    PE_VP_JOB_STRAIGHT,            /*   4 */
    PE_VP_JOB_THREE_OF_A_KIND,     /*   3 */
    PE_VP_JOB_TWO_PAIR,            /*   2 */
    PE_VP_JOB_JACKS_OR_BETTER,     /*   1 */
    PE_VP_JOB_NOTHING              /*   0 */
};

/* Canonical category indices for PE_VP_DEUCES_WILD (payout order). */
enum {
    PE_VP_DW_NATURAL_ROYAL_FLUSH = 0, /* 800 */
    PE_VP_DW_FOUR_DEUCES,             /* 200 */
    PE_VP_DW_WILD_ROYAL_FLUSH,        /*  25 */
    PE_VP_DW_FIVE_OF_A_KIND,          /*  15 */
    PE_VP_DW_STRAIGHT_FLUSH,          /*   9 */
    PE_VP_DW_FOUR_OF_A_KIND,          /*   5 */
    PE_VP_DW_FULL_HOUSE,              /*   3 */
    PE_VP_DW_FLUSH,                   /*   2 */
    PE_VP_DW_STRAIGHT,                /*   2 */
    PE_VP_DW_THREE_OF_A_KIND,         /*   1 */
    PE_VP_DW_NOTHING                  /*   0 */
};

/* Canonical category indices for PE_VP_JOKER_POKER (payout order). */
enum {
    PE_VP_JP_NATURAL_ROYAL_FLUSH = 0, /* 800 */
    PE_VP_JP_FIVE_OF_A_KIND,          /* 200 */
    PE_VP_JP_WILD_ROYAL_FLUSH,        /* 100 */
    PE_VP_JP_STRAIGHT_FLUSH,          /*  50 */
    PE_VP_JP_FOUR_OF_A_KIND,          /*  20 */
    PE_VP_JP_FULL_HOUSE,              /*   7 */
    PE_VP_JP_FLUSH,                   /*   5 */
    PE_VP_JP_STRAIGHT,                /*   3 */
    PE_VP_JP_THREE_OF_A_KIND,         /*   2 */
    PE_VP_JP_TWO_PAIR,                /*   1 */
    PE_VP_JP_KINGS_OR_BETTER,         /*   1 */
    PE_VP_JP_NOTHING                  /*   0 */
};

/* The joker of PE_VP_JOKER_POKER is card index 52 (JokerDeck_JOKER), outside
 * the 52-card StdDeck range. In the card mask it occupies the dedicated joker
 * field (bit 61 of the 64-bit layout), not raw bit 52, which is inside the
 * hearts field: the StdDeck mask packs spades/clubs/diamonds/hearts as
 * 13-bit fields with 3 bits of padding between them. */
#define PE_VP_JOKER_CARD 52

/* Classify a 5-card hand into the variant's category index (0 .. n-1 in the
 * canonical row order above). For PE_VP_DEUCES_WILD the deuces (rank 2 of
 * every suit) are wild; for PE_VP_JOKER_POKER the joker (bit 52, see
 * PE_VP_JOKER_CARD) is wild. A wild hand is scored as its best possible
 * category, which is the rule video poker machines use.
 *
 * Returns -1 for an invalid variant or a hand that is not exactly 5 cards
 * (for joker poker, 4 natural cards plus the joker). */
POKEREVAL_EXPORT int pe_video_poker_category(pe_video_poker_variant_t variant,
                                             StdDeck_CardMask hand);

/* One derived outcome category of an optimal-strategy derivation. */
typedef struct {
    const char *name;       /* human readable category name */
    double payout;          /* payout multiplier for this category */
    long long combinations; /* combinations under the counting convention */
} pe_vp_derived_category_t;

/* Result of a full optimal-strategy derivation for one paytable. */
typedef struct {
    pe_vp_derived_category_t categories[PE_PAYTABLE_MAX_ROWS];
    int num_categories;          /* number of rows filled */
    long long total_combinations;/* sum of combinations (see header note)  */
    long long num_deals;         /* C(52,5) or C(53,5) initial deals       */
    double total_ev;             /* expected return per unit wager         */
} pe_vp_derived_strategy_t;

/* Derive the optimal-hold outcome frequencies for a paytable.
 *
 * payouts[i] must be the payout multiplier of category i in the variant's
 * canonical row order (see the category enums above), so num_payouts must
 * equal pe_video_poker_num_categories(variant). A non-standard table such as
 * 8/5 Jacks or Better is just a different payout vector for the same
 * variant.
 *
 * Algorithm: every 5-card deal is reduced to its suit-isomorphism class via a
 * canonical suit-relabeling key (134,459 classes for the 52-card deck, a
 * 19.3x reduction from 2,598,960 deals; 16,432 classes for the 4-card joker
 * deals of the 53-card deck). The outcome frequencies are derived per class:
 * a completion table is built once per variant that stores, for every
 * suit-isomorphism class of t-card sets (t = 0..5), the category counts of
 * all its completions to a 5-card hand; the expected payout of a keep/discard
 * mask is then recovered exactly by inclusion-exclusion over the discarded
 * natural cards (at most 2^5 subsets), and the mask maximizing expected
 * payout is the optimal hold. The resulting category frequencies are
 * accumulated with the per-deal weight convention described at the top of
 * this header, which makes the derived combinations exactly comparable to
 * the published tables. The derivation cross-checks a sample of classes
 * against the generalized draw optimizer (pe_compute_draw_optima_fn).
 *
 * Cost: roughly 10^7 hand evaluations to build the completion table, then a
 * few thousand arithmetic operations per deal class (parallel with OpenMP
 * when available); a full derivation takes seconds to tens of seconds. Do
 * not run the derivation inside the default test suite.
 *
 * Returns 0 on success, -1 on invalid input. */
POKEREVAL_EXPORT int pe_video_poker_derive_strategy(
    pe_video_poker_variant_t variant,
    const double *payouts,
    int num_payouts,
    pe_vp_derived_strategy_t *out_strategy);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_VIDEO_POKER_STRATEGY_H */
