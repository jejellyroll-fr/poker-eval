/*
 * test_batched_montecarlo.c - Unit tests for batched Monte-Carlo implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/batched_montecarlo.h>

#define TEST_ITERATIONS 50000
#define TOLERANCE 0.02  /* 2% tolerance for Monte-Carlo results */

static int verbose_batched_logs(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_BATCHED_MC");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

/* Test that batched and regular enumSample produce similar results */
static int test_results_match(enum_game_t game, const char *test_name,
                             StdDeck_CardMask pockets[], int npockets,
                             StdDeck_CardMask board, StdDeck_CardMask dead,
                             int nboard) {
    enum_result_t result_regular, result_batched;
    int err;
    int passed = 1;
    
    printf("Testing %s...\n", test_name);
    
    /* Run regular enumSample */
    err = enumSample(game, pockets, board, dead, npockets, nboard, 
                    TEST_ITERATIONS, 0, &result_regular);
    if (err) {
        printf("  FAILED: Regular enumSample returned error %d\n", err);
        return 0;
    }
    
    /* Run batched enumSample */
    err = enumSampleBatched(game, pockets, board, dead, npockets, nboard,
                           TEST_ITERATIONS, 0, &result_batched);
    if (err) {
        printf("  FAILED: Batched enumSample returned error %d\n", err);
        return 0;
    }
    
    /* Compare results */
    for (int i = 0; i < npockets; i++) {
        double ev_regular = result_regular.ev[i] / result_regular.nsamples;
        double ev_batched = result_batched.ev[i] / result_batched.nsamples;
        double diff = fabs(ev_regular - ev_batched);
        if (verbose_batched_logs()) {
            printf("    player %d regular=%.6f batched=%.6f diff=%.6f (nsamples regular=%u batched=%u)\n",
                   i, ev_regular, ev_batched, diff,
                   result_regular.nsamples, result_batched.nsamples);
        }
        
        if (diff > TOLERANCE) {
            printf("  FAILED: Player %d EV differs too much (regular: %.4f, batched: %.4f, diff: %.4f)\n",
                   i, ev_regular, ev_batched, diff);
            passed = 0;
        }
    }
    
    if (passed) {
        printf("  PASSED: Results match within tolerance\n");
    }
    
    return passed;
}

/* Test Hold'em heads-up */
static int test_holdem_headsup(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    /* Player 1: As Ah */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    
    /* Player 2: Ks Kh */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    return test_results_match(game_holdem, "Hold'em Heads-up (AA vs KK)",
                             pockets, 2, board, dead, 0);
}

/* Test Hold'em with flop */
static int test_holdem_with_flop(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    /* Player 1: As Ks */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    
    /* Player 2: Qh Qd */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    
    /* Flop: Qs 7s 2c */
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    
    /* Mark all cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);
    
    return test_results_match(game_holdem, "Hold'em with Flop (AKs vs QQ on Qs7s2c)",
                             pockets, 2, board, dead, 3);
}

/* Test Hold'em Hi/Lo */
static int test_holdem8(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    /* Player 1: A2s */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    
    /* Player 2: KK */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    
    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    return test_results_match(game_holdem8, "Hold'em Hi/Lo (A2s vs KK)",
                             pockets, 2, board, dead, 0);
}

/* Test Omaha */
static int test_omaha(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    /* Player 1: AsAhKsKh */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    /* Player 2: QcQdJcJd */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    
    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    return test_results_match(game_omaha, "Omaha (AAKK vs QQJJ)",
                             pockets, 2, board, dead, 0);
}

/* Test batch generation */
static int test_batch_generation(void) {
    BoardBatch batch;
    StdDeck_CardMask dead;
    int passed = 1;
    
    printf("Testing batch generation...\n");
    
    StdDeck_CardMask_RESET(dead);
    
    /* Generate a batch */
    generateBoardBatch(&batch, dead, 5, BATCH_SIZE);
    
    /* Check that we got the right number of boards */
    if (batch.count != BATCH_SIZE) {
        printf("  FAILED: Expected %d boards, got %d\n", BATCH_SIZE, batch.count);
        return 0;
    }
    
    /* Check that each board has 5 cards */
    for (int i = 0; i < batch.count; i++) {
        int numCards = StdDeck_numCards(batch.boards[i]);
        if (numCards != 5) {
            printf("  FAILED: Board %d has %d cards, expected 5\n", i, numCards);
            passed = 0;
        }
    }
    
    /* Check that boards are different (with high probability) */
    int duplicates = 0;
    for (int i = 0; i < batch.count - 1; i++) {
        for (int j = i + 1; j < batch.count; j++) {
            if (StdDeck_CardMask_EQUAL(batch.boards[i], batch.boards[j])) {
                duplicates++;
            }
        }
    }
    
    if (duplicates > batch.count / 10) {  /* Allow up to 10% duplicates */
        printf("  FAILED: Too many duplicate boards (%d out of %d)\n", 
               duplicates, batch.count * (batch.count - 1) / 2);
        passed = 0;
    }
    
    if (passed) {
        printf("  PASSED: Batch generation works correctly\n");
    }
    
    return passed;
}

/* Test 7-Card Stud */
static int test_7stud(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: (As Ah) Ks ... needs 4 more */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    /* Player 2: (Qs Qh) Js ... needs 4 more */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));

    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    return test_results_match(game_7stud, "7-Card Stud (AAK vs QQJ)",
                             pockets, 2, board, dead, 0);
}

