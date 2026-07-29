/*
 * bench_holdem8_simd.c - Measure Hold'em Hi/Lo batched SIMD vs scalar
 * Usage: bench_holdem8_simd [players] [niter]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/batched_montecarlo.h>

static long long now_us(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

static void random_unique_cards(StdDeck_CardMask* out, int k, StdDeck_CardMask* used) {
    StdDeck_CardMask_RESET(*out);
    for (int i=0;i<k;i++) {
        int c;
        do { c = rand() % StdDeck_N_CARDS; } while (StdDeck_CardMask_CARD_IS_SET(*used, c));
        StdDeck_CardMask_SET(*out, c);
        StdDeck_CardMask_SET(*used, c);
    }
}

int main(int argc, char** argv) {
    int players = 4;
    int niter = 200000;
    if (argc > 1) players = atoi(argv[1]);
    if (argc > 2) niter = atoi(argv[2]);
    if (players < 2) players = 2;
    if (players > ENUM_MAXPLAYERS) players = ENUM_MAXPLAYERS;

    srand((unsigned)time(NULL));

    /* Prepare pockets + random flop */
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    StdDeck_CardMask board, dead; StdDeck_CardMask_RESET(board); StdDeck_CardMask_RESET(dead);
    for (int p=0;p<players;p++) random_unique_cards(&pockets[p], 2, &dead);
    StdDeck_CardMask flop; random_unique_cards(&flop, 3, &dead); StdDeck_CardMask_OR(board, board, flop);

    enum_result_t E = {0};

    /* SIMD OFF */
    setenv("PE_DISABLE_SIMD", "1", 1);
    long long t0 = now_us();
    enumSampleBatched(game_holdem8, pockets, board, dead, players, 3, niter, 0, &E);
    long long t1 = now_us();
    double off_ms = (double)(t1 - t0) / 1000.0;

    /* SIMD ON */
    setenv("PE_DISABLE_SIMD", "0", 1);
    memset(&E, 0, sizeof(E));
    t0 = now_us();
    enumSampleBatched(game_holdem8, pockets, board, dead, players, 3, niter, 0, &E);
    t1 = now_us();
    double on_ms = (double)(t1 - t0) / 1000.0;

    printf("Hold'em8 N=%d, niter=%d\n", players, niter);
    printf("SIMD OFF: %.2f ms\n", off_ms);
    printf("SIMD ON : %.2f ms\n", on_ms);
    if (on_ms > 0.0) printf("Speedup: %.2fx\n", off_ms / on_ms);

    return 0;
}

