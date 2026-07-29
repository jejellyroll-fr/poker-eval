/*
 * bench_7c_hilo_singlepass.c - Compare 7c Hi/Lo single-pass vs naive split
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/combo_7to5_hilo.h>

static mask_t random_7card_mask(unsigned seed) {
    mask_t m = 0; srand(seed);
    while (mask_popcount(m) < 7) {
        int c = rand() % 52;
        if (!mask_is_set(m, c)) m = mask_set(m, c);
    }
    return m;
}

/* Generate 7-card hand biased towards low-friendly (ranks <= 8, distinct) */
static mask_t random_7card_low_friendly(unsigned seed) {
    srand(seed);
    int ranks_pool[8] = {12,0,1,2,3,4,5,6}; /* A,2,3,4,5,6,7,8 (A=12, 2=0) */
    /* Fisher-Yates shuffle */
    for (int i = 7; i > 0; --i) { int j = rand() % (i+1); int t = ranks_pool[i]; ranks_pool[i] = ranks_pool[j]; ranks_pool[j] = t; }
    mask_t m = 0;
    /* pick 7 distinct ranks from pool, random suits */
    for (int i = 0; i < 7; ++i) {
        int r = ranks_pool[i]; int s = rand() % 4; m = mask_set(m, r + 13*s);
    }
    return m;
}

int main(int argc, char** argv) {
    int samples = 10000; unsigned seed = 12345u; int disable_simd = 1; const char* mode = "low-friendly"; /* low-friendly|random */
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--samples") && i+1<argc) samples = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--seed") && i+1<argc) seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i],"--enable-simd")) disable_simd = 0;
        else if (!strcmp(argv[i],"--mode") && i+1<argc) mode = argv[++i];
    }

    EvalConfig cfg = eval_config_holdem(); cfg.enable_simd = !disable_simd;
    EvalContext* ctx = eval_context_create(&cfg);
    if (!ctx) { fprintf(stderr, "ctx create failed\n"); return 1; }

    mask_t* hands = (mask_t*)malloc(sizeof(mask_t)*samples);
    for (int i=0;i<samples;i++) {
        unsigned s = seed + i*17u; hands[i] = (strcmp(mode,"low-friendly")==0) ? random_7card_low_friendly(s) : random_7card_mask(s);
    }

    /* Single-pass Hi/Lo */
    clock_t s1 = clock();
    long long checksum1 = 0;
    for (int i=0;i<samples;i++) {
        eval_t hi=0, lo=0; bool lq=false;
        pe_eval_7c_hilo(ctx, hands[i], &hi, &lo, &lq);
        checksum1 ^= hi; if (lq) checksum1 ^= (lo<<1);
    }
    clock_t e1 = clock();

    /* Naive split: enumerate 21 combos deux fois (High séparé de Low) */
    clock_t s2 = clock();
    long long checksum2 = 0;
    for (int i=0;i<samples;i++) {
        /* High via pe_eval_7c */
        eval_t hi = pe_eval_7c(ctx, hands[i]); checksum2 ^= hi;
        /* Low via enumeration dédiée (générateur Hi/Lo) ignorante du High de la première passe */
        combo_7to5_hilo_t* gen = combo_7to5_hilo_create(hands[i], false);
        if (gen) {
            mask_t five; hilo_result_t res; eval_t best_lo = 0; int any_low = 0;
            while (combo_7to5_hilo_next(gen, &five, &res)) {
                if (res.has_low) { if (!any_low || res.lo_value < best_lo) { best_lo = res.lo_value; any_low = 1; } }
            }
            combo_7to5_hilo_destroy(gen);
            if (any_low) checksum2 ^= (best_lo<<1);
        }
    }
    clock_t e2 = clock();

    double t1 = (double)(e1 - s1) / CLOCKS_PER_SEC;
    double t2 = (double)(e2 - s2) / CLOCKS_PER_SEC;
    printf("7c Hi/Lo single-pass vs naive (samples=%d, mode=%s, simd=%s)\n", samples, mode, disable_simd?"OFF":"ON");
    printf("Single-pass: %.3f s\n", t1);
    printf("Naive split: %.3f s\n", t2);
    printf("Speedup: %.2fx\n", (t2>0? t2/t1 : 0.0));
    printf("Checksums: %lld vs %lld\n", checksum1, checksum2);

    free(hands); eval_context_destroy(ctx);
    return 0;
}
