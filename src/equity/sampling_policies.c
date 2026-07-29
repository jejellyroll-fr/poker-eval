/*
 * sampling_policies.c - Monte Carlo/QMC sampling policies for equity estimation
 */

#include <poker_eval/equity/sampling_policies.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/board_stats.h>
#include <poker_eval/core/enumeration_adapters.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <poker_eval/equity/batched_montecarlo.h>

static pe_sampling_ctx_t g_ctx = {0};

void pe_sampling_set_policy(pe_sampling_policy_t policy) {
    g_ctx.policy = policy;
}

pe_sampling_policy_t pe_sampling_get_policy(void) {
    return g_ctx.policy;
}

void pe_sampling_reset(uint64_t seq_start) {
    g_ctx.seq_index = seq_start;
}

void pe_sampling_set_pair_weights(const double weights[3]) {
    if (!weights) return;
    g_ctx.pair_weights[0] = weights[0];
    g_ctx.pair_weights[1] = weights[1];
    g_ctx.pair_weights[2] = weights[2];
    g_ctx.custom_weights_set = 1;
    /* Recompute normalizer if counts are known */
    g_ctx.pair_Z = 0.0;
    for (int c = 0; c < 3; ++c) {
        g_ctx.pair_Z += g_ctx.pair_weights[c] * (double)g_ctx.pair_counts[c];
    }
}

/* Parse once: PE_STRAT_WEIGHTS="w0,w1,w2" and apply */
static void pe_apply_env_pair_weights_if_any(void) {
    static int parsed = 0;
    if (parsed) return;
    parsed = 1;
    const char* ev = getenv("PE_STRAT_WEIGHTS");
    if (!ev || !*ev) return;
    char buf[128];
    size_t n = strlen(ev); if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, ev, n); buf[n] = '\0';
    for (char* p = buf; *p; ++p) if (*p == ';') *p = ','; /* tolerate ';' */
    /* strtod rather than sscanf: the return value tells how many conversions
     * succeeded but not where they stopped, so "1,2,3junk" was accepted. */
    double w[3] = { 0.0, 0.0, 0.0 };
    const char* cursor = buf;
    int ok = 1;
    for (int i = 0; i < 3 && ok; ++i) {
        char* end = NULL;
        errno = 0;
        w[i] = strtod(cursor, &end);
        if (end == cursor || errno == ERANGE) {
            ok = 0;
            break;
        }
        cursor = end;
        if (i < 2) {
            if (*cursor != ',') ok = 0;
            else ++cursor;
        }
    }
    while (ok && *cursor == ' ') ++cursor;
    if (ok && *cursor != '\0') ok = 0;
    if (ok) {
        pe_sampling_set_pair_weights(w);
    }
}

/* --- Halton sequence utilities --- */
static double halton_radical_inverse(uint64_t n, uint32_t base) {
    double f = 1.0;
    double r = 0.0;
    while (n > 0) {
        f /= (double)base;
        r += f * (double)(n % base);
        n /= base;
    }
    return r;
}

/* Bases for first few dimensions (pairwise coprime) */
static const uint32_t k_halton_bases[] = { 2, 3, 5, 7, 11, 13, 17 };

static void qmc_halton_draw_cards(StdDeck_CardMask dead,
                                  int num_cards,
                                  uint64_t seq_idx,
                                  StdDeck_CardMask* out_board) {
    StdDeck_CardMask used = dead;
    StdDeck_CardMask_RESET(*out_board);

    /* For each required card, use its Halton dimension to propose a card index */
    for (int j = 0; j < num_cards; j++) {
        /* Map [0,1) → [0,52) with a per-dimension radical inverse */
        uint32_t base = k_halton_bases[j % (int)(sizeof(k_halton_bases)/sizeof(k_halton_bases[0]))];
        /* Use sequence index + small offset to decorrelate dimensions */
        uint64_t n = seq_idx + (uint64_t)j * 1469598103934665603ULL; /* FNV-ish stride */
        double u = halton_radical_inverse(n, base);
        int proposal = (int)(u * (double)StdDeck_N_CARDS);

        /* Rejection with cyclic scan to avoid duplicates */
        if (proposal < 0) proposal = 0;
        if (proposal >= StdDeck_N_CARDS) proposal = StdDeck_N_CARDS - 1;
        int c = proposal;
        int k = 0;
        while (k < StdDeck_N_CARDS && StdDeck_CardMask_CARD_IS_SET(used, c)) {
            c = (c + 1) % StdDeck_N_CARDS;
            k++;
        }
        if (k == StdDeck_N_CARDS) {
            /* fallback: shouldn't happen */
            continue;
        }
        StdDeck_CardMask_SET(*out_board, c);
        StdDeck_CardMask_SET(used, c);
    }
}

