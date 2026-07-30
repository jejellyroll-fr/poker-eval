/*
 * eval_7c_simd.c - SIMD-accelerated 7→5 evaluation (phase 1 scaffold)
 *
 * Phase 1: Wire-up and fallback to scalar combo enumeration.
 * Next phases will add AVX2/AVX-512 vector kernels for classification
 * and reduction over the 21 five-card combinations.
 */

#include <poker_eval/core/eval_7c_simd.h>
#include <poker_eval/core/combo_7to5.h>
#include <poker_eval/core/canonical_5card.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/eval_context.h>

#if defined(__AVX2__) || defined(__AVX512F__) || defined(HAS_AVX2) || defined(HAS_AVX512)
#include <immintrin.h>
#endif

/* Precomputed tables for 13-bit masks (used by both SIMD and scalar fallback) */
static int pc32_tbl[8192];     /* popcount of 13-bit value */
static int straight_tbl[8192]; /* 1 if straight present */
static int top1_tbl[8192];     /* highest set bit index (or -1) */
static int top2_tbl[8192];     /* second highest set bit (or -1) */
static int top3_tbl[8192];
static int top4_tbl[8192];
static int top5_tbl[8192];
static uint32_t hc5_tbl[8192];    /* high-card kicker aggregate for 5 singles */
static uint32_t pair3_tbl[8192];  /* pair kickers (3 singles): 1000*x1+100*x2+10*x3 */
static uint32_t trips2_tbl[8192]; /* trips kickers (2 singles): 100*x1+1*x2 */
static int tables_init = 0;

static void init_tables_once(void)
{
    if (tables_init)
        return;
    for (int i = 0; i < 8192; ++i)
    {
        /* popcount */
        int x = i, cnt = 0;
        while (x)
        {
            cnt += x & 1;
            x >>= 1;
        }
        pc32_tbl[i] = cnt;
        /* straight */
        int r = i;
        int is = 0;
        if ((r & (1 << 12)) && ((r & 0x0F) == 0x0F))
            is = 1; /* A-5 */
        else
        {
            for (int hi = 12; hi >= 4; --hi)
            {
                int mask = 0x1F << (hi - 4);
                if ((r & mask) == mask)
                {
                    is = 1;
                    break;
                }
            }
        }
        straight_tbl[i] = is;

        /* top-k ranks and kicker aggregates */
        int t = i;
        int t1 = -1, t2 = -1, t3 = -1, t4 = -1, t5 = -1;
        for (int k = 0; k < 5; k++)
        {
            int tb = -1;
            if (t)
            {
                tb = 31 - __builtin_clz(t);
                t &= ~(1 << tb);
            }
            if (k == 0)
                t1 = tb;
            else if (k == 1)
                t2 = tb;
            else if (k == 2)
                t3 = tb;
            else if (k == 3)
                t4 = tb;
            else
                t5 = tb;
        }
        top1_tbl[i] = t1;
        top2_tbl[i] = t2;
        top3_tbl[i] = t3;
        top4_tbl[i] = t4;
        top5_tbl[i] = t5;
        hc5_tbl[i] = (uint32_t)((t1 >= 0 ? t1 * 10000 : 0) + (t2 >= 0 ? t2 * 1000 : 0) + (t3 >= 0 ? t3 * 100 : 0) + (t4 >= 0 ? t4 * 10 : 0) + (t5 >= 0 ? t5 : 0));
        pair3_tbl[i] = (uint32_t)((t1 >= 0 ? t1 * 1000 : 0) + (t2 >= 0 ? t2 * 100 : 0) + (t3 >= 0 ? t3 * 10 : 0));
        trips2_tbl[i] = (uint32_t)((t1 >= 0 ? t1 * 100 : 0) + (t2 >= 0 ? t2 : 0));
    }
    tables_init = 1;
}

/* Helpers for 5-card evaluation (NFS path) */
static inline void five_suit_masks(mask_t five, uint32_t suits[4])
{
    suits[0] = (uint32_t)((five >> 0) & 0x1FFFu);
    suits[1] = (uint32_t)((five >> 13) & 0x1FFFu);
    suits[2] = (uint32_t)((five >> 26) & 0x1FFFu);
    suits[3] = (uint32_t)((five >> 39) & 0x1FFFu);
}

static inline int popcnt32(uint32_t x) { return __builtin_popcount(x); }

static inline int is_flush_5(uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3)
{
    return popcnt32(s0) == 5 || popcnt32(s1) == 5 || popcnt32(s2) == 5 || popcnt32(s3) == 5;
}

