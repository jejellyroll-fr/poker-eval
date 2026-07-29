/*
 * bench_holdem_canonicalize_7.c - Count unique 7-card equivalence classes (pocket+board) in Hold'em
 * Outputs CSV: stage,samples,unique_classes,time_sec
 * stage ∈ {preflop,flop,turn,river}
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/canonicalize.h>

typedef enum { STAGE_PREFLOP=0, STAGE_FLOP=3, STAGE_TURN=4, STAGE_RIVER=5 } stage_t;

static const char* stage_name(stage_t s) {
    switch (s) {
        case STAGE_PREFLOP: return "preflop";
        case STAGE_FLOP: return "flop";
        case STAGE_TURN: return "turn";
        case STAGE_RIVER: return "river";
        default: return "flop";
    }
}

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Simple open addressing set */
typedef struct { unsigned long long h; int used; } slot_t;

static int insert_hash(slot_t* T, size_t cap, unsigned long long h) {
    size_t i0 = (size_t)(h % cap);
    for (size_t k = 0; k < cap; ++k) {
        size_t idx = (i0 + k) % cap;
        if (!T[idx].used) { T[idx].used = 1; T[idx].h = h; return 1; }
        if (T[idx].h == h) return 0;
    }
    return 0;
}

static void mask_from_strs(StdDeck_CardMask* out, const char* a[], int n) {
    StdDeck_CardMask_RESET(*out);
    for (int i=0;i<n;i++) {
        if (!a[i]) continue;
        char buf[8];
        strncpy(buf, a[i], sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';
        int c=-1;
        if (StdDeck_stringToCard(buf, &c)==0 && c>=0)
            StdDeck_CardMask_SET(*out, c);
    }
}

int main(int argc, char** argv) {
    stage_t stage = STAGE_FLOP;
    if (argc > 1) {
        if (strcmp(argv[1], "preflop")==0) stage = STAGE_PREFLOP;
        else if (strcmp(argv[1], "flop")==0) stage = STAGE_FLOP;
        else if (strcmp(argv[1], "turn")==0) stage = STAGE_TURN;
        else if (strcmp(argv[1], "river")==0) stage = STAGE_RIVER;
    }

    /* Fixed pockets for determinism */
    const char* p1s[] = {"As","Ah"};
    const char* p2s[] = {"Kd","Kh"};
    StdDeck_CardMask p1, p2, board, dead;
    mask_from_strs(&p1, p1s, 2);
    mask_from_strs(&p2, p2s, 2);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, p1);
    StdDeck_CardMask_OR(dead, dead, p2);

    int nboard_known = (int)stage;
    /* pick a deterministic board for flop/turn/river known cards */
    int chosen = 0;
    for (int c = 0; c < StdDeck_N_CARDS && chosen < nboard_known; ++c) {
        if (!StdDeck_CardMask_CARD_IS_SET(dead, c)) {
            StdDeck_CardMask_SET(board, c);
            StdDeck_CardMask_SET(dead, c);
            ++chosen;
        }
    }

    int n_needed = 5 - nboard_known;
    /* Build remaining deck indices */
    int deck[StdDeck_N_CARDS];
    int ndeck = 0;
    for (int c = 0; c < StdDeck_N_CARDS; ++c) if (!StdDeck_CardMask_CARD_IS_SET(dead, c)) deck[ndeck++] = c;

    /* Hash set capacity heuristic */
    size_t cap = (size_t) ( (n_needed==0 ? 1024 : (1u<<22)) );
    slot_t* T = (slot_t*)calloc(cap, sizeof(slot_t));
    if (!T) { fprintf(stderr, "oom\n"); return 1; }
    int unique = 0;

    /* Iterate combinations of missing cards */
    double t0 = now_sec();
    if (n_needed <= 0) {
        StdDeck_CardMask cards7; StdDeck_CardMask_OR(cards7, p1, board);
        unsigned long long h = pe_hand7_canonical_hash64(cards7);
        if (h) unique += insert_hash(T, cap, h);
    } else {
        int idx[5] = {0,1,2,3,4};
        for (int i = 0; i < n_needed; ++i) idx[i] = i;
        while (1) {
            StdDeck_CardMask missing; StdDeck_CardMask_RESET(missing);
            for (int i = 0; i < n_needed; ++i) StdDeck_CardMask_SET(missing, deck[idx[i]]);
            StdDeck_CardMask fullBoard; StdDeck_CardMask_OR(fullBoard, board, missing);
            StdDeck_CardMask cards7; StdDeck_CardMask_OR(cards7, p1, fullBoard);
            unsigned long long h = pe_hand7_canonical_hash64(cards7);
            if (h) unique += insert_hash(T, cap, h);
            /* next combination */
            int pos = n_needed - 1;
            while (pos >= 0 && idx[pos] == ndeck - n_needed + pos) pos--;
            if (pos < 0) break;
            idx[pos]++;
            for (int j = pos + 1; j < n_needed; ++j) idx[j] = idx[j-1] + 1;
        }
    }
    double t1 = now_sec();

    printf("stage,samples,unique_classes,time_sec\n");
    printf("%s,%d,%d,%.6f\n", stage_name(stage), 0, unique, (t1 - t0));
    free(T);
    return 0;
}

