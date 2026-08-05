/*
 * generate_pineapple8_rankings.c - Generate a static Pineapple Hi/Lo hand rankings lookup
 *
 * Computes preflop equity (vs. a random 3-card hand) for every canonical
 * Pineapple Hi/Lo starting hand and writes a compact C header containing the
 * canonical keys sorted best-first. The Advanced Range Parser consumes this
 * table to expand percentage ranges (e.g. "15%", "20.5%") without running
 * Monte-Carlo at call time.
 *
 * The output header is committed to the tree (src/range/pineapple8_hand_rankings.h);
 * re-run this tool only when the definition of "best" hand needs refreshing.
 *
 * Usage: generate_pineapple8_rankings <output.h> [samples_per_hand] [seed]
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/pineapple_preflop.h>

/* Total number of 3-card combinations from a 52-card deck: C(52,3) */
#define TOTAL_HANDS 22100u

typedef struct {
    pineapple_hand_key_t key;
    float equity;
} ranked_entry_t;

/* Deal `n` random cards from `dead` into `out` (and mark them dead). */
static void deal_random(StdDeck_CardMask *dead, StdDeck_CardMask *out, int n) {
    StdDeck_CardMask_RESET(*out);
    for (int c = 0; c < n; ++c) {
        int idx;
        do {
            idx = rand() % 52;
        } while (StdDeck_CardMask_CARD_IS_SET(*dead, idx));
        StdDeck_CardMask_SET(*out, idx);
        StdDeck_CardMask_SET(*dead, idx);
    }
}

/* Preflop equity of `hand` vs a random 3-card opponent, via Monte-Carlo.
 * Both players see the same 5-card board; the opponent pocket and the
 * board are dealt from the 49-card stub left over after removing `hand`. */
static double pineapple_equity_vs_random(StdDeck_CardMask hand, int samples) {
    StdDeck_CardMask empty;
    StdDeck_CardMask_RESET(empty);
    StdDeck_CardMask pockets[2];
    pockets[0] = hand;

    double equity = 0.0;
    for (int it = 0; it < samples; ++it) {
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);
        StdDeck_CardMask_OR(dead, dead, hand);

        StdDeck_CardMask opp, board;
        deal_random(&dead, &opp, 3);
        deal_random(&dead, &board, 5);

        pockets[1] = opp;
        enum_result_t res;
        enumSample(game_pineapple8, pockets, board, empty, 2, 5, 1, 0, &res);
        equity += res.ev[0];
    }
    return equity / (double)samples;
}

