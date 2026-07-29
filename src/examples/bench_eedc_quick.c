#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/enumerate_eedc.h>

// Mesure le temps en secondes
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

// Benchmark pour un jeu donné avec board partiel
static void bench_game_with_board(const char* name, enum_game_t game, int npockets, int nboard) {
    printf("\n=== %s (avec %d cartes sur le board) ===\n", name, nboard);
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Génère des mains simples valides pour chaque jeu
    for (int i = 0; i < npockets; ++i) {
        StdDeck_CardMask_RESET(pockets[i]);
        
        // Hold'em : 2 cartes
        if (game == game_holdem || game == game_holdem8) {
            StdDeck_CardMask_SET(pockets[i], i*13);     // As de couleur i
            StdDeck_CardMask_SET(pockets[i], i*13+12);  // Roi de couleur i
        }
        // Omaha : 4 cartes
        else if (game == game_omaha || game == game_omaha8) {
            for (int k = 0; k < 4; ++k) {
                StdDeck_CardMask_SET(pockets[i], i*13 + k);
            }
        }
        
        // Ajouter les cartes utilisées aux cartes mortes
        StdDeck_CardMask_OR(dead, dead, pockets[i]);
    }
    
    // Ajouter des cartes sur le board si demandé
    if (nboard > 0) {
        for (int i = 0; i < nboard; ++i) {
            StdDeck_CardMask_SET(board, 26 + i); // Cartes du milieu du deck
        }
        StdDeck_CardMask_OR(dead, dead, board);
    }
    
    enum_result_t res1, res2;
    double t1, t2;
    
    // Test version classique
    t1 = get_time();
    enumExhaustive(game, pockets, board, dead, npockets, nboard, 0, &res1);
    t1 = get_time() - t1;
    
    // Test version dispatch (qui route vers EEDC pour Hold'em/Omaha)
    t2 = get_time();
    enumExhaustive_dispatch(game, pockets, board, dead, npockets, nboard, 0, &res2);
    t2 = get_time() - t2;
    
    printf("Ancienne version : %.3fs (%d boards)\n", t1, res1.nsamples);
    printf("EEDC optimisée   : %.3fs (%d boards)\n", t2, res2.nsamples);
    printf("Speedup : %.2fx\n", t1/t2);
    printf("Résultats identiques : %s\n", 
           (res1.nsamples == res2.nsamples && fabs(res1.ev[0] - res2.ev[0]) < 1e-6) ? "OUI" : "NON");
}

int main(void) {
    printf("\n=== Benchmark EEDC vs Classic (Version Rapide) ===\n");
    
    // Tests avec différents nombres de cartes sur le board pour réduire les combinaisons
    bench_game_with_board("Hold'em (flop)", game_holdem, 2, 3);
    bench_game_with_board("Hold'em (turn)", game_holdem, 2, 4);
    bench_game_with_board("Hold'em (river)", game_holdem, 2, 5);
    
    bench_game_with_board("Omaha (flop)", game_omaha, 2, 3);
    bench_game_with_board("Omaha (turn)", game_omaha, 2, 4);
    
    return 0;
}
