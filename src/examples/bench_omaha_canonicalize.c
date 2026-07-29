/*
 * bench_omaha_canonicalize.c - Compare exhaustive classic vs EEDC (canonicalized) for Omaha
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/equity/enumerate_eedc.h>

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void mask_from_strs(StdDeck_CardMask* out, const char* a[], int n) {
    StdDeck_CardMask_RESET(*out);
    for (int i=0;i<n;i++) {
        if (!a[i]) continue;
        char buf[8];
        strncpy(buf, a[i], sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';
        int c=-1;
        if (StdDeck_stringToCard(buf, &c) == 0 && c>=0)
            StdDeck_CardMask_SET(*out, c);
    }
}

int main(int argc, char** argv) {
    /* Two fixed 4-card pockets */
    const char* p1s[] = {"As","Ks","Qd","Jh"};
    const char* p2s[] = {"Ah","Kh","Qh","Jd"};
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    for (int i=0;i<ENUM_MAXPLAYERS;i++) StdDeck_CardMask_RESET(pockets[i]);
    mask_from_strs(&pockets[0], p1s, 4);
    mask_from_strs(&pockets[1], p2s, 4);

    StdDeck_CardMask board, dead; StdDeck_CardMask_RESET(board); StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    enum_result_t R1 = {0}, R2 = {0};
    double t0, t1, t2;

    /* Classic exhaustive */
    t0 = now_sec();
    enumExhaustive(game_omaha, pockets, board, dead, 2, 0, 0, &R1);
    t1 = now_sec();

    /* Dispatch path (routes to EEDC + canonicalization if enabled) */
    enumExhaustive_dispatch(game_omaha, pockets, board, dead, 2, 0, 0, &R2);
    t2 = now_sec();

    printf("Omaha canonicalization bench (nboard=0)\n");
    printf("Classic:  nsamples=%u, time=%.3fs\n", R1.nsamples, (t1 - t0));
    printf("EEDC   :  nsamples=%u, time=%.3fs\n", R2.nsamples, (t2 - t1));
    if ((t2-t1)>0.0) printf("Speedup: %.2fx\n", (t1 - t0)/(t2 - t1));
    return 0;
}
