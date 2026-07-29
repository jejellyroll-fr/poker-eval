/*
 * bench_7c_validate.c - Validation of pe_eval_7c (SIMD vs Scalar)
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

static mask_t random_7card_sf_heavy(unsigned seed) {
    srand(seed);
    int suit = rand() % 4;
    int ranks[13]; for (int i=0;i<13;i++) ranks[i]=i;
    for (int i=12;i>0;i--) { int j=rand()%(i+1); int tmp=ranks[i]; ranks[i]=ranks[j]; ranks[j]=tmp; }
    mask_t m = 0;
    for (int i=0;i<5;i++) m = mask_set(m, ranks[i] + 13*suit);
    while (mask_popcount(m) < 7) {
        int c = rand()%52; if (!mask_is_set(m,c)) m = mask_set(m,c);
    }
    return m;
}

static double compute_nfs_ratio(mask_t hand) {
    /* compute ratio of 21 combos that are NFS */
    int cards[7], ci = 0; mask_t m = hand;
    for (int c = __builtin_ctzll(m); m; m &= (m - 1), c = __builtin_ctzll(m)) { cards[ci++] = c; if (ci==7) break; }
    long long nfs=0; long long total=0;
    for (int a=0;a<3;a++) for (int b=a+1;b<4;b++) for (int c=b+1;c<5;c++) for (int d=c+1;d<6;d++) for (int e=d+1;e<7;e++) {
        mask_t five = 0;
        five |= (1ULL<<cards[a])|(1ULL<<cards[b])|(1ULL<<cards[c])|(1ULL<<cards[d])|(1ULL<<cards[e]);
        uint32_t suits[4]; five_suit_masks(five, suits);
        uint32_t ranks_any = suits[0] | suits[1] | suits[2] | suits[3];
        int flush = is_flush_5(suits[0],suits[1],suits[2],suits[3]);
        int straight = is_straight_from_ranks(ranks_any);
        total++; if (!flush && !straight) nfs++;
    }
    return total ? (double)nfs/(double)total : 0.0;
}

int main(int argc, char** argv) {
    int samples = 10000;
    unsigned seed = 12345u;
    const char* mode = "random"; /* random|nfs-heavy|sf-heavy */
    const char* csv = NULL;

    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--samples") && i+1<argc) samples = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--seed") && i+1<argc) seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i],"--mode") && i+1<argc) mode = argv[++i];
        else if (!strcmp(argv[i],"--csv") && i+1<argc) csv = argv[++i];
    }

    EvalConfig c_simd = eval_config_holdem(); c_simd.enable_simd = 1;
    EvalConfig c_scal = eval_config_holdem(); c_scal.enable_simd = 0;
    EvalContext* ctx_simd = eval_context_create(&c_simd);
    EvalContext* ctx_scal = eval_context_create(&c_scal);
    if (!ctx_simd||!ctx_scal) { fprintf(stderr, "ctx create failed\n"); return 1; }

    FILE* fcsv = NULL;
    if (csv) {
        fcsv = fopen(csv, "w");
        if (fcsv) fprintf(fcsv, "idx,mask,simd,scalar,simd_class,scalar_class,nfs_ratio,set_type\n");
    }

    long long mismatches = 0;
    double nfs_sum = 0.0;
    char mask_str[512];

    for (int i=0;i<samples;i++) {
        unsigned s = seed + i*17u;
        mask_t hand = 0;
        if (!strcmp(mode,"sf-heavy")) hand = random_7card_sf_heavy(s);
        else if (!strcmp(mode,"nfs-heavy")) {
            /* rejection to push NFS */
            for (int t=0;t<10000;t++) { hand = random_7card_mask(s+t*3); if (compute_nfs_ratio(hand) >= 0.8) break; }
        } else {
            hand = random_7card_mask(s);
        }

        eval_t e_simd = pe_eval_7c(ctx_simd, hand);
        eval_t e_scal = pe_eval_7c(ctx_scal, hand);
        double nfs = compute_nfs_ratio(hand);
        nfs_sum += nfs;

        if (e_simd != e_scal) {
            mismatches++;
            if (fcsv) {
                mask_to_string(hand, mask_str, sizeof(mask_str));
                hand_class_t hc_simd = eval_get_hand_class(e_simd);
                hand_class_t hc_scal = eval_get_hand_class(e_scal);
                fprintf(fcsv, "%d,%s,%u,%u,%d,%d,%.6f,%s\n",
                        i, mask_str, (unsigned)e_simd, (unsigned)e_scal,
                        (int)hc_simd, (int)hc_scal, nfs, mode);
            }
        }
    }

    double mismatch_rate = (double)mismatches / (double)samples;
    double nfs_avg = nfs_sum / (double)samples;
    printf("Validation 7c SIMD vs Scalar: samples=%d mode=%s\n", samples, mode);
    printf("Mismatches: %lld (%.6f)\n", mismatches, mismatch_rate);
    printf("Average NFS ratio: %.4f\n", nfs_avg);
    if (fcsv) { fclose(fcsv); printf("CSV written to %s\n", csv); }

    eval_context_destroy(ctx_simd);
    eval_context_destroy(ctx_scal);
    return (mismatches==0) ? 0 : 2;
}

