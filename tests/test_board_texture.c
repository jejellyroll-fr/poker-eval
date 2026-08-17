/*
 * test_board_texture.c - Unit tests for the FEAT-13 board texture categorizer
 *
 * Copyright (C) 2026 poker-eval contributors
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/engine/solvers/cfr/board_texture.h>

static mask_t C(int rank, int suit) { return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit)); }

static mask_t board3(int r0, int s0, int r1, int s1, int r2, int s2)
{
    return mask_set(mask_set(C(r0, s0), MODERN_MAKE_CARD(r1, s1)), MODERN_MAKE_CARD(r2, s2));
}

static void test_monotone(void)
{
    printf("test_board_texture: monotone flop ... ");
    mask_t b = board3(0, 0, 1, 0, 2, 0); /* A K Q all hearts */
    pe_board_texture_t t;
    assert(pe_board_analyze(b, PE_TEXTURE_FILTER_LARGE, &t) == 0);
    assert(t.is_monotone);
    assert(!t.is_two_tone);
    assert(!t.is_rainbow);
    assert(t.texture_class == PE_BOARD_TEXTURE_MONOTONE);
    printf("PASS\n");
}

static void test_rainbow(void)
{
    printf("test_board_texture: rainbow flop ... ");
    mask_t b = board3(1, 0, 6, 1, 12, 2); /* K 7 2 rainbow (non-broadway, dry) */
    pe_board_texture_t t;
    assert(pe_board_analyze(b, PE_TEXTURE_FILTER_LARGE, &t) == 0);
    assert(t.is_rainbow);
    assert(!t.is_monotone);
    assert(!t.is_two_tone);
    assert(t.texture_class == PE_BOARD_TEXTURE_DRY);
    printf("PASS\n");
}

static void test_paired(void)
{
    printf("test_board_texture: paired flop ... ");
    mask_t b = board3(1, 0, 1, 1, 6, 2); /* K K 7 */
    pe_board_texture_t t;
    assert(pe_board_analyze(b, PE_TEXTURE_FILTER_LARGE, &t) == 0);
    assert(t.is_paired);
    assert(!t.is_trips);
    assert(t.paired_rank == 1);
    assert(t.texture_class == PE_BOARD_TEXTURE_PAIRED);
    printf("PASS\n");
}

static void test_connected_wet(void)
{
    printf("test_board_texture: connected wet flop ... ");
    mask_t b = board3(8, 0, 7, 0, 6, 2); /* T 9 8 : two spades, one diamond */
    pe_board_texture_t t;
    assert(pe_board_analyze(b, PE_TEXTURE_FILTER_LARGE, &t) == 0);
    assert(t.is_connected);
    assert(t.is_two_tone);
    assert(!t.is_rainbow);
    assert(t.texture_class == PE_BOARD_TEXTURE_WET);
    printf("PASS\n");
}

static void test_turn_and_river(void)
{
    printf("test_board_texture: turn / river card counts ... ");
    /* Turn: 4 cards */
    mask_t turn = mask_set(board3(0, 0, 1, 1, 6, 2), MODERN_MAKE_CARD(3, 3)); /* A K 7 4 */
    pe_board_texture_t tt;
    assert(pe_board_analyze(turn, PE_TEXTURE_FILTER_MEDIUM, &tt) == 0);
    assert(tt.n_cards == 4);

    /* River: 5 cards */
    mask_t river = mask_set(turn, MODERN_MAKE_CARD(9, 0)); /* A K 7 4 J */
    pe_board_texture_t tr;
    assert(pe_board_analyze(river, PE_TEXTURE_FILTER_MEDIUM, &tr) == 0);
    assert(tr.n_cards == 5);
    printf("PASS\n");
}

static void test_invalid(void)
{
    printf("test_board_texture: invalid input rejected ... ");
    mask_t two = board3(0, 0, 1, 1, 1, 1); /* force only 2 distinct via mask reset */
    mask_t bad = mask_set(C(0, 0), MODERN_MAKE_CARD(1, 1)); /* 2 cards */
    pe_board_texture_t t;
    assert(pe_board_analyze(bad, PE_TEXTURE_FILTER_LARGE, &t) == -1);
    (void)two;
    printf("PASS\n");
}

static void test_density_monotonic(void)
{
    printf("test_board_texture: density monotonic in filter level ... ");
    assert(pe_board_texture_density(PE_TEXTURE_FILTER_PERFECT) == 1.0);
    assert(pe_board_texture_density(PE_TEXTURE_FILTER_NONE) == 0.0);
    assert(pe_board_texture_density(PE_TEXTURE_FILTER_LARGE) >
           pe_board_texture_density(PE_TEXTURE_FILTER_MEDIUM));
    assert(pe_board_texture_density(PE_TEXTURE_FILTER_MEDIUM) >
           pe_board_texture_density(PE_TEXTURE_FILTER_SMALL));
    printf("PASS\n");
}

static void test_texture_id_merging(void)
{
    printf("test_board_texture: texture id merges under coarser levels ... ");
    /* Two rainbow dry boards that differ only in high card: must collide under
     * SMALL (wet/dry axis only). A dry vs a monotone (wet) board must differ
     * under LARGE (coarse texture class). */
    mask_t a = board3(0, 0, 1, 1, 6, 2); /* A K 7 rainbow dry */
    mask_t b = board3(2, 0, 3, 1, 6, 2); /* Q J 7 rainbow dry */
    mask_t c = board3(8, 0, 7, 0, 6, 2); /* T 9 8 monotone -> WET */
    uint64_t id_small_a = pe_board_texture_id(a, PE_TEXTURE_FILTER_SMALL);
    uint64_t id_small_b = pe_board_texture_id(b, PE_TEXTURE_FILTER_SMALL);
    uint64_t id_large_a = pe_board_texture_id(a, PE_TEXTURE_FILTER_LARGE);
    uint64_t id_large_c = pe_board_texture_id(c, PE_TEXTURE_FILTER_LARGE);
    assert(id_small_a == id_small_b);   /* both dry -> merged */
    assert(id_large_a != id_large_c);   /* dry vs wet -> separate */
    /* PERFECT must keep the raw board (no merging). */
    assert(pe_board_texture_id(a, PE_TEXTURE_FILTER_PERFECT) == (uint64_t)a);
    printf("PASS\n");
}

int main(void)
{
    printf("=== test_board_texture (FEAT-13) ===\n");
    test_monotone();
    test_rainbow();
    test_paired();
    test_connected_wet();
    test_turn_and_river();
    test_invalid();
    test_density_monotonic();
    test_texture_id_merging();
    printf("=== all board_texture tests passed ===\n");
    return 0;
}
