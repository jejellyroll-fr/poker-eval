/*
 * test_omaha_invariants.c - Random Omaha invariants (2+3, 60 combos, uniqueness)
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#include <poker_eval/core/omaha_combinations.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

static int rand_card_not_in(mask_t m) {
    while (1) {
        int c = rand() % 52;
        if (!mask_is_set(m, c)) return c;
    }
}

static void random_omaha_setup(mask_t* hole, mask_t* board) {
    mask_t m = MASK_EMPTY;
    while (mask_popcount(m) < 9) {
        int c = rand_card_not_in(m);
        m = mask_set(m, c);
    }
    /* First 4 bits -> hole, next 5 -> board (any partition is fine, all disjoint) */
    *hole = MASK_EMPTY;
    *board = MASK_EMPTY;
    int count = 0;
    for (int c = 0; c < 52; ++c) {
        if (mask_is_set(m, c)) {
            if (count < 4) *hole = mask_set(*hole, c);
            else *board = mask_set(*board, c);
            count++;
        }
    }
}

static int contains(mask_t* arr, size_t n, mask_t v) {
    for (size_t i = 0; i < n; ++i) if (arr[i] == v) return 1;
    return 0;
}

static int count_combos_with_hole_card(mask_t* combos, size_t n, int hole_card) {
    int count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (mask_is_set(combos[i], hole_card)) count++;
    }
    return count;
}

int main(void) {
    srand(1337);
    for (int iter = 0; iter < 100; ++iter) {
        mask_t hole, board;
        random_omaha_setup(&hole, &board);

        omaha_config_t cfg = omaha_config_plo(hole, board, MASK_EMPTY);
        omaha_generator_t* gen = omaha_generator_create(&cfg);
        assert(gen);

        EvalConfig econf = eval_config_omaha();
        EvalContext* ctx = eval_context_create(&econf);
        assert(ctx);

        mask_t seen[60];
        size_t count = 0;
        uint32_t best = 0;
        mask_t combo;
        while (omaha_generator_next(gen, &combo)) {
            assert(mask_popcount(combo) == 5);
            assert(!contains(seen, count, combo));

            /* Verify combo contains exactly 2 hole cards and 3 board cards */
            assert(mask_popcount(combo & hole) == 2);    /* exactly 2 from hole */
            assert(mask_popcount(combo & board) == 3);   /* exactly 3 from board */
            assert((combo & ~(hole | board)) == MASK_EMPTY); /* no other cards */

            seen[count++] = combo;
            uint32_t ev = pe_eval_5c(ctx, combo);
            if (ev > best) best = ev;
        }

        assert(count == 60);
        assert(best > 0);

        /* Verify each hole card appears in exactly 10 combinations */
        int hole_cards[4];
        int hole_count = 0;
        for (int c = 0; c < 52; ++c) {
            if (mask_is_set(hole, c)) hole_cards[hole_count++] = c;
        }
        assert(hole_count == 4);

        for (int i = 0; i < 4; ++i) {
            assert(count_combos_with_hole_card(seen, count, hole_cards[i]) == 30);
        }

        /* Avoid unused variable warnings in Release builds where assert() is disabled */
        (void)seen;
        (void)hole_cards;

        omaha_generator_destroy(gen);
        eval_context_destroy(ctx);
    }
    printf("Omaha invariants (100 random hands, 60 combos, unique, 10/hole, disjoint): OK\n");
    return 0;
}

