/*
 * bench_7c_simd_micro.c - Micro-benchmark for 7-card SIMD path vs scalar
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/core/eval_context.h>

static mask_t random_7card_mask(unsigned seed) {
    mask_t m = 0; srand(seed);
    while (mask_popcount(m) < 7) {
        int c = rand() % 52;
        if (!mask_is_set(m, c)) m = mask_set(m, c);
    }
    return m;
}

static mask_t random_7card_sf_heavy(unsigned seed) {
    srand(seed);
    int suit = rand() % 4;
    /* pick 5 ranks for flush */
    int ranks[13]; for (int i=0;i<13;i++) ranks[i]=i;
    for (int i=12;i>0;i--) { int j=rand()%(i+1); int tmp=ranks[i]; ranks[i]=ranks[j]; ranks[j]=tmp; }
    mask_t m = 0;
    for (int i=0;i<5;i++) m = mask_set(m, ranks[i] + 13*suit);
    while (mask_popcount(m) < 7) {
        int c = rand()%52; if (!mask_is_set(m,c)) m = mask_set(m,c);
    }
    return m;
}

static double compute_nfs_ratio(mask_t* hands, int samples);

static mask_t random_7card_nfs_heavy(unsigned seed) {
    /* rejection sampling by NFS ratio */
    srand(seed);
    for (int tries=0; tries<10000; ++tries) {
        mask_t m = random_7card_mask(seed + tries*31u);
        mask_t arr[1] = {m};
        double r = compute_nfs_ratio(arr, 1);
        if (r >= 0.8) return m;
    }
    return random_7card_mask(seed+777);
}

static inline void five_suit_masks(mask_t five, uint32_t suits[4]) {
    suits[0] = (uint32_t)((five >> 0)  & 0x1FFFu);
    suits[1] = (uint32_t)((five >> 13) & 0x1FFFu);
    suits[2] = (uint32_t)((five >> 26) & 0x1FFFu);
    suits[3] = (uint32_t)((five >> 39) & 0x1FFFu);
}

static inline int is_flush_5(uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3) {
    int c0 = __builtin_popcount(s0);
    int c1 = __builtin_popcount(s1);
    int c2 = __builtin_popcount(s2);
    int c3 = __builtin_popcount(s3);
    return c0 == 5 || c1 == 5 || c2 == 5 || c3 == 5;
}

static inline int is_straight_from_ranks(uint32_t r) {
    if ((r & (1u << 12)) && ((r & 0x0Fu) == 0x0Fu)) return 1;
    for (int hi = 12; hi >= 4; --hi) {
        uint32_t mask = 0x1Fu << (hi - 4);
        if ((r & mask) == mask) return 1;
    }
    return 0;
}

static double compute_nfs_ratio(mask_t* hands, int samples) {
    long long total = 0;
    long long nfs = 0;
    for (int i = 0; i < samples; ++i) {
        /* extract 7 cards */
        int cards[7]; int ci = 0; mask_t m = hands[i];
        for (int c = __builtin_ctzll(m); m; m &= (m - 1), c = __builtin_ctzll(m)) {
            cards[ci++] = c;
            if (ci == 7) break;
        }
        /* iterate all 21 combinations */
        for (int a = 0; a < 3; ++a)
        for (int b = a+1; b < 4; ++b)
        for (int c = b+1; c < 5; ++c)
        for (int d = c+1; d < 6; ++d)
        for (int e = d+1; e < 7; ++e) {
            mask_t five = 0;
            five |= (1ULL << cards[a]);
            five |= (1ULL << cards[b]);
            five |= (1ULL << cards[c]);
            five |= (1ULL << cards[d]);
            five |= (1ULL << cards[e]);
            uint32_t suits[4];
            five_suit_masks(five, suits);
            uint32_t ranks_any = suits[0] | suits[1] | suits[2] | suits[3];
            int flush = is_flush_5(suits[0], suits[1], suits[2], suits[3]);
            int straight = is_straight_from_ranks(ranks_any);
            total++;
            if (!flush && !straight) nfs++;
        }
    }
    return total ? ((double)nfs / (double)total) : 0.0;
}

