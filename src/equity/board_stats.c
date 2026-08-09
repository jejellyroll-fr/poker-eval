/*
 * board_stats.c - Board scoring utilities for importance sampling
 */

#include <poker_eval/equity/board_stats.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/card.h>
#include <string.h>
#include <stdlib.h>
#include <poker_eval/core/pcg_rng.h>

double pe_board_partial_score(StdDeck_CardMask partial_missing_cards,
                              int num_cards_drawn) {
    /* Heuristic similar to Phase 1, but factored here. */
    int rank_count[13]; memset(rank_count, 0, sizeof(rank_count));
    int suit_count[4]; memset(suit_count, 0, sizeof(suit_count));
    for (int c = 0; c < StdDeck_N_CARDS; c++) {
        if (StdDeck_CardMask_CARD_IS_SET(partial_missing_cards, c)) {
            int r = StdDeck_RANK(c);
            int s = StdDeck_SUIT(c);
            if (r >= 0 && r < 13) rank_count[r]++;
            if (s >= 0 && s < 4) suit_count[s]++;
        }
    }
    (void)num_cards_drawn;
    int paired = 0;
    for (int r = 0; r < 13; r++) if (rank_count[r] >= 2) { paired = 1; break; }
    int max_suit = 0; for (int s = 0; s < 4; s++) if (suit_count[s] > max_suit) max_suit = suit_count[s];
    int minr = 12, maxr = 0; int distinct = 0;
    for (int r = 0; r < 13; r++) if (rank_count[r] > 0) { if (r < minr) minr = r; if (r > maxr) maxr = r; distinct++; }
    int span = (distinct > 0 ? (maxr - minr) : 0);
    int straightish = (distinct >= 3 && span <= 3) ? 1 : 0;
    double score = 1.0 + 2.0 * paired + 0.5 * (double)(max_suit > 1 ? (max_suit - 1) : 0) + 0.5 * straightish;
    if (score < 0.5) score = 0.5;
    return score;
}

/* --- Pair-class utilities --- */

pe_bpair_class_t pe_board_pairclass(StdDeck_CardMask cards) {
    int rank_count[13]; memset(rank_count, 0, sizeof(rank_count));
    for (int c = 0; c < StdDeck_N_CARDS; ++c) {
        if (StdDeck_CardMask_CARD_IS_SET(cards, c)) {
            rank_count[StdDeck_RANK(c)]++;
        }
    }
    int has_pair = 0;
    for (int r = 0; r < 13; ++r) {
        if (rank_count[r] >= 3) return PE_BPAIR_TRIPS;
        if (rank_count[r] == 2) has_pair = 1;
    }
    return has_pair ? PE_BPAIR_ONE_PAIR : PE_BPAIR_DISTINCT;
}

/* Build available mask = full deck minus dead */
static inline StdDeck_CardMask available_from_dead(StdDeck_CardMask dead) {
    StdDeck_CardMask avail;
    StdDeck_CardMask_RESET(avail);
    for (int c = 0; c < StdDeck_N_CARDS; ++c) {
        if (!StdDeck_CardMask_CARD_IS_SET(dead, c)) {
            StdDeck_CardMask_SET(avail, c);
        }
    }
    return avail;
}

/* Tally per-rank available counts and optionally store card indices per rank */
static int tally_rank_avail(const StdDeck_CardMask avail,
                            int m_rank[13], int* lists[13]) {
    int N = 0;
    for (int r = 0; r < 13; ++r) { m_rank[r] = 0; if (lists) lists[r] = NULL; }
    static int scratch[13][4];
    for (int c = 0; c < StdDeck_N_CARDS; ++c) {
        if (StdDeck_CardMask_CARD_IS_SET(avail, c)) {
            int r = StdDeck_RANK(c);
            int k = m_rank[r]++;
            if (lists && k < 4) scratch[r][k] = c;
            ++N;
        }
    }
    if (lists) {
        for (int r = 0; r < 13; ++r) lists[r] = (m_rank[r] > 0) ? scratch[r] : NULL;
    }
    return N;
}

/* Simple safe binomial C(n,k) for small n<=52 */
static inline uint64_t C_u64(int n, int k) {
    if (k < 0 || k > n) return 0ULL;
    if (k == 0 || k == n) return 1ULL;
    if (k > n - k) k = n - k;
    uint64_t r = 1ULL;
    for (int i = 1; i <= k; ++i) {
        r = (r * (uint64_t)(n - (k - i))) / (uint64_t)i;
    }
    return r;
}

