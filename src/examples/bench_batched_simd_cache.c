/*
 * bench_batched_simd_cache.c - Micro-bench: SIMD vs scalar, cache ON/OFF, batched Hold'em
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/sampling_policies.h>
#include <poker_eval/core/eval_cache.h>

static long long now_us(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

static void random_unique_cards(StdDeck_CardMask* out, int k, StdDeck_CardMask* used) {
    StdDeck_CardMask_RESET(*out);
    for (int i=0;i<k;i++) {
        int c;
        int guard=0;
        do { c = rand() % StdDeck_N_CARDS; guard++; } while (StdDeck_CardMask_CARD_IS_SET(*used, c) && guard<1000);
        StdDeck_CardMask_SET(*out, c);
        StdDeck_CardMask_SET(*used, c);
    }
}

static double run_once(int players, int niter, int simd_on, int cache_on) {
    /* pockets + flop random */
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    StdDeck_CardMask board, dead; StdDeck_CardMask_RESET(board); StdDeck_CardMask_RESET(dead);
    for (int p=0;p<players;p++) random_unique_cards(&pockets[p], 2, &dead);
    StdDeck_CardMask flop; random_unique_cards(&flop, 3, &dead); StdDeck_CardMask_OR(board, board, flop);

    pe_sampling_set_policy(PE_SAMPLING_MC_UNIFORM);

    /* SIMD toggle via env */
    setenv("PE_DISABLE_SIMD", simd_on ? "0" : "1", 1);

    if (cache_on) {
        (void)eval_cache_init_global(EVAL_CACHE_DEFAULT_SIZE);
    } else {
        eval_cache_destroy_global();
    }

    enum_result_t E = {0};
    long long t0 = now_us();
    enumSampleBatched(game_holdem, pockets, board, dead, players, 3, niter, 0, &E);
    long long t1 = now_us();
    return (double)(t1 - t0) / 1000.0; /* ms */
}

int main(int argc, char** argv) {
    int players = 2;
    int niter = 500000;
    if (argc > 1) players = atoi(argv[1]);
    if (argc > 2) niter = atoi(argv[2]);
    if (players < 2) players = 2;
    if (players > ENUM_MAXPLAYERS) players = ENUM_MAXPLAYERS;
    srand(12345);

    /* CSV header */
    printf("players,niter,simd,cache,time_ms\n");
    /* OFF/OFF */
    double t00 = run_once(players, niter, 0, 0);
    printf("%d,%d,%s,%s,%.3f\n", players, niter, "OFF", "OFF", t00);
    /* OFF/ON */
    double t01 = run_once(players, niter, 0, 1);
    printf("%d,%d,%s,%s,%.3f\n", players, niter, "OFF", "ON", t01);
    /* ON/OFF */
    double t10 = run_once(players, niter, 1, 0);
    printf("%d,%d,%s,%s,%.3f\n", players, niter, "ON", "OFF", t10);
    /* ON/ON */
    double t11 = run_once(players, niter, 1, 1);
    printf("%d,%d,%s,%s,%.3f\n", players, niter, "ON", "ON", t11);

    return 0;
}