static double bench_pass(int samples, int iters, int enable_simd, unsigned seed, const char* mode) {
    EvalConfig cfg = eval_config_holdem();
    cfg.enable_simd = enable_simd;
    EvalContext* ctx = eval_context_create(&cfg);
    if (!ctx) { fprintf(stderr, "ctx create failed\n"); return 0.0; }

    mask_t* hands = (mask_t*)malloc(sizeof(mask_t)*samples);
    for (int i = 0; i < samples; ++i) {
        unsigned s = seed + i*17u;
        if (mode && strcmp(mode, "sf-heavy")==0) hands[i] = random_7card_sf_heavy(s);
        else if (mode && strcmp(mode, "nfs-heavy")==0) hands[i] = random_7card_nfs_heavy(s);
        else hands[i] = random_7card_mask(s);
    }

    clock_t start = clock();
    eval_t acc = 0;
    for (int r = 0; r < iters; ++r) {
        for (int i = 0; i < samples; ++i) {
            acc ^= pe_eval_7c(ctx, hands[i]);
        }
    }
    clock_t end = clock();
    double secs = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Result checksum: %u\n", (unsigned)acc);
    free(hands);
    eval_context_destroy(ctx);
    return secs;
}

int main(int argc, char** argv) {
    int samples = 2000;
    int iters = 20;
    unsigned seed = 12345u;
    const char* csv_path = NULL;
    const char* mode = "random"; /* random | nfs-heavy | sf-heavy */

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--samples") == 0 && i+1 < argc) { samples = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--iters") == 0 && i+1 < argc) { iters = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--seed") == 0 && i+1 < argc) { seed = (unsigned)strtoul(argv[++i], NULL, 10); }
        else if (strcmp(argv[i], "--csv") == 0 && i+1 < argc) { csv_path = argv[++i]; }
        else if (strcmp(argv[i], "--mode") == 0 && i+1 < argc) { mode = argv[++i]; }
    }

    printf("7c SIMD micro-benchmark (samples=%d, iters=%d)\n", samples, iters);
    /* Prepare sample set once to compute NFS ratio */
    mask_t* hands = (mask_t*)malloc(sizeof(mask_t)*samples);
    for (int i = 0; i < samples; ++i) {
        unsigned s = seed + i*17u;
        if (strcmp(mode, "sf-heavy")==0) hands[i] = random_7card_sf_heavy(s);
        else if (strcmp(mode, "nfs-heavy")==0) hands[i] = random_7card_nfs_heavy(s);
        else hands[i] = random_7card_mask(s);
    }
    double nfs_ratio = compute_nfs_ratio(hands, samples);
    free(hands);

    double t_simd = bench_pass(samples, iters, 1, seed, mode);
    double t_scalar = bench_pass(samples, iters, 0, seed, mode);

    long long evals = (long long)samples * iters;
    printf("SIMD:   %.3f s  (%.0f eval/s)\n", t_simd, (double)evals / t_simd);
    printf("Scalar: %.3f s  (%.0f eval/s)\n", t_scalar, (double)evals / t_scalar);
    if (t_simd > 0 && t_scalar > 0)
        printf("Speedup: %.2fx\n", t_scalar / t_simd);

    if (csv_path) {
        FILE* f = fopen(csv_path, "a");
        if (f) {
            double sp = (t_simd > 0 && t_scalar > 0) ? (t_scalar / t_simd) : 0.0;
            fprintf(f, "SIMD,%d,%d,%lld,%.6f,%.0f,%.6f,%.3f,%s\n", samples, iters, evals, t_simd, (double)evals / t_simd, nfs_ratio, sp, mode);
            fprintf(f, "SCALAR,%d,%d,%lld,%.6f,%.0f,%.6f,,%s\n", samples, iters, evals, t_scalar, (double)evals / t_scalar, nfs_ratio, mode);
            fclose(f);
        }
    }
    return 0;
}
