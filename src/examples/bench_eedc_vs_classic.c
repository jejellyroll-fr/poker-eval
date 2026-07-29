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

// Benchmark pour un jeu donné
static void bench_game(const char* name, enum_game_t game, int npockets, int nboard) {
    printf("\n=== %s ===\n", name);
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Génère des mains simples valides pour chaque jeu
    for (int i = 0; i < npockets; ++i) {
        StdDeck_CardMask_RESET(pockets[i]);
        
        // Hold'em et variantes : 2 cartes
        if (game == game_holdem || game == game_holdem8 || game == game_sdholdem) {
            StdDeck_CardMask_SET(pockets[i], i*13);     // As de couleur i
            StdDeck_CardMask_SET(pockets[i], i*13+12);  // Roi de couleur i
        }
        // Omaha et variantes : 4 cartes
        else if (game == game_omaha || game == game_omaha8) {
            for (int k = 0; k < 4; ++k) {
                StdDeck_CardMask_SET(pockets[i], i*13 + k);
            }
        }
        // Omaha 5 cartes
        else if (game == game_omaha5 || game == game_omaha85) {
            for (int k = 0; k < 5; ++k) {
                StdDeck_CardMask_SET(pockets[i], i*13 + k);
            }
        }
        // Omaha 6 cartes
        else if (game == game_omaha6) {
            for (int k = 0; k < 6; ++k) {
                StdDeck_CardMask_SET(pockets[i], i*13 + k);
            }
        }
        // 7-card stud : 6 cartes initiales (pour benchmark rapide)
        else if (game == game_7stud || game == game_7stud8 || game == game_7studnsq || game == game_razz) {
            for (int k = 0; k < 6; ++k) {
                StdDeck_CardMask_SET(pockets[i], i*13 + k);
            }
        }
        // 5-card draw : 4 cartes initiales (pour benchmark rapide)
        else if (game == game_5draw || game == game_5draw8 || game == game_5drawnsq || game == game_lowball || game == game_lowball27) {
            for (int k = 0; k < 4; ++k) {
                StdDeck_CardMask_SET(pockets[i], i*13 + k);
            }
        }
        // Double flop holdem : 2 cartes
        else if (game == game_doubleflop_holdem) {
            StdDeck_CardMask_SET(pockets[i], i*13);
            StdDeck_CardMask_SET(pockets[i], i*13+1);
        }
        
        // Ajouter les cartes utilisées aux cartes mortes
        StdDeck_CardMask_OR(dead, dead, pockets[i]);
    }
    
    // Calculer le nombre de combinaisons pour éviter les benchmarks trop longs
    int cards_remaining = 52 - StdDeck_numCards(dead);
    int cards_needed = 0;
    
    if (game == game_7stud || game == game_7stud8 || game == game_7studnsq || game == game_razz) {
        cards_needed = (7 - 6) * npockets;  // 1 carte par joueur
    } else if (game == game_5draw || game == game_5draw8 || game == game_5drawnsq || game == game_lowball || game == game_lowball27) {
        cards_needed = (5 - 4) * npockets;  // 1 carte par joueur
    } else if (game == game_holdem || game == game_holdem8 || game == game_sdholdem) {
        cards_needed = 5 - nboard;  // Cartes du board
    } else if (game == game_omaha || game == game_omaha8 || game == game_omaha5 || game == game_omaha85 || game == game_omaha6) {
        cards_needed = 5 - nboard;  // Cartes du board
    } else if (game == game_doubleflop_holdem) {
        cards_needed = 10 - nboard;  // 2 boards de 5 cartes
    }
    
    // Calculer approximativement le nombre de combinaisons
    double combinations = 1;
    for (int i = 0; i < cards_needed && i < cards_remaining; i++) {
        combinations *= (cards_remaining - i);
        combinations /= (i + 1);
    }
    
    printf("Cartes restantes: %d, cartes nécessaires: %d, combinaisons: %.0f\n", 
           cards_remaining, cards_needed, combinations);
    
    // Skip si trop de combinaisons
    if (combinations > 5000000) {
        printf("Trop de combinaisons, test ignoré\n");
        return;
    }
    enum_result_t res1, res2;
    double t1, t2;
    t1 = get_time();
    enumExhaustive(game, pockets, board, dead, npockets, nboard, 0, &res1);
    t1 = get_time() - t1;
    t2 = get_time();
    enumExhaustive_dispatch(game, pockets, board, dead, npockets, nboard, 0, &res2);
    t2 = get_time() - t2;
    printf("Ancienne version : %.3fs (%d samples)\n", t1, res1.nsamples);
    printf("EEDC optimisée   : %.3fs (%d samples)\n", t2, res2.nsamples);
    printf("Speedup : %.2fx\n", t1/t2);
    
    // Pour Stud/Draw, les samples peuvent différer (permutations vs combinaisons)
    // On vérifie que les EV normalisées sont identiques
    double ev1_norm = res1.nsamples > 0 ? res1.ev[0] / res1.nsamples : 0;
    double ev2_norm = res2.nsamples > 0 ? res2.ev[0] / res2.nsamples : 0;
    
    printf("EV normalisées : %.4f vs %.4f\n", ev1_norm, ev2_norm);
    printf("Résultats cohérents : %s\n", fabs(ev1_norm - ev2_norm) < 1e-6 ? "OUI" : "NON");
}

int main(void) {
    printf("\n=== Benchmark EEDC vs Classic ===\n");
    bench_game("Hold'em", game_holdem, 2, 0);
    bench_game("Omaha", game_omaha, 2, 0);
    bench_game("Omaha8", game_omaha8, 2, 0);
    bench_game("7stud", game_7stud, 2, 0);
    bench_game("5draw", game_5draw, 2, 0);
    bench_game("Lowball27", game_lowball27, 2, 0);
    bench_game("Doubleflop Hold'em", game_doubleflop_holdem, 2, 0);
    return 0;
}