static int cmp_ranked_desc(const void *a, const void *b) {
    const ranked_entry_t *ea = (const ranked_entry_t *)a;
    const ranked_entry_t *eb = (const ranked_entry_t *)b;
    if (ea->equity > eb->equity) return -1;
    if (ea->equity < eb->equity) return 1;
    if (ea->key > eb->key) return 1;
    if (ea->key < eb->key) return -1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <output.h> [samples_per_hand] [seed]\n", argv[0]);
        return 1;
    }
    const char *outfile = argv[1];
    int samples = (argc > 2) ? atoi(argv[2]) : 500;
    unsigned seed = (argc > 3) ? (unsigned)strtoul(argv[3], NULL, 10) : 0;

    srand(seed);

    /* Enumerate every canonical 3-card hand exactly once.  A dynamic table
     * sized to the max canonical key space is too large without the whole
     * (uncommitted) equity table, so we enumerate all concrete hands and
     * keep the first representative for every distinct canonical key. */
    static pineapple_hand_key_t avail[TOTAL_HANDS];
    static uint8_t density[(1u << 16) / 8];
    size_t class_count = 0;

    for (int i = 0; i + 3 <= 52; ++i) {
        for (int j = i + 1; j < 52; ++j) {
            for (int k = j + 1; k < 52; ++k) {
                StdDeck_CardMask hand;
                StdDeck_CardMask_RESET(hand);
                StdDeck_CardMask_SET(hand, i);
                StdDeck_CardMask_SET(hand, j);
                StdDeck_CardMask_SET(hand, k);
                pineapple_hand_key_t key = pineapple_cards_to_key(hand);
                if (key < (1u << 16) && !(density[key >> 3] & (1u << (key & 7)))) {
                    density[key >> 3] |= (uint8_t)(1u << (key & 7));
                    avail[class_count++] = key;
                }
            }
        }
    }

    fprintf(stderr, "Found %zu canonical classes.\n", class_count);

    ranked_entry_t *ranks = (ranked_entry_t *)malloc(class_count * sizeof(ranked_entry_t));
    if (!ranks) { fprintf(stderr, "alloc failed\n"); return 1; }

    for (size_t idx = 0; idx < class_count; ++idx) {
        pineapple_hand_key_t key = avail[idx];

        /* Reconstruct a representative hand so equity can be evaluated. */
        uint32_t rp = key >> 4;
        int ranks3[3] = { (int)((rp >> 8) & 0xF), (int)((rp >> 4) & 0xF),
                            (int)(rp & 0xF) };

        StdDeck_CardMask rep;
        StdDeck_CardMask_RESET(rep);
        int have_rep = 0;
        for (int a1 = 0; a1 < 4 && !have_rep; ++a1)
        for (int a2 = 0; a2 < 4 && !have_rep; ++a2)
        for (int a3 = 0; a3 < 4 && !have_rep; ++a3) {
            int suits[3] = { a1, a2, a3 };
            int valid = 1;
            for (int c = 0; c < 3 && valid; ++c)
                for (int d = c + 1; d < 3; ++d)
                    if (ranks3[c] == ranks3[d] && suits[c] == suits[d]) { valid = 0; break; }
            if (!valid) continue;

            StdDeck_CardMask m;
            StdDeck_CardMask_RESET(m);
            for (int c = 0; c < 3; ++c)
                StdDeck_CardMask_SET(m, StdDeck_MAKE_CARD(ranks3[c], suits[c]));
            if (pineapple_cards_to_key(m) != key) continue;

            rep = m;
            have_rep = 1;
        }
        if (!have_rep) {
            fprintf(stderr, "Internal error: no representative for key 0x%X\n", (unsigned)key);
            free(ranks);
            return 1;
        }

        StdDeck_CardMask empty;
        StdDeck_CardMask_RESET(empty);
        ranks[idx].key = key;
        ranks[idx].equity = (float)pineapple_equity_vs_random(rep, samples);

        if (idx % 1000 == 0)
            fprintf(stderr, "\rEquity %zu/%zu ...", idx, class_count);
    }
    fprintf(stderr, "\n");

    qsort(ranks, class_count, sizeof(ranked_entry_t), cmp_ranked_desc);

    FILE *f = fopen(outfile, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", outfile); free(ranks); return 1; }

    fprintf(f, "/* This is a GENERATED file. Do not edit by hand.                   */\n");
    fprintf(f, "/* Canonical Pineapple Hi/Lo hands ranked by preflop equity.       */\n");
    fprintf(f, "/* Canonical : suits normalized, ranks descending (Ace..2), and    */\n");
    fprintf(f, "/* equal-ranked cards interchangeable (see pineapple_preflop.h).   */\n");
    fprintf(f, "/*                                                                 */\n");
    fprintf(f, "/* Equity is Monte-Carlo estimated, so the ordering of near-equal  */\n");
    fprintf(f, "/* hands depends on the sample count and seed. Regenerate with the */\n");
    fprintf(f, "/* exact arguments below to reproduce this table:                  */\n");
    fprintf(f, "/*   generate_pineapple8_rankings <out.h> %d %u\n", samples, seed);
    fprintf(f, "*/\n\n");

    fprintf(f, "#ifndef POKER_EVAL_PINEAPPLE8_HAND_RANKINGS_H\n");
    fprintf(f, "#define POKER_EVAL_PINEAPPLE8_HAND_RANKINGS_H\n\n");
    fprintf(f, "#include \"poker_eval/equity/pineapple_preflop.h\"\n\n");

    fprintf(f, "/* %zu canonical classes sorted by equity, best-first */\n", class_count);
    fprintf(f, "static const pineapple_hand_key_t pineapple8_hand_rankings[] = {\n");
    for (size_t idx = 0; idx < class_count; ++idx) {
        fprintf(f, "    %uU,\n", (unsigned)ranks[idx].key);
    }
    fprintf(f, "};\n\n");
    fprintf(f, "#define PINEAPPLE8_HAND_RANKINGS_COUNT %zu\n", class_count);
    fprintf(f, "#define PINEAPPLE8_ALL_CONCRETE_HANDS %u\n", TOTAL_HANDS);
    fprintf(f, "\n#endif /* POKER_EVAL_PINEAPPLE8_HAND_RANKINGS_H */\n");

    fclose(f);
    fprintf(stderr, "Wrote %zu ranked keys to %s\n", class_count, outfile);

    free(ranks);
    return 0;
}