pe_bpair_counts_t pe_board_pair_counts(StdDeck_CardMask dead, int k) {
    pe_bpair_counts_t out; out.count[0] = out.count[1] = out.count[2] = 0ULL; out.total = 0ULL;
    StdDeck_CardMask avail = available_from_dead(dead);
    int m_rank[13]; tally_rank_avail(avail, m_rank, NULL);

    int N = 0; for (int r = 0; r < 13; ++r) N += m_rank[r];
    if (k < 0 || k > N) return out;
    out.total = C_u64(N, k);
    if (k == 0) { out.count[PE_BPAIR_DISTINCT] = 1; return out; }
    if (k == 1) { out.count[PE_BPAIR_DISTINCT] = (uint64_t)N; return out; }

    if (k == 2) {
        uint64_t pairs = 0, distinct = 0;
        for (int r = 0; r < 13; ++r) pairs += C_u64(m_rank[r], 2);
        distinct = out.total - pairs;
        out.count[PE_BPAIR_ONE_PAIR] = pairs;
        out.count[PE_BPAIR_DISTINCT] = distinct;
        return out;
    }

    if (k == 3) {
        /* trips: sum_r C(m_r,3) */
        uint64_t trips = 0ULL;
        for (int r = 0; r < 13; ++r) trips += C_u64(m_rank[r], 3);
        /* one pair: sum_r C(m_r,2) * (N - m_r) */
        uint64_t onepair = 0ULL;
        for (int r = 0; r < 13; ++r) onepair += C_u64(m_rank[r], 2) * (uint64_t)(N - m_rank[r]);
        /* distinct: remainder */
        uint64_t distinct = out.total - onepair - trips;
        out.count[PE_BPAIR_TRIPS] = trips;
        out.count[PE_BPAIR_ONE_PAIR] = onepair;
        out.count[PE_BPAIR_DISTINCT] = distinct;
        return out;
    }

    /* For K>3 we only provide a total and leave counts zero (not used here) */
    return out;
}

/* RNG helper */
static inline int urand_bounded(int bound) {
    if (bound <= 0) return 0;
    return (int)pe_rng_below(pe_rng_current(), (uint32_t)bound);
}

/* Choose rank index with probability proportional to weights[r] among ranks with weights>0 */
static int choose_rank_weighted(const uint64_t weights[13]) {
    uint64_t sum = 0ULL; for (int r = 0; r < 13; ++r) sum += weights[r];
    if (sum == 0ULL) return -1;
    double u = pe_rng_uniform01(pe_rng_current());
    uint64_t x = (uint64_t)(u * (double)sum);
    uint64_t acc = 0ULL;
    for (int r = 0; r < 13; ++r) {
        acc += weights[r];
        if (x < acc) return r;
    }
    return 12; /* fallback */
}

/* Sample 2 distinct indices in [0..m-1] uniformly */
static void sample2(int m, int* a, int* b) {
    int i = urand_bounded(m);
    int j = urand_bounded(m - 1);
    if (j >= i) j++;
    *a = i; *b = j;
}

/* Sample 3 distinct indices in [0..m-1] uniformly */
static void sample3(int m, int* a, int* b, int* c) {
    int i = urand_bounded(m);
    int j = urand_bounded(m - 1); if (j >= i) j++;
    int k = urand_bounded(m - 2);
    int x = i, y = j;
    /* Map k skipping i and j */
    int t = k;
    if (t >= x) t++;
    if (t >= y) t++;
    *a = i; *b = j; *c = t;
}