/* --- Sobol (base-2) with digital Owen scramble per dimension --- */
static inline uint32_t reverse_bits32(uint32_t x) {
    x = ((x >> 1)  & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2)  & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4)  & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8)  & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    x = (x >> 16) | (x << 16);
    return x;
}

static inline double vdc_base2_scrambled(uint64_t n, uint32_t scramble) {
    /* 32-bit van der Corput in base 2; XOR digital scramble */
    uint32_t rev = reverse_bits32((uint32_t)n) ^ scramble;
    return (double)rev / 4294967296.0; /* 2^32 */
}

static void qmc_sobol_draw_cards(StdDeck_CardMask dead,
                                 int num_cards,
                                 uint64_t seq_idx,
                                 StdDeck_CardMask* out_board) {
    StdDeck_CardMask used = dead;
    StdDeck_CardMask_RESET(*out_board);
    /* Digital scrambles per dimension derived from seq_idx and constants */
    for (int j = 0; j < num_cards; j++) {
        uint32_t scramble = (uint32_t)(0x9E3779B9u * (j + 1)) ^ (uint32_t)(seq_idx * 0x85EBCA6Bu);
        double u = vdc_base2_scrambled((uint64_t)(seq_idx + (uint64_t)j * 1315423911ULL), scramble);
        int proposal = (int)(u * (double)StdDeck_N_CARDS);
        if (proposal < 0) proposal = 0;
        if (proposal >= StdDeck_N_CARDS) proposal = StdDeck_N_CARDS - 1;
        int c = proposal;
        int k = 0;
        while (k < StdDeck_N_CARDS && StdDeck_CardMask_CARD_IS_SET(used, c)) {
            c = (c + 1) % StdDeck_N_CARDS;
            k++;
        }
        if (k == StdDeck_N_CARDS) continue;
        StdDeck_CardMask_SET(*out_board, c);
        StdDeck_CardMask_SET(used, c);
    }
}

/* --- Uniform MC fallback --- */
static void mc_uniform_draw_cards(StdDeck_CardMask dead,
                                 int num_cards,
                                 StdDeck_CardMask* out_board) {
    /* Branchless bit-mask sampler for k-from-mask */
    mask_t avail = MODERN_FULL_DECK_MASK & ~cardmask_to_mask_t(dead);
    mask_t m = sample_k_from_mask(avail, num_cards);
    *out_board = mask_t_to_cardmask(m);
}

/* Main entry: fill batch according to current policy */
/* --- Board-aware importance (simple heuristic) --- */
static double board_class_score(StdDeck_CardMask cards, int num_cards) {
    /* Heuristic: boost paired boards, high suitedness, tight rank spans */
    int rank_count[13]; memset(rank_count, 0, sizeof(rank_count));
    int suit_count[4]; memset(suit_count, 0, sizeof(suit_count));
    /* int ranks_bit = 0; */
    for (int c = 0; c < StdDeck_N_CARDS; c++) {
        if (StdDeck_CardMask_CARD_IS_SET(cards, c)) {
            int r = StdDeck_RANK(c);
            int s = StdDeck_SUIT(c);
            rank_count[r]++; suit_count[s]++;
            /* ranks_bit |= (1 << r); */
        }
    }
    int paired = 0;
    for (int r = 0; r < 13; r++) {
        if (rank_count[r] >= 2) { paired = 1; break; }
    }
    int max_suit = 0; for (int s = 0; s < 4; s++) if (suit_count[s] > max_suit) max_suit = suit_count[s];
    /* Straight-ish: check span */
    int minr = 12, maxr = 0; int cnt = 0;
    for (int r = 0; r < 13; r++) if (rank_count[r] > 0) { if (r < minr) minr = r; if (r > maxr) maxr = r; cnt++; }
    int span = (cnt > 0 ? (maxr - minr) : 0);
    int straightish = (cnt >= 3 && span <= 3) ? 1 : 0;
    double score = 1.0 + 2.0 * paired + 0.5 * (double)(max_suit > 1 ? (max_suit - 1) : 0) + 0.5 * straightish;
    if (score < 0.5) score = 0.5;
    return score;
}

static const double k_importance_fmax = 1.0 + 2.0 * 1.0 + 0.5 * 4.0 + 0.5 * 1.0; /* conservative upper bound */

