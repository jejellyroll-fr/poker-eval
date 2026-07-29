/*
 * test_joker_multiplayer.c - Test JokerDeck enumeration with 3+ players
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/universal_deck.h>

static void test_3_players(void) {
    printf("Testing 3-player lowball enumeration...\n");
    
    enum_game_t game = game_lowball;
    StdDeck_CardMask pockets[3];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 3;
    int nboard = 0;
    int err;
    
    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Player 1: Ac 2c 3c 4c (needs 1)
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    
    // Player 2: 5d 6d 7d 8d (needs 1)
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    
    // Player 3: 9h Th Jh Qh (needs 1)
    StdDeck_CardMask_RESET(pockets[2]);
    StdDeck_CardMask_SET(pockets[2], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[2], StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[2], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[2], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    
    // Set dead cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    StdDeck_CardMask_OR(dead, dead, pockets[2]);
    
    printf("  Player 1: A♣ 2♣ 3♣ 4♣ (needs 1)\n");
    printf("  Player 2: 5♦ 6♦ 7♦ 8♦ (needs 1)\n");
    printf("  Player 3: 9♥ T♥ J♥ Q♥ (needs 1)\n");
    
    err = enumExhaustive_dispatch(game, pockets, board, dead, npockets, nboard, 0, &result);
    
    if (err) {
        printf("  ✗ Error: enumExhaustive_dispatch returned %d\n", err);
    } else {
        printf("  ✓ 3-player enumeration successful\n");
        printf("    Samples: %u\n", result.nsamples);
        printf("    Player 1 EV: %.3f\n", result.ev[0]);
        printf("    Player 2 EV: %.3f\n", result.ev[1]);
        printf("    Player 3 EV: %.3f\n", result.ev[2]);
        
        // Verify EVs sum to approximately 1.0
        double total_ev = result.ev[0] + result.ev[1] + result.ev[2];
        if (total_ev > 0.99 && total_ev < 1.01) {
            printf("  ✓ EVs sum correctly (%.3f ≈ 1.0)\n", total_ev);
        }
    }
    
    enumResultFree(&result);
}

static void test_4_players_monte_carlo(void) {
    printf("\nTesting 4-player lowball with Monte Carlo...\n");
    
    enum_game_t game = game_lowball;
    StdDeck_CardMask pockets[4];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 4;
    int nboard = 0;
    int niter = 10000;
    int err;
    
    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Each player has 3 cards, needs 2
    for (int i = 0; i < 4; i++) {
        StdDeck_CardMask_RESET(pockets[i]);
    }
    
    // Player 1: A♠ 2♠ 3♠
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES));
    
    // Player 2: 4♥ 5♥ 6♥
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS));
    
    // Player 3: 7♦ 8♦ 9♦
    StdDeck_CardMask_SET(pockets[2], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[2], StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[2], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS));
    
    // Player 4: T♣ J♣ Q♣
    StdDeck_CardMask_SET(pockets[3], StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[3], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[3], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    
    // Set dead cards
    StdDeck_CardMask_RESET(dead);
    for (int i = 0; i < 4; i++) {
        StdDeck_CardMask_OR(dead, dead, pockets[i]);
    }
    
    printf("  Player 1: A♠ 2♠ 3♠ (needs 2)\n");
    printf("  Player 2: 4♥ 5♥ 6♥ (needs 2)\n");
    printf("  Player 3: 7♦ 8♦ 9♦ (needs 2)\n");
    printf("  Player 4: T♣ J♣ Q♣ (needs 2)\n");
    printf("  Using Monte Carlo with %d iterations\n", niter);
    
    err = enumSample(game, pockets, board, dead, npockets, nboard, niter, 0, &result);
    
    if (err) {
        printf("  ✗ Error: enumSample returned %d\n", err);
    } else {
        printf("  ✓ 4-player Monte Carlo successful\n");
        printf("    Samples: %u\n", result.nsamples);
        for (int i = 0; i < 4; i++) {
            printf("    Player %d EV: %.3f (win: %.1f%%)\n", 
                   i+1, result.ev[i], result.ev[i] * 100);
        }
    }
    
    enumResultFree(&result);
}

int main(void) {
    printf("=== Multiplayer JokerDeck Tests ===\n\n");
    
    test_3_players();
    test_4_players_monte_carlo();
    
    printf("\n✅ Multiplayer tests completed!\n");
    
    return 0;
}
