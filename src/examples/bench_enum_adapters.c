/*
 * bench_enum_adapters.c - Micro-bench: legacy DECK_ENUMERATE_* vs modern ENUM_STDDECK_ENUMERATE_*
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumeration_adapters.h>

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void make_dead_from_pockets(StdDeck_CardMask* dead) {
    StdDeck_CardMask p1, p2; StdDeck_CardMask_RESET(p1); StdDeck_CardMask_RESET(p2);
    int c;
    char buf[8];
    strcpy(buf, "As"); StdDeck_stringToCard(buf, &c); StdDeck_CardMask_SET(p1, c);
    strcpy(buf, "Ah"); StdDeck_stringToCard(buf, &c); StdDeck_CardMask_SET(p1, c);
    strcpy(buf, "Kd"); StdDeck_stringToCard(buf, &c); StdDeck_CardMask_SET(p2, c);
    strcpy(buf, "Kh"); StdDeck_stringToCard(buf, &c); StdDeck_CardMask_SET(p2, c);
    StdDeck_CardMask_RESET(*dead);
    StdDeck_CardMask_OR(*dead, *dead, p1);
    StdDeck_CardMask_OR(*dead, *dead, p2);
}

static unsigned long long bench_legacy(int k, StdDeck_CardMask dead, double* out_t) {
    double t0 = now_sec();
    unsigned long long cnt = 0ULL;
    StdDeck_CardMask shared;
    (void)shared; /* Used by macro expansion */
    switch (k) {
        case 1:
            DECK_ENUMERATE_1_CARDS_D(StdDeck, shared, dead, { cnt++; });
            break;
        case 2:
            DECK_ENUMERATE_2_CARDS_D(StdDeck, shared, dead, { cnt++; });
            break;
        case 5:
            DECK_ENUMERATE_5_CARDS_D(StdDeck, shared, dead, { cnt++; });
            break;
        default:
            break;
    }
    double t1 = now_sec();
    if (out_t) *out_t = t1 - t0;
    return cnt;
}

static unsigned long long bench_modern(int k, StdDeck_CardMask dead, double* out_t) {
    double t0 = now_sec();
    unsigned long long cnt = 0ULL;
    StdDeck_CardMask shared;
    ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(k, dead, shared, { cnt++; });
    double t1 = now_sec();
    if (out_t) *out_t = t1 - t0;
    return cnt;
}

int main(int argc, char** argv) {
    int k = 5;
    int verify_only = 0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i], "--k") && i+1<argc) { k = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--verify-only")) { verify_only = 1; }
    }
    if (!(k==1||k==2||k==5)) k = 5;

    StdDeck_CardMask dead; make_dead_from_pockets(&dead);

    double t_legacy=0.0, t_modern=0.0;
    unsigned long long c_legacy = bench_legacy(k, dead, &t_legacy);
    unsigned long long c_modern = bench_modern(k, dead, &t_modern);
    int ok = (c_legacy == c_modern);

    if (verify_only) {
        printf("verify k=%d: legacy=%llu modern=%llu => %s\n", k,
               (unsigned long long)c_legacy, (unsigned long long)c_modern,
               ok?"OK":"MISMATCH");
        return ok ? 0 : 1;
    }

    printf("Enumeration adapters micro-bench (k=%d)\n", k);
    printf("  Dead = pockets(As Ah, Kd Kh)\n");
    printf("  Legacy count: %llu, time: %.6fs\n", c_legacy, t_legacy);
    printf("  Modern  count: %llu, time: %.6fs\n", c_modern, t_modern);
    if (t_modern>0.0) printf("  Speedup (legacy/modern): %.2fx\n", t_legacy/t_modern);
    printf("  Equal counts: %s\n", ok?"YES":"NO");
    return ok?0:2;
}