double pe_sampling_importance_weight(StdDeck_CardMask board, int num_cards_to_draw) {
    if (g_ctx.policy == PE_SAMPLING_IMPORTANCE_BOARD_AWARE) {
        double f = board_class_score(board, num_cards_to_draw);
        if (f <= 0.0) f = 0.5;
        return 1.0 / f; /* q ∝ f(b) */
    } else if (g_ctx.policy == PE_SAMPLING_STRATIFIED_BOARD_AWARE) {
        /* q(b) is piecewise-constant within pair-class: q = w[c]/Z */
        if (num_cards_to_draw != g_ctx.last_num_cards) {
            /* Fallback to neutral weight if context out-of-sync */
            return 1.0;
        }
        pe_bpair_class_t cls = pe_board_pairclass(board);
        double w = g_ctx.pair_weights[cls];
        if (w <= 0.0 || g_ctx.pair_Z <= 0.0) return 1.0; /* degenerate */
        return g_ctx.pair_Z / w;
    }
    return 1.0;
}

void pe_sampling_generate_boards(BoardBatch* batch,
                                 StdDeck_CardMask dead,
                                 int num_cards_to_draw,
                                 int batch_size) {
    batch->count = batch_size;
    if (g_ctx.policy == PE_SAMPLING_STRATIFIED_BOARD_AWARE && (num_cards_to_draw == 2 || num_cards_to_draw == 3 || num_cards_to_draw == 1)) {
        pe_apply_env_pair_weights_if_any();
        /* Prepare pair-class counts and weights */
        pe_bpair_counts_t cnt = pe_board_pair_counts(dead, num_cards_to_draw);
        g_ctx.last_num_cards = num_cards_to_draw;
        g_ctx.last_dead = dead;
        g_ctx.pair_counts[0] = cnt.count[0];
        g_ctx.pair_counts[1] = cnt.count[1];
        g_ctx.pair_counts[2] = cnt.count[2];
        g_ctx.pair_total = cnt.total;
        /* Default heuristic weights (only if not overridden by API/env): emphasize decisive structures */
        if (!g_ctx.custom_weights_set) {
            g_ctx.pair_weights[PE_BPAIR_DISTINCT] = 1.0;
            g_ctx.pair_weights[PE_BPAIR_ONE_PAIR] = (num_cards_to_draw == 2) ? 1.5 : 1.75;
            g_ctx.pair_weights[PE_BPAIR_TRIPS]    = (num_cards_to_draw == 3) ? 3.0 : 0.0;
        }
        g_ctx.pair_Z = 0.0;
        for (int c = 0; c < 3; ++c) g_ctx.pair_Z += g_ctx.pair_weights[c] * (double)g_ctx.pair_counts[c];

        for (int i = 0; i < batch_size; ++i) {
            /* Choose target class with probability ∝ w[c]*count[c] */
        double u = rand() / ((double)RAND_MAX + 1.0);
            double target = u * g_ctx.pair_Z;
            double acc = 0.0; int chosen = PE_BPAIR_DISTINCT;
            for (int c = 0; c < 3; ++c) {
                double mass = g_ctx.pair_weights[c] * (double)g_ctx.pair_counts[c];
                acc += mass;
                if (target <= acc) { chosen = c; break; }
            }
            /* If chosen class has zero, fallback to uniform */
            StdDeck_CardMask b;
            if (g_ctx.pair_counts[chosen] == 0 || !pe_board_sample_pairclass(dead, num_cards_to_draw, (pe_bpair_class_t)chosen, &b)) {
                mc_uniform_draw_cards(dead, num_cards_to_draw, &b);
            }
            batch->boards[i] = b;
        }
        g_ctx.seq_index += (uint64_t)batch_size;
    } else {
        for (int i = 0; i < batch_size; i++) {
            if (g_ctx.policy == PE_SAMPLING_IMPORTANCE_BOARD_AWARE) {
                /* Acceptance-rejection: propose uniform, accept with prob f/fmax */
                while (1) {
                    StdDeck_CardMask b;
                    mc_uniform_draw_cards(dead, num_cards_to_draw, &b);
                    double f = board_class_score(b, num_cards_to_draw);
                    double u = ((double) (rand())) / ((double)RAND_MAX + 1.0);
                    if (u < (f / k_importance_fmax)) {
                        batch->boards[i] = b;
                        break;
                    }
                }
        } else if (g_ctx.policy == PE_SAMPLING_QMC_HALTON) {
            qmc_halton_draw_cards(dead, num_cards_to_draw, g_ctx.seq_index + (uint64_t)i, &batch->boards[i]);
        } else if (g_ctx.policy == PE_SAMPLING_QMC_SOBOL) {
            qmc_sobol_draw_cards(dead, num_cards_to_draw, g_ctx.seq_index + (uint64_t)i, &batch->boards[i]);
        } else {
            mc_uniform_draw_cards(dead, num_cards_to_draw, &batch->boards[i]);
        }
        }
        g_ctx.seq_index += (uint64_t)batch_size;
    }
}