static inline int is_straight_from_ranks(uint32_t r)
{
    /* A-5 wheel */
    if ((r & (1u << 12)) && ((r & 0x0Fu) == 0x0Fu))
        return 1;
    for (int hi = 12; hi >= 4; --hi)
    {
        uint32_t mask = 0x1Fu << (hi - 4);
        if ((r & mask) == mask)
            return 1;
    }
    return 0;
}

static inline int topbit(uint32_t m) { return m ? (31 - __builtin_clz(m)) : -1; }
static inline int pop_lsb(uint32_t *m)
{
    int b = __builtin_ctz(*m);
    *m &= (*m - 1);
    return b;
}

static inline eval_t eval_5c_nfs_from_suits(uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3)
{
    /* Bitwise multiplicity masks per rank */
    uint32_t A1 = (s0 | s1) | (s2 | s3);
    uint32_t P01 = s0 & s1, P02 = s0 & s2, P03 = s0 & s3, P12 = s1 & s2, P13 = s1 & s3, P23 = s2 & s3;
    uint32_t A2 = (P01 | P02) | (P03 | P12) | (P13 | P23);
    uint32_t T012 = s0 & s1 & s2, T013 = s0 & s1 & s3, T023 = s0 & s2 & s3, T123 = s1 & s2 & s3;
    uint32_t A3 = (T012 | T013) | (T023 | T123);
    uint32_t A4 = ((s0 & s1) & s2) & s3;

    uint32_t M4 = A4;
    uint32_t M3 = A3 & ~A4;
    uint32_t M2 = A2 & ~A3;
    uint32_t M1 = A1 & ~A2;

    uint32_t base = 0, kick = 0;

    if (M4)
    {
        int q = topbit(M4);
        base = EVAL_QUADS;
        kick = (uint32_t)q * 1000u;
        uint32_t others = (A1 & ~(1u << q));
        if (others)
            kick += (uint32_t)topbit(others);
        return base + kick;
    }

    if (M3)
    {
        int t1 = topbit(M3);
        uint32_t M3r = M3 & ~(1u << t1);
        if (M3r || M2)
        {
            int pr = M3r ? topbit(M3r) : topbit(M2);
            base = EVAL_FULL_HOUSE;
            kick = (uint32_t)t1 * 1000u + (uint32_t)pr;
            return base + kick;
        }
        base = EVAL_TRIPS;
        kick = (uint32_t)t1 * 10000u;
        kick += trips2_tbl[M1];
        return base + kick;
    }

    if ((M2 & (M2 - 1)) != 0)
    { /* at least two bits set */
        int p1 = topbit(M2);
        uint32_t M2r = M2 & ~(1u << p1);
        int p2 = topbit(M2r);
        base = EVAL_TWO_PAIR;
        kick = (uint32_t)p1 * 1000u + (uint32_t)p2 * 100u;
        if (M1)
            kick += (uint32_t)top1_tbl[M1];
        return base + kick;
    }

    if (M2)
    {
        int p = topbit(M2);
        base = EVAL_PAIR;
        kick = (uint32_t)p * 10000u;
        kick += pair3_tbl[M1];
        return base + kick;
    }

    /* High card */
    base = EVAL_HIGH_CARD;
    kick = hc5_tbl[M1];
    return base + kick;
}

static inline eval_t eval_5c_nfs_from_masks(uint32_t A1, uint32_t M1, uint32_t M2, uint32_t M3, uint32_t M4)
{
    uint32_t base = 0, kick = 0;
    if (M4)
    {
        int q = top1_tbl[M4];
        base = EVAL_QUADS;
        kick = (uint32_t)q * 1000u;
        uint32_t others = (A1 & ~(1u << q));
        if (others)
            kick += (uint32_t)top1_tbl[others];
        return base + kick;
    }
    if (M3)
    {
        int t1 = top1_tbl[M3];
        uint32_t M3r = M3 & ~(1u << t1);
        if (M3r || M2)
        {
            int pr = M3r ? top1_tbl[M3r] : top1_tbl[M2];
            base = EVAL_FULL_HOUSE;
            kick = (uint32_t)t1 * 1000u + (uint32_t)pr;
            return base + kick;
        }
        base = EVAL_TRIPS;
        kick = (uint32_t)t1 * 10000u;
        kick += trips2_tbl[M1];
        return base + kick;
    }
    if ((M2 & (M2 - 1)) != 0)
    {
        int p1 = top1_tbl[M2];
        uint32_t M2r = M2 & ~(1u << p1);
        int p2 = top1_tbl[M2r];
        base = EVAL_TWO_PAIR;
        kick = (uint32_t)p1 * 1000u + (uint32_t)p2 * 100u;
        if (M1)
            kick += (uint32_t)top1_tbl[M1];
        return base + kick;
    }
    if (M2)
    {
        int p = top1_tbl[M2];
        base = EVAL_PAIR;
        kick = (uint32_t)p * 10000u;
        kick += pair3_tbl[M1];
        return base + kick;
    }
    base = EVAL_HIGH_CARD;
    kick = hc5_tbl[M1];
    return base + kick;
}