int pe_board_sample_pairclass(StdDeck_CardMask dead, int k,
                              pe_bpair_class_t cls,
                              StdDeck_CardMask* out) {
    if (!out) return 0;
    StdDeck_CardMask avail = available_from_dead(dead);
    int m_rank[13]; int* rank_lists[13];
    int N = tally_rank_avail(avail, m_rank, rank_lists);
    if (k < 0 || k > N) return 0;

    StdDeck_CardMask_RESET(*out);
    if (k == 0) return 1;
    if (k == 1) {
        /* Any single card */
        int idx = urand_bounded(N);
        for (int c = 0, seen = 0; c < StdDeck_N_CARDS; ++c) {
            if (StdDeck_CardMask_CARD_IS_SET(avail, c)) {
                if (seen == idx) { StdDeck_CardMask_SET(*out, c); return 1; }
                ++seen;
            }
        }
        return 0;
    }

    if (k == 2) {
        if (cls == PE_BPAIR_ONE_PAIR) {
            uint64_t wrank[13]; for (int r = 0; r < 13; ++r) wrank[r] = C_u64(m_rank[r], 2);
            int r = choose_rank_weighted(wrank); if (r < 0 || m_rank[r] < 2) return 0;
            int a, b; sample2(m_rank[r], &a, &b);
            StdDeck_CardMask_SET(*out, rank_lists[r][a]);
            StdDeck_CardMask_SET(*out, rank_lists[r][b]);
            return 1;
        } else { /* DISTINCT */
            /* Choose two distinct ranks with weights m_r, then one card from each */
            uint64_t wrank[13]; for (int r = 0; r < 13; ++r) wrank[r] = (uint64_t)m_rank[r];
            int r1 = choose_rank_weighted(wrank); if (r1 < 0 || m_rank[r1] == 0) return 0;
            uint64_t wrank2[13]; for (int r = 0; r < 13; ++r) wrank2[r] = (r == r1) ? 0ULL : (uint64_t)m_rank[r];
            int r2 = choose_rank_weighted(wrank2); if (r2 < 0 || m_rank[r2] == 0) return 0;
            int i1 = urand_bounded(m_rank[r1]);
            int i2 = urand_bounded(m_rank[r2]);
            StdDeck_CardMask_SET(*out, rank_lists[r1][i1]);
            StdDeck_CardMask_SET(*out, rank_lists[r2][i2]);
            return 1;
        }
    }

    if (k == 3) {
        if (cls == PE_BPAIR_TRIPS) {
            uint64_t wrank[13]; for (int r = 0; r < 13; ++r) wrank[r] = C_u64(m_rank[r], 3);
            int r = choose_rank_weighted(wrank); if (r < 0 || m_rank[r] < 3) return 0;
            int a, b, c; sample3(m_rank[r], &a, &b, &c);
            StdDeck_CardMask_SET(*out, rank_lists[r][a]);
            StdDeck_CardMask_SET(*out, rank_lists[r][b]);
            StdDeck_CardMask_SET(*out, rank_lists[r][c]);
            return 1;
        } else if (cls == PE_BPAIR_ONE_PAIR) {
            /* Pick rank with weight C(m_r,2)*(N-m_r), then pick pair suits and a kicker from other ranks */
            uint64_t wrank[13];
            for (int r = 0; r < 13; ++r) wrank[r] = C_u64(m_rank[r], 2) * (uint64_t)(N - m_rank[r]);
            int rp = choose_rank_weighted(wrank); if (rp < 0 || m_rank[rp] < 2) return 0;
            int a, b; sample2(m_rank[rp], &a, &b);
            StdDeck_CardMask_SET(*out, rank_lists[rp][a]);
            StdDeck_CardMask_SET(*out, rank_lists[rp][b]);
            /* Choose kicker from any other rank uniformly */
            int others = N - m_rank[rp]; if (others <= 0) return 0;
            int t = urand_bounded(others);
            for (int r = 0; r < 13; ++r) if (r != rp) {
                if (t < m_rank[r]) { StdDeck_CardMask_SET(*out, rank_lists[r][t]); return 1; }
                t -= m_rank[r];
            }
            return 0;
        } else { /* DISTINCT */
            /* Choose three distinct ranks with weights m_r (without replacement) */
            uint64_t w1[13]; for (int r = 0; r < 13; ++r) w1[r] = (uint64_t)m_rank[r];
            int r1 = choose_rank_weighted(w1); if (r1 < 0 || m_rank[r1] == 0) return 0;
            uint64_t w2[13]; for (int r = 0; r < 13; ++r) w2[r] = (r == r1) ? 0ULL : (uint64_t)m_rank[r];
            int r2 = choose_rank_weighted(w2); if (r2 < 0 || m_rank[r2] == 0) return 0;
            uint64_t w3[13]; for (int r = 0; r < 13; ++r) w3[r] = (r == r1 || r == r2) ? 0ULL : (uint64_t)m_rank[r];
            int r3 = choose_rank_weighted(w3); if (r3 < 0 || m_rank[r3] == 0) return 0;
            int i1 = urand_bounded(m_rank[r1]);
            int i2 = urand_bounded(m_rank[r2]);
            int i3 = urand_bounded(m_rank[r3]);
            StdDeck_CardMask_SET(*out, rank_lists[r1][i1]);
            StdDeck_CardMask_SET(*out, rank_lists[r2][i2]);
            StdDeck_CardMask_SET(*out, rank_lists[r3][i3]);
            return 1;
        }
    }

    return 0;
}
