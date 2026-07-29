/*
 * benchmark_canonicalize_boards.c - Estimates number of unique 5-card board classes by sampling
 * Outputs CSV with columns: samples, unique_classes, time_sec
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/canonicalize.h>

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Simple open addressing hash set for 64-bit hashes */
typedef struct { uint64_t h; int used; } slot_t;

static int hashset_insert(slot_t* T, size_t cap, uint64_t h) {
    size_t i = (size_t)(h % cap);
    for (size_t k = 0; k < cap; k++) {
        size_t idx = (i + k) % cap;
        if (!T[idx].used) { T[idx].used = 1; T[idx].h = h; return 1; }
        if (T[idx].h == h) return 0; /* already present */
    }
    return 0; /* full */
}

static void random_board5(StdDeck_CardMask* out) {
    StdDeck_CardMask_RESET(*out);
    StdDeck_CardMask used = *out;
    for (int j = 0; j < 5; j++) {
        int c;
        do { c = rand() % StdDeck_N_CARDS; } while (StdDeck_CardMask_CARD_IS_SET(used, c));
        StdDeck_CardMask_SET(*out, c);
        StdDeck_CardMask_SET(used, c);
    }
}

int main(void) {
    int samples_list[] = { 10000, 50000, 100000 };
    int ns = (int)(sizeof(samples_list)/sizeof(samples_list[0]));
    printf("samples,unique_classes,time_sec\n");
    for (int k = 0; k < ns; k++) {
        int samples = samples_list[k];
        size_t cap = (size_t)samples * 2 + 1; /* load ~0.5 */
        slot_t* T = (slot_t*)calloc(cap, sizeof(slot_t));
        int unique = 0;
        double t0 = now_sec();
        for (int i = 0; i < samples; i++) {
            StdDeck_CardMask b; random_board5(&b);
            uint64_t h = pe_board5_canonical_hash64(b);
            if (h && hashset_insert(T, cap, h)) unique++;
        }
        double t1 = now_sec();
        printf("%d,%d,%.6f\n", samples, unique, (t1 - t0));
        free(T);
    }
    return 0;
}