/* Test 7-Card Stud Hi/Lo */
static int test_7stud8(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: (As 2s) 3s ... low draw / flush draw */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));

    /* Player 2: (Ks Kh) Kd ... high trips */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));

    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    return test_results_match(game_7stud8, "7-Card Stud Hi/Lo (A23 vs KKK)",
                             pockets, 2, board, dead, 0);
}

/* Test Razz */
static int test_razz(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: (As 2s) 3s ... excellent start */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));

    /* Player 2: (Ks Kh) Kd ... terrible start */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));

    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    return test_results_match(game_razz, "Razz (A23 vs KKK)",
                             pockets, 2, board, dead, 0);
}

/* Test 2-7 Lowball (No Draw, dealing remaining cards) */
static int test_lowball27(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: 2s 3h 4d 5c ... needs 1 card */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_CLUBS));

    /* Player 2: As Ah Ad Ac ... needs 1 card, terrible for 2-7 */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));

    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    return test_results_match(game_lowball27, "2-7 Lowball (2345 vs AAAA)",
                             pockets, 2, board, dead, 0);
}

/* Test 2-7 Triple Draw */
static int test_27_triple_draw(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: 2s 3h 4d ... needs 2 cards */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_DIAMONDS));

    /* Player 2: 8s 9h Td ... needs 2 cards */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS));

    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    return test_results_match(game_27_triple_draw, "2-7 Triple Draw (234 vs 89T)",
                             pockets, 2, board, dead, 0);
}

/* Test A-5 Triple Draw */
static int test_a5_triple_draw(void) {
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: As 2h 3d ... needs 2 cards */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));

    /* Player 2: 8s 9h Td ... needs 2 cards */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS));

    /* Mark cards as dead */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    return test_results_match(game_a5_triple_draw, "A-5 Triple Draw (A23 vs 89T)",
                             pockets, 2, board, dead, 0);
}

int main(void) {
    int total_tests = 0;
    int passed_tests = 0;
    
    /* Initialize random seed */
    srand((unsigned int)time(NULL));
    
    printf("Batched Monte-Carlo Unit Tests\n");
    printf("==============================\n\n");
    
    /* Run tests */
    total_tests++; passed_tests += test_batch_generation();
    total_tests++; passed_tests += test_holdem_headsup();
    total_tests++; passed_tests += test_holdem_with_flop();
    total_tests++; passed_tests += test_holdem8();
    total_tests++; passed_tests += test_omaha();
    total_tests++; passed_tests += test_7stud();
    total_tests++; passed_tests += test_7stud8();
    total_tests++; passed_tests += test_razz();
    total_tests++; passed_tests += test_lowball27();
    total_tests++; passed_tests += test_27_triple_draw();
    total_tests++; passed_tests += test_a5_triple_draw();
    
    /* Summary */
    printf("\n==============================\n");
    printf("Tests passed: %d/%d\n", passed_tests, total_tests);
    
    return (passed_tests == total_tests) ? 0 : 1;
}
