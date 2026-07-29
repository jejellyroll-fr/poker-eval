#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/enumerate_eedc.h>

// Function is declared in enumerate_eedc.h

// Mesure le temps en secondes
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

// Benchmark pour Omaha avec différentes configurations
static void bench_omaha_versions(const char* name, enum_game_t game, int nboard) {
    printf("\n=== %s (board: %d cartes) ===\n", name, nboard);
    
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Joueur 1: AsAhKsKh
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    // Joueur 2: QcQdJcJd
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    
    // Ajouter les cartes utilisées aux cartes mortes
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    // Ajouter des cartes sur le board si demandé
    if (nboard > 0) {
        // Flop: Tc9h8d
        StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS));
        if (nboard > 1) {
            StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
        }
        if (nboard > 2) {
            StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
        }
        if (nboard > 3) {
            StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));
        }
        if (nboard > 4) {
            StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS));
        }
        StdDeck_CardMask_OR(dead, dead, board);
    }
    
    enum_result_t res1, res2, res3;
    double t1, t2, t3;
    
    // Test version classique
    printf("Version classique...\n");
    t1 = get_time();
    enumExhaustive(game, pockets, board, dead, 2, nboard, 0, &res1);
    t1 = get_time() - t1;
    
    // Test version EEDC standard
    printf("Version EEDC standard...\n");
    t2 = get_time();
    enumExhaustive_eedc(game, pockets, board, dead, 2, nboard, 0, &res2);
    t2 = get_time() - t2;
    
    // Test version EEDC optimisée pour Omaha
    printf("Version EEDC Omaha optimisée...\n");
    t3 = get_time();
    enumExhaustive_eedc_omaha_opt(game, pockets, board, dead, 2, nboard, 0, &res3);
    t3 = get_time() - t3;
    
    printf("\nRésultats:\n");
    printf("Classique        : %.3fs (%d boards)\n", t1, res1.nsamples);
    printf("EEDC standard    : %.3fs (%d boards) - Speedup: %.2fx\n", t2, res2.nsamples, t1/t2);
    printf("EEDC Omaha opt   : %.3fs (%d boards) - Speedup: %.2fx\n", t3, res3.nsamples, t1/t3);
    
    printf("Vérification: ");
    if (res1.nsamples == res2.nsamples && res2.nsamples == res3.nsamples &&
        fabs(res1.ev[0] - res2.ev[0]) < 1e-6 && fabs(res2.ev[0] - res3.ev[0]) < 1e-6) {
        printf("✓ Tous les résultats correspondent\n");
    } else {
        printf("✗ Divergence dans les résultats!\n");
    }
}

int main(void) {
    printf("=== Benchmark Optimisations Omaha ===\n");
    
    // Tests avec différents nombres de cartes sur le board
    bench_omaha_versions("Omaha Hi (pas de board)", game_omaha, 0);
    bench_omaha_versions("Omaha Hi (flop)", game_omaha, 3);
    bench_omaha_versions("Omaha Hi (turn)", game_omaha, 4);
    
    printf("\n");
    
    bench_omaha_versions("Omaha Hi/Lo (pas de board)", game_omaha8, 0);
    bench_omaha_versions("Omaha Hi/Lo (flop)", game_omaha8, 3);
    
    return 0;
}
