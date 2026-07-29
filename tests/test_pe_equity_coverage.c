/*
 * test_pe_equity_coverage.c -- tests for pe_equity.c modern API
 *
 * Validates the modern equity API functions:
 * - pe_equity_calculate()
 * - pe_equity_multiway()
 * - pe_equity_preflop()
 * - pe_equity_range_vs_range()
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <poker_eval/equity.h>
#include <poker_eval/range.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/eval_context.h>

/* Tolerance for floating point comparisons */
#define EV_TOLERANCE 0.01
#define MC_TOLERANCE 0.05  /* Monte Carlo has higher variance */

/* Helper to add a card to a mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit)
{
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Helper to create a simple range from a hand mask array */
static pe_range_t *create_range_from_masks(StdDeck_CardMask *masks, int count)
{
    pe_range_t *range = (pe_range_t *)malloc(sizeof(pe_range_t));
    if (!range) return NULL;

    range->combos = (pe_combo_t *)malloc(count * sizeof(pe_combo_t));
    if (!range->combos) {
        free(range);
        return NULL;
    }

    range->count = count;
    range->capacity = count;
    range->game_type = game_holdem;
    range->total_weight = (double)count;

    for (int i = 0; i < count; i++) {
        range->combos[i].hand = masks[i];
        range->combos[i].weight = 1.0;
    }

    return range;
}

/* Generate pocket pairs for a single rank */
static int generate_pocket_pairs(StdDeck_CardMask *hands, int rank)
{
    int count = 0;
    for (int suit1 = 0; suit1 < 4; suit1++) {
        for (int suit2 = suit1 + 1; suit2 < 4; suit2++) {
            StdDeck_CardMask_RESET(hands[count]);
            add_card(&hands[count], rank, suit1);
            add_card(&hands[count], rank, suit2);
            count++;
        }
    }
    return count; /* Always 6 combos */
}

void setUp(void) {}
void tearDown(void) {}

/* ===== pe_equity_calculate() TESTS ===== */

/*
 * Test basic Holdem equity calculation
 */