static inline mask_t five_from_positions(const int cards[7], const uint8_t pos[5])
{
    mask_t m = 0;
    m = mask_set(m, cards[pos[0]]);
    m = mask_set(m, cards[pos[1]]);
    m = mask_set(m, cards[pos[2]]);
    m = mask_set(m, cards[pos[3]]);
    m = mask_set(m, cards[pos[4]]);
    return m;
}

eval_t pe_eval_7c_simd(const EvalContext *ctx, mask_t seven_mask)
{
#if defined(HAS_AVX2) || defined(HAS_AVX512)
    if (!ctx || mask_popcount(seven_mask) != 7)
        return EVAL_INVALID;

    /* Build 7-card list */
    int cards[7];
    int ci = 0;
    for (int c = mask_first_set(seven_mask); c >= 0 && ci < 7; seven_mask = mask_unset(seven_mask, c), c = mask_first_set(seven_mask))
    {
        cards[ci++] = c;
    }
    if (ci != 7)
        return EVAL_INVALID;

    eval_t best_eval_scalar = EVAL_INVALID;
    __m256i best_vec = _mm256_set1_epi32(0);
    int have_vec = 0;

    init_tables_once();
    uint32_t processed = 0;
    for (int base = 0; base < 21; base += 8)
    {
        eval_t vals[8] = {0};
        int lanes = (21 - base) < 8 ? (21 - base) : 8;

        /* Prepare lane data */
        uint32_t s0v[8] = {0}, s1v[8] = {0}, s2v[8] = {0}, s3v[8] = {0}, rv[8] = {0};
        mask_t fiveMasks[8] = {0};
        for (int i = 0; i < lanes; ++i)
        {
            const combo_7to5_pattern_t *pat = &combo_patterns[base + i];
            mask_t five = five_from_positions(cards, pat->positions);
            fiveMasks[i] = five;
            uint32_t suits[4];
            five_suit_masks(five, suits);
            s0v[i] = suits[0];
            s1v[i] = suits[1];
            s2v[i] = suits[2];
            s3v[i] = suits[3];
            rv[i] = suits[0] | suits[1] | suits[2] | suits[3];
        }

#if defined(HAS_AVX2)
        __m256i vS0 = _mm256_loadu_si256((const __m256i *)s0v);
        __m256i vS1 = _mm256_loadu_si256((const __m256i *)s1v);
        __m256i vS2 = _mm256_loadu_si256((const __m256i *)s2v);
        __m256i vS3 = _mm256_loadu_si256((const __m256i *)s3v);
        __m256i vR = _mm256_loadu_si256((const __m256i *)rv);

        __m256i pc0 = _mm256_i32gather_epi32(pc32_tbl, vS0, 4);
        __m256i pc1 = _mm256_i32gather_epi32(pc32_tbl, vS1, 4);
        __m256i pc2 = _mm256_i32gather_epi32(pc32_tbl, vS2, 4);
        __m256i pc3 = _mm256_i32gather_epi32(pc32_tbl, vS3, 4);
        __m256i five = _mm256_set1_epi32(5);
        __m256i f0 = _mm256_cmpeq_epi32(pc0, five);
        __m256i f1 = _mm256_cmpeq_epi32(pc1, five);
        __m256i f2 = _mm256_cmpeq_epi32(pc2, five);
        __m256i f3 = _mm256_cmpeq_epi32(pc3, five);
        __m256i f_or = _mm256_or_si256(_mm256_or_si256(f0, f1), _mm256_or_si256(f2, f3));

        __m256i isStr = _mm256_i32gather_epi32(straight_tbl, vR, 4);

        /* Compute vA1..vA4 and vM1..vM4 per-lane (packed 32-bit) */
        __m256i vA1 = _mm256_or_si256(_mm256_or_si256(vS0, vS1), _mm256_or_si256(vS2, vS3));
        __m256i vP01 = _mm256_and_si256(vS0, vS1);
        __m256i vP02 = _mm256_and_si256(vS0, vS2);
        __m256i vP03 = _mm256_and_si256(vS0, vS3);
        __m256i vP12 = _mm256_and_si256(vS1, vS2);
        __m256i vP13 = _mm256_and_si256(vS1, vS3);
        __m256i vP23 = _mm256_and_si256(vS2, vS3);
        __m256i vA2 = _mm256_or_si256(_mm256_or_si256(_mm256_or_si256(vP01, vP02), _mm256_or_si256(vP03, vP12)), _mm256_or_si256(vP13, vP23));
        __m256i vT012 = _mm256_and_si256(vP01, vS2);
        __m256i vT013 = _mm256_and_si256(vP01, vS3);
        __m256i vT023 = _mm256_and_si256(vP02, vS3);
        __m256i vT123 = _mm256_and_si256(vP12, vS3);
        __m256i vA3 = _mm256_or_si256(_mm256_or_si256(vT012, vT013), _mm256_or_si256(vT023, vT123));
        __m256i vA4 = _mm256_and_si256(_mm256_and_si256(vS0, vS1), _mm256_and_si256(vS2, vS3));
        __m256i vNotA4 = _mm256_xor_si256(vA4, _mm256_set1_epi32(-1));
        __m256i vNotA3 = _mm256_xor_si256(vA3, _mm256_set1_epi32(-1));
        __m256i vNotA2 = _mm256_xor_si256(vA2, _mm256_set1_epi32(-1));
        __m256i vM4 = vA4;
        __m256i vM3 = _mm256_and_si256(vA3, vNotA4);
        __m256i vM2 = _mm256_and_si256(vA2, vNotA3);
        __m256i vM1 = _mm256_and_si256(vA1, vNotA2);

        int fmask[8], smask[8];
        _mm256_storeu_si256((__m256i *)fmask, f_or);
        _mm256_storeu_si256((__m256i *)smask, isStr);

        uint32_t A1a[8], M1a[8], M2a[8], M3a[8], M4a[8];
        _mm256_storeu_si256((__m256i *)A1a, vA1);
        _mm256_storeu_si256((__m256i *)M1a, vM1);
        _mm256_storeu_si256((__m256i *)M2a, vM2);
        _mm256_storeu_si256((__m256i *)M3a, vM3);
        _mm256_storeu_si256((__m256i *)M4a, vM4);

        for (int i = 0; i < lanes; ++i)
        {
            int flush = fmask[i] != 0;
            int straight = smask[i] != 0;
            if (!flush && !straight)
            {
                vals[i] = eval_5c_nfs_from_masks(A1a[i], M1a[i], M2a[i], M3a[i], M4a[i]);
            }
            else
            {
                vals[i] = pe_eval_5c(ctx, fiveMasks[i]);
            }
        }
#else
        for (int i = 0; i < lanes; ++i)
        {
            int flush = is_flush_5(s0v[i], s1v[i], s2v[i], s3v[i]);
            int straight = is_straight_from_ranks(rv[i]);
            if (!flush && !straight)
                vals[i] = eval_5c_nfs_from_suits(s0v[i], s1v[i], s2v[i], s3v[i]);
            else
                vals[i] = pe_eval_5c(ctx, fiveMasks[i]);
        }
#endif
#if defined(HAS_AVX2)
        if (lanes == 8)
        {
            __m256i v = _mm256_loadu_si256((const __m256i *)vals);
            if (!have_vec)
            {
                best_vec = v;
                have_vec = 1;
            }
            else
            {
                best_vec = _mm256_max_epu32(best_vec, v);
            }
        }
        else
        {
            /* scalar reduction for tail */
            for (int i = 0; i < lanes; ++i)
                if (vals[i] > best_eval_scalar)
                    best_eval_scalar = vals[i];
        }
#else
        for (int i = 0; i < lanes; ++i)
            if (vals[i] > best_eval_scalar)
                best_eval_scalar = vals[i];
#endif
        processed += (uint32_t)lanes;
    }

    eval_t best = best_eval_scalar;
#if defined(HAS_AVX2)
    if (have_vec)
    {
        /* horizontal max of 8 lanes */
        __m256i t1 = _mm256_permute2x128_si256(best_vec, best_vec, 1);
        __m256i m1 = _mm256_max_epu32(best_vec, t1);
        __m256i m2 = _mm256_max_epu32(m1, _mm256_shuffle_epi32(m1, _MM_SHUFFLE(2, 3, 0, 1)));
        __m256i m3 = _mm256_max_epu32(m2, _mm256_shuffle_epi32(m2, _MM_SHUFFLE(1, 0, 3, 2)));
        uint32_t tmp[8];
        _mm256_storeu_si256((__m256i *)tmp, m3);
        if (tmp[0] > best)
            best = tmp[0];
    }
#endif
    /* Update statistics: combinations enumerated precisely */
    eval_context_add_combinations(ctx, processed);
    return best;
#else
    (void)ctx;
    (void)seven_mask;
    return EVAL_INVALID;
#endif
}
