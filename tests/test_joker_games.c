/*
 * test_joker_games.c - Test all joker-enabled games
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/universal_deck.h>

static void test_5draw_hi(void) {
    printf("Testing 5-card Draw Hi with joker (-5d)...\n");
    
    enum_game_t game = game_5draw;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 2;
    int nboard = 0;
    int niter = 10000; // Use Monte Carlo to avoid timeout
    int err;
    
    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Player 1: As Ah Ad (trips, needs 2)
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    
    // Player 2: Ks Kh Kd (trips, needs 2)
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    
    // Set dead cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    
    printf("  Player 1: A♠ A♥ A♦ (needs 2)\n");
    printf("  Player 2: K♠ K♥ K♦ (needs 2)\n");
    printf("  Using Monte Carlo with %d iterations\n", niter);
    
    err = enumSample(game, pockets, board, dead, npockets, nboard, niter, 0, &result);
    
    if (err) {
        printf("  ✗ Error: enumSample returned %d\n", err);
    } else {
        printf("  ✓ 5-draw Hi enumeration successful\n");
        printf("    Samples: %u\n", result.nsamples);
        printf("    Player 1 (AAA) EV: %.3f\n", result.ev[0]);
        printf("    Player 2 (KKK) EV: %.3f\n", result.ev[1]);
        
        // Player 1 should have advantage
        if (result.ev[0] > result.ev[1]) {
            printf("  ✓ Aces beat Kings as expected\n");
        }
    }
    
    enumResultFree(&result);
}

static void test_5draw8_hilo(void) {
    printf("\nTesting 5-card Draw Hi/Lo 8-or-better with joker (-5d8)...\n");
    
    enum_game_t game = game_5draw8;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 2;
    int nboard = 0;
    int niter = 10000;
    int err;
    
    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Player 1: Ac 2c 3c 4c (low draw, needs 1)
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    
    // Player 2: Kh Kd Ks Kc (quads!)
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    
    // Set dead cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    
    printf("  Player 1: A♣ 2♣ 3♣ 4♣ (low draw)\n");
    printf("  Player 2: K♥ K♦ K♠ K♣ (quads!)\n");
    printf("  Using Monte Carlo with %d iterations\n", niter);
    
    err = enumSample(game, pockets, board, dead, npockets, nboard, niter, 0, &result);
    
    if (err) {
        printf("  ✗ Error: enumSample returned %d\n", err);
    } else {
        printf("  ✓ 5-draw Hi/Lo 8 enumeration successful\n");
        printf("    Samples: %u\n", result.nsamples);
        printf("    Player 1 EV: %.3f (Hi wins: %d, Lo wins: %d)\n", 
               result.ev[0], result.nwinhi[0], result.nwinlo[0]);
        printf("    Player 2 EV: %.3f (Hi wins: %d, Lo wins: %d)\n", 
               result.ev[1], result.nwinhi[1], result.nwinlo[1]);
        
        // Player 2 should win high, Player 1 might win low
        if (result.nwinhi[1] > result.nwinhi[0]) {
            printf("  ✓ Quads win high as expected\n");
        }
    }
    
    enumResultFree(&result);
}

static void test_5drawnsq_hilo(void) {
    printf("\nTesting 5-card Draw Hi/Lo no qualifier with joker (-5dnsq)...\n");
    
    enum_game_t game = game_5drawnsq;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 2;
    int nboard = 0;
    int niter = 10000;
    int err;
    
    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Player 1: 9h 9d 9s 9c (quads)
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    
    // Player 2: Jh Jd Js (trips, needs 2)
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    
    // Set dead cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    
    printf("  Player 1: 9♥ 9♦ 9♠ 9♣ (quads)\n");
    printf("  Player 2: J♥ J♦ J♠ (trips, needs 2)\n");
    printf("  No qualifier for low - any hand can win low\n");
    printf("  Using Monte Carlo with %d iterations\n", niter);
    
    err = enumSample(game, pockets, board, dead, npockets, nboard, niter, 0, &result);
    
    if (err) {
        printf("  ✗ Error: enumSample returned %d\n", err);
    } else {
        printf("  ✓ 5-draw Hi/Lo no qualifier enumeration successful\n");
        printf("    Samples: %u\n", result.nsamples);
        printf("    Player 1 EV: %.3f\n", result.ev[0]);
        printf("    Player 2 EV: %.3f\n", result.ev[1]);
        
        // With no qualifier, both players compete for low
        printf("    Player 1 Lo wins: %d\n", result.nwinlo[0]);
        printf("    Player 2 Lo wins: %d\n", result.nwinlo[1]);
    }
    
    enumResultFree(&result);
}

int main(void) {
    printf("=== All Joker Games Tests ===\n\n");
    
    test_5draw_hi();
    test_5draw8_hilo();
    test_5drawnsq_hilo();
    
    printf("\n✅ All joker game tests completed!\n");
    
    return 0;
}