void test_pe_equity_calculate_holdem(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(kk_hands, kk_count);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_NOT_NULL(r2);

    /* River board for exact calculation */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    const pe_range_t *ranges[] = {r1, r2};

    status = pe_equity_calculate(game_holdem, ranges, 2, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(2, result.num_players);
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.7, result.results[0].equity); /* AA dominates */
    TEST_ASSERT_LESS_THAN_DOUBLE(0.3, result.results[1].equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

/*
 * Test Omaha equity calculation
 */
void test_pe_equity_calculate_omaha(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    /* Player 1: AAKKds */
    StdDeck_CardMask_RESET(hands1[0]);
    add_card(&hands1[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&hands1[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&hands1[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&hands1[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    /* Player 2: QQJJds */
    StdDeck_CardMask_RESET(hands2[0]);
    add_card(&hands2[0], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&hands2[0], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&hands2[0], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&hands2[0], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    pe_range_t *r1 = create_range_from_masks(hands1, 1);
    pe_range_t *r2 = create_range_from_masks(hands2, 1);
    r1->game_type = game_omaha;
    r2->game_type = game_omaha;

    /* Full board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    const pe_range_t *ranges[] = {r1, r2};

    status = pe_equity_calculate(game_omaha, ranges, 2, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(2, result.num_players);
    /* Both should have some equity */
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[0].equity);
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[1].equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

/* ===== pe_equity_multiway() TESTS ===== */

/*
 * Test 3-player multiway equity
 */
void test_pe_equity_multiway_3p(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6], qq_hands[6];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);
    int qq_count = generate_pocket_pairs(qq_hands, StdDeck_Rank_QUEEN);

    /* Limit to 2 combos each for speed */
    if (aa_count > 2) aa_count = 2;
    if (kk_count > 2) kk_count = 2;
    if (qq_count > 2) qq_count = 2;

    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(kk_hands, kk_count);
    pe_range_t *r3 = create_range_from_masks(qq_hands, qq_count);

    /* Full board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    const pe_range_t *ranges[] = {r1, r2, r3};

    status = pe_equity_multiway(NULL, game_holdem, ranges, 3, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(3, result.num_players);
    /* AA should have highest equity */
    TEST_ASSERT_GREATER_THAN_DOUBLE(result.results[1].equity, result.results[0].equity);
    TEST_ASSERT_GREATER_THAN_DOUBLE(result.results[2].equity, result.results[0].equity);

    pe_range_free(r1);
    pe_range_free(r2);
    pe_range_free(r3);
}

/*
 * Test 4-player multiway equity
 */
void test_pe_equity_multiway_4p(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6], qq_hands[6], jj_hands[6];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    /* Single combo each for speed */
    StdDeck_CardMask_RESET(aa_hands[0]);
    add_card(&aa_hands[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&aa_hands[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(kk_hands[0]);
    add_card(&kk_hands[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&kk_hands[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(qq_hands[0]);
    add_card(&qq_hands[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&qq_hands[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask_RESET(jj_hands[0]);
    add_card(&jj_hands[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&jj_hands[0], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    pe_range_t *r1 = create_range_from_masks(aa_hands, 1);
    pe_range_t *r2 = create_range_from_masks(kk_hands, 1);
    pe_range_t *r3 = create_range_from_masks(qq_hands, 1);
    pe_range_t *r4 = create_range_from_masks(jj_hands, 1);

    /* Full board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    const pe_range_t *ranges[] = {r1, r2, r3, r4};

    status = pe_equity_multiway(NULL, game_holdem, ranges, 4, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(4, result.num_players);

    pe_range_free(r1);
    pe_range_free(r2);
    pe_range_free(r3);
    pe_range_free(r4);
}

/* ===== pe_equity_preflop() TESTS ===== */

/*
 * Test preflop equity calculation
 */
void test_pe_equity_preflop(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    pe_equity_result_multi_t result;
    pe_status_t status;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    /* Limit for speed */
    if (aa_count > 3) aa_count = 3;
    if (kk_count > 3) kk_count = 3;

    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(kk_hands, kk_count);

    const pe_range_t *ranges[] = {r1, r2};

    pe_equity_opts_t opts = {
        .is_monte_carlo = 1,
        .iterations = 10000,  /* Low iterations for speed */
        .timeout_ms = 0
    };

    status = pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(2, result.num_players);
    TEST_ASSERT_EQUAL_INT(0, result.exact); /* Monte Carlo */
    /* AA vs KK preflop is ~82% vs ~18% */
    TEST_ASSERT_DOUBLE_WITHIN(MC_TOLERANCE, 0.82, result.results[0].equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

/* ===== pe_equity_range_vs_range() TESTS ===== */

/*
 * Test range vs range calculation
 */
void test_pe_equity_range_vs_range(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(kk_hands, kk_count);

    /* Full board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    status = pe_equity_range_vs_range(NULL, game_holdem, r1, r2, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(2, result.num_players);
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.7, result.results[0].equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

/* ===== HI/LO GAME TESTS ===== */

/*
 * Test Hi/Lo game equity (Omaha8)
 */
void test_pe_equity_hilo_game(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    /* Player 1: A2KK - good for low */
    StdDeck_CardMask_RESET(hands1[0]);
    add_card(&hands1[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&hands1[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&hands1[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&hands1[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    /* Player 2: QQJJ - no low */
    StdDeck_CardMask_RESET(hands2[0]);
    add_card(&hands2[0], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&hands2[0], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&hands2[0], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&hands2[0], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    pe_range_t *r1 = create_range_from_masks(hands1, 1);
    pe_range_t *r2 = create_range_from_masks(hands2, 1);
    r1->game_type = game_omaha8;
    r2->game_type = game_omaha8;

    /* Board with low possible: 3-4-5-8-K */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    StdDeck_CardMask_RESET(dead);

    const pe_range_t *ranges[] = {r1, r2};

    status = pe_equity_calculate(game_omaha8, ranges, 2, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(2, result.num_players);
    /* Player 1 should have low equity */
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.0, result.hilo_results[0].win_lo_prob + result.hilo_results[0].tie_lo_prob);

    pe_range_free(r1);
    pe_range_free(r2);
}

/* ===== EDGE CASE AND ERROR TESTS ===== */

/*
 * Test invalid parameters return error
 */
void test_pe_equity_invalid_params(void)
{
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* NULL ranges */
    status = pe_equity_calculate(game_holdem, NULL, 2, board, dead, NULL, &result);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG, status);

    /* NULL result */
    StdDeck_CardMask aa_hands[6];
    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(aa_hands, aa_count);
    const pe_range_t *ranges[] = {r1, r2};

    status = pe_equity_calculate(game_holdem, ranges, 2, board, dead, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG, status);

    /* Less than 2 players */
    status = pe_equity_calculate(game_holdem, ranges, 1, board, dead, NULL, &result);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG, status);

    /* 0 players */
    status = pe_equity_calculate(game_holdem, ranges, 0, board, dead, NULL, &result);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG, status);

    /* More than 10 players */
    status = pe_equity_calculate(game_holdem, ranges, 11, board, dead, NULL, &result);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_INVALID_ARG, status);

    pe_range_free(r1);
    pe_range_free(r2);
}

/*
 * Test Monte Carlo vs exhaustive produce similar results
 */
void test_pe_equity_mc_vs_exhaustive(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result_exact, result_mc;
    pe_status_t status;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    /* Limit for speed */
    if (aa_count > 3) aa_count = 3;
    if (kk_count > 3) kk_count = 3;

    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(kk_hands, kk_count);

    /* Full board for exact calculation */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    const pe_range_t *ranges[] = {r1, r2};

    /* Exact calculation */
    status = pe_equity_calculate(game_holdem, ranges, 2, board, dead, NULL, &result_exact);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);

    /* Monte Carlo with high iterations */
    pe_equity_opts_t mc_opts = {
        .is_monte_carlo = 1,
        .iterations = 50000,
        .timeout_ms = 0
    };

    status = pe_equity_calculate(game_holdem, ranges, 2, board, dead, &mc_opts, &result_mc);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);

    /* Results should be similar (within MC tolerance) */
    TEST_ASSERT_DOUBLE_WITHIN(MC_TOLERANCE, result_exact.results[0].equity, result_mc.results[0].equity);
    TEST_ASSERT_DOUBLE_WITHIN(MC_TOLERANCE, result_exact.results[1].equity, result_mc.results[1].equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

/*
 * Test with parsed ranges using pe_range_parse
 */
void test_pe_equity_with_parsed_ranges(void)
{
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;
    pe_range_t *r1 = NULL, *r2 = NULL;

    StdDeck_CardMask_RESET(dead);

    /* Parse AA range */
    status = pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL_INT(6, r1->count);

    /* Parse KK range */
    status = pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_INT(6, r2->count);

    /* Full board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    const pe_range_t *ranges[] = {r1, r2};

    status = pe_equity_calculate(game_holdem, ranges, 2, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    TEST_ASSERT_EQUAL_INT(2, result.num_players);
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.7, result.results[0].equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

/*
 * Test equity calculation with dead cards
 */
void test_pe_equity_with_dead_cards(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(kk_hands, kk_count);

    /* Full board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    /* Dead cards: Ac (blocks 3 AA combos) */
    StdDeck_CardMask_RESET(dead);
    add_card(&dead, StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);

    const pe_range_t *ranges[] = {r1, r2};

    status = pe_equity_calculate(game_holdem, ranges, 2, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);
    /* Should still produce valid equity */
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[0].equity);
    TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(1.0, result.results[0].equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

/*
 * Test result structure fields are populated correctly
 */
void test_pe_equity_result_fields(void)
{
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    StdDeck_CardMask board, dead;
    pe_equity_result_multi_t result;
    pe_status_t status;

    int aa_count = generate_pocket_pairs(aa_hands, StdDeck_Rank_ACE);
    int kk_count = generate_pocket_pairs(kk_hands, StdDeck_Rank_KING);

    pe_range_t *r1 = create_range_from_masks(aa_hands, aa_count);
    pe_range_t *r2 = create_range_from_masks(kk_hands, kk_count);

    /* Full board */
    StdDeck_CardMask_RESET(board);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    StdDeck_CardMask_RESET(dead);

    const pe_range_t *ranges[] = {r1, r2};

    status = pe_equity_calculate(game_holdem, ranges, 2, board, dead, NULL, &result);

    TEST_ASSERT_EQUAL_INT(PE_STATUS_OK, status);

    /* Check all relevant fields are populated */
    TEST_ASSERT_EQUAL_INT(2, result.num_players);
    TEST_ASSERT_GREATER_THAN(0, result.samples);

    /* Check player 0 fields */
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[0].equity);
    TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(1.0, result.results[0].equity);
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[0].win_prob);
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[0].tie_prob);
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[0].ev);

    /* Check player 1 fields */
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(0.0, result.results[1].equity);
    TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(1.0, result.results[1].equity);

    /* Equities should sum to ~1.0 */
    double total_equity = result.results[0].equity + result.results[1].equity;
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.0, total_equity);

    pe_range_free(r1);
    pe_range_free(r2);
}

int main(void)
{
    UNITY_BEGIN();

    /* pe_equity_calculate tests */
    RUN_TEST(test_pe_equity_calculate_holdem);
    RUN_TEST(test_pe_equity_calculate_omaha);

    /* pe_equity_multiway tests */
    RUN_TEST(test_pe_equity_multiway_3p);
    RUN_TEST(test_pe_equity_multiway_4p);

    /* pe_equity_preflop tests */
    RUN_TEST(test_pe_equity_preflop);

    /* pe_equity_range_vs_range tests */
    RUN_TEST(test_pe_equity_range_vs_range);

    /* Hi/Lo game tests */
    RUN_TEST(test_pe_equity_hilo_game);

    /* Edge cases and error handling */
    RUN_TEST(test_pe_equity_invalid_params);
    RUN_TEST(test_pe_equity_mc_vs_exhaustive);
    RUN_TEST(test_pe_equity_with_parsed_ranges);
    RUN_TEST(test_pe_equity_with_dead_cards);
    RUN_TEST(test_pe_equity_result_fields);

    return UNITY_END();
}
