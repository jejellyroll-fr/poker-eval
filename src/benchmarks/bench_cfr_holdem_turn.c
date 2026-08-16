/*
 * bench_cfr_holdem_turn.c - MCCFR on Hold'em HU turn (sample river)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/core/eval_context.h>
#include <math.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/holdem_turn_adapter.h>
#include <poker_eval/engine/solvers/cfr/strength_bucketing.h>
#include <poker_eval/engine/solvers/cfr/board_texture.h>

typedef struct
{
    FILE *f;
    int bucket_mode;
    int n_bet;
    double fracs[4];
    int first;
    double ev_root;
    double ev_local;
} dump_ctx_t;
static void dump_cb(uint64_t key, int n, const double *regret, const double *avg, void *u)
{
    (void)regret;
    dump_ctx_t *C = (dump_ctx_t *)u;
    unsigned p = (unsigned)((key >> 4) & 1ull);
    unsigned raises_left = (unsigned)(key & 0xFull);
    uint64_t act = (key >> 16);
    unsigned to_call = (unsigned)(act & 1ull);
    unsigned hist = (unsigned)((act >> 8) & 0xFFFFu);
    unsigned b_cls = (unsigned)((key >> 56) & 0xFull);
    unsigned p_cls = (unsigned)((key >> 52) & 0xFull);
    unsigned coarse = (unsigned)((key >> 48) & 0xFull);
    unsigned turn_feats = (unsigned)((key >> 40) & 0xFFu);
    char feats_lbl[64];
    feats_lbl[0] = '\0';
    if (turn_feats & 1u)
        strncat(feats_lbl, feats_lbl[0] ? "|pair" : "pair", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & 2u)
        strncat(feats_lbl, feats_lbl[0] ? "|fdraw" : "fdraw", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & 4u)
        strncat(feats_lbl, feats_lbl[0] ? "|sdraw" : "sdraw", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & (1u << 5))
        strncat(feats_lbl, feats_lbl[0] ? "|trips" : "trips", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & (1u << 6))
        strncat(feats_lbl, feats_lbl[0] ? "|quads" : "quads", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & 4u)
    {
        if (turn_feats & (1u << 7))
            strncat(feats_lbl, feats_lbl[0] ? "|open" : "open", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
        else
            strncat(feats_lbl, feats_lbl[0] ? "|closed" : "closed", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    }
    unsigned priv_feats = (turn_feats >> 3) & 0x3u;
    char priv_lbl[64];
    priv_lbl[0] = '\0';
    if (priv_feats & 1u)
        strncat(priv_lbl, priv_lbl[0] ? "|blk_flush" : "blk_flush", sizeof(priv_lbl) - strlen(priv_lbl) - 1);
    if (priv_feats & 2u)
        strncat(priv_lbl, priv_lbl[0] ? "|blk_straight" : "blk_straight", sizeof(priv_lbl) - strlen(priv_lbl) - 1);
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += avg[i];
    char buf[1024];
    buf[0] = '\0';
    double H = 0.0;
    for (int i = 0; i < n; i++)
    {
        char abuf[128];
        double pr = sum > 0 ? avg[i] / sum : 1.0 / n;
        if (pr > 0)
            H -= pr * log(pr);
        if (!to_call)
        {
            if (i == 0)
                snprintf(abuf, sizeof(abuf), "check:%.4f", pr);
            else
            {
                int idx = i - 1;
                if (idx >= 0 && idx < C->n_bet)
                {
                    int frac = (int)(C->fracs[idx] * 100.0 + 0.5);
                    snprintf(abuf, sizeof(abuf), "bet_%d%%:%.4f", frac, pr);
                }
                else
                    snprintf(abuf, sizeof(abuf), "bet:%.4f", pr);
            }
        }
        else
        {
            if (i == 0)
                snprintf(abuf, sizeof(abuf), "call:%.4f", pr);
            else if (i == 1)
                snprintf(abuf, sizeof(abuf), "fold:%.4f", pr);
            else
            {
                int idx = i - 2;
                if (idx >= 0 && idx < C->n_bet)
                {
                    int frac = (int)(C->fracs[idx] * 100.0 + 0.5);
                    snprintf(abuf, sizeof(abuf), "raise_%d%%:%.4f", frac, pr);
                }
                else
                    snprintf(abuf, sizeof(abuf), "raise:%.4f", pr);
            }
        }
        if (i > 0)
            strncat(buf, "|", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, abuf, sizeof(buf) - strlen(buf) - 1);
    }
    const char *bname = eval_hand_class_name((hand_class_t)b_cls);
    const char *pname = eval_hand_class_name((hand_class_t)p_cls);
    {
        unsigned priv_feats_row = (turn_feats >> 3) & 0x3u;
        char priv_lbl_row[64];
        priv_lbl_row[0] = '\0';
        if (priv_feats_row & 1u)
            strncat(priv_lbl_row, priv_lbl_row[0] ? "|blk_flush" : "blk_flush", sizeof(priv_lbl_row) - strlen(priv_lbl_row) - 1);
        if (priv_feats_row & 2u)
            strncat(priv_lbl_row, priv_lbl_row[0] ? "|blk_straight" : "blk_straight", sizeof(priv_lbl_row) - strlen(priv_lbl_row) - 1);
        fprintf(C->f, "%llu,%u,%s,%s,%u,%u,%s,%u,%s,0x%04X,%u,%u,%d,%s,%.6f,%.6f,%.6f,%.6f\n",
                (unsigned long long)key, p, bname ? bname : "-", pname ? pname : "-", (C->bucket_mode == 3) ? coarse : 0u,
                turn_feats, feats_lbl, priv_feats_row, priv_lbl_row, hist, to_call, raises_left, n, buf, sum, H, C->ev_root, C->ev_local);
    }
}

static void dump_json_cb(uint64_t key, int n, const double *regret, const double *avg, void *u)
{
    (void)regret;
    dump_ctx_t *C = (dump_ctx_t *)u;
    unsigned p = (unsigned)((key >> 4) & 1ull);
    unsigned raises_left = (unsigned)(key & 0xFull);
    uint64_t act = (key >> 16);
    unsigned to_call = (unsigned)(act & 1ull);
    unsigned hist = (unsigned)((act >> 8) & 0xFFFFu);
    unsigned b_cls = (unsigned)((key >> 56) & 0xFull);
    unsigned p_cls = (unsigned)((key >> 52) & 0xFull);
    unsigned coarse = (unsigned)((key >> 48) & 0xFull);
    unsigned turn_feats = (unsigned)((key >> 40) & 0xFFu);
    char feats_lbl[64];
    feats_lbl[0] = '\0';
    if (turn_feats & 1u)
        strncat(feats_lbl, feats_lbl[0] ? "|pair" : "pair", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & 2u)
        strncat(feats_lbl, feats_lbl[0] ? "|fdraw" : "fdraw", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & 4u)
        strncat(feats_lbl, feats_lbl[0] ? "|sdraw" : "sdraw", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & (1u << 5))
        strncat(feats_lbl, feats_lbl[0] ? "|trips" : "trips", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & (1u << 6))
        strncat(feats_lbl, feats_lbl[0] ? "|quads" : "quads", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    if (turn_feats & 4u)
    {
        if (turn_feats & (1u << 7))
            strncat(feats_lbl, feats_lbl[0] ? "|open" : "open", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
        else
            strncat(feats_lbl, feats_lbl[0] ? "|closed" : "closed", sizeof(feats_lbl) - strlen(feats_lbl) - 1);
    }
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += avg[i];
    double H = 0.0;
    for (int i = 0; i < n; i++)
    {
        double pr = sum > 0 ? avg[i] / sum : 1.0 / n;
        if (pr > 0)
            H -= pr * log(pr);
    }
    const char *bname = eval_hand_class_name((hand_class_t)b_cls);
    const char *pname = eval_hand_class_name((hand_class_t)p_cls);
    if (!C->first)
        fprintf(C->f, ",\n");
    C->first = 0;
    {
        unsigned priv_feats_json = (turn_feats >> 3) & 0x3u;
        char priv_lbl_json[64];
        priv_lbl_json[0] = '\0';
        if (priv_feats_json & 1u)
            strncat(priv_lbl_json, priv_lbl_json[0] ? "|blk_flush" : "blk_flush", sizeof(priv_lbl_json) - strlen(priv_lbl_json) - 1);
        if (priv_feats_json & 2u)
            strncat(priv_lbl_json, priv_lbl_json[0] ? "|blk_straight" : "blk_straight", sizeof(priv_lbl_json) - strlen(priv_lbl_json) - 1);
        fprintf(C->f, "    {\"key\": %llu, \"player\": %u, \"board_cls\": \"%s\", \"private_cls\": \"%s\", \"coarse_bin\": %u, \"turn_feats\": %u, \"turn_feats_labels\": \"%s\", \"priv_feats\": %u, \"priv_feats_labels\": \"%s\", \"hist\": \"0x%04X\", \"to_call\": %u, \"raises_left\": %u, \"mass\": %.6f, \"entropy\": %.6f, \"ev_root\": %.6f, \"ev_local\": %.6f, \"actions\": [",
                (unsigned long long)key, p, bname ? bname : "-", pname ? pname : "-", (C->bucket_mode == 3) ? coarse : 0u,
                turn_feats, feats_lbl, priv_feats_json, priv_lbl_json, hist, to_call, raises_left, sum, H, C->ev_root, C->ev_local);
    }
    for (int i = 0; i < n; i++)
    {
        double pr = sum > 0 ? avg[i] / sum : 1.0 / n;
        const char *label = NULL;
        char tmp[32];
        if (!to_call)
        {
            if (i == 0)
                label = "check";
            else
            {
                int idx = i - 1;
                if (idx >= 0 && idx < C->n_bet)
                {
                    int frac = (int)(C->fracs[idx] * 100.0 + 0.5);
                    snprintf(tmp, sizeof(tmp), "bet_%d%%", frac);
                    label = tmp;
                }
                else
                    label = "bet";
            }
        }
        else
        {
            if (i == 0)
                label = "call";
            else if (i == 1)
                label = "fold";
            else
            {
                int idx = i - 2;
                if (idx >= 0 && idx < C->n_bet)
                {
                    int frac = (int)(C->fracs[idx] * 100.0 + 0.5);
                    snprintf(tmp, sizeof(tmp), "raise_%d%%", frac);
                    label = tmp;
                }
                else
                    label = "raise";
            }
        }
        fprintf(C->f, "%s{\"label\":\"%s\",\"p\":%.6f}", (i ? "," : ""), label, pr);
    }
    fprintf(C->f, "]}");
}

/* Top-level bridges to emit local EV in dumps */
static void dump_cb_stats_bridge(uint64_t key,
                                 int n,
                                 const double *reg,
                                 const double *avg,
                                 double ev_sum,
                                 double ev_sq_sum,
                                 uint64_t sample_count,
                                 void *u)
{
    (void)ev_sq_sum;
    dump_ctx_t *C = (dump_ctx_t *)u;
    dump_ctx_t C2 = *C;
    C2.ev_local = (sample_count > 0) ? (ev_sum / (double)sample_count) : 0.0;
    dump_cb(key, n, reg, avg, &C2);
}
static void dump_json_cb_stats_bridge(uint64_t key,
                                      int n,
                                      const double *reg,
                                      const double *avg,
                                      double ev_sum,
                                      double ev_sq_sum,
                                      uint64_t sample_count,
                                      void *u)
{
    (void)ev_sq_sum;
    dump_ctx_t *C = (dump_ctx_t *)u;
    dump_ctx_t C2 = *C;
    C2.ev_local = (sample_count > 0) ? (ev_sum / (double)sample_count) : 0.0;
    dump_json_cb(key, n, reg, avg, &C2);
}

static void sample_random_turn(unsigned seed, mask_t *h0, mask_t *h1, mask_t *board4)
{
    bool used[52] = {0};
    int cards[8];
    int cnt = 0;
    srand(seed);
    while (cnt < 8)
    {
        int c = rand() % 52;
        if (!used[c])
        {
            used[c] = true;
            cards[cnt++] = c;
        }
    }
    *h0 = MASK_EMPTY;
    *h1 = MASK_EMPTY;
    *board4 = MASK_EMPTY;
    *h0 = mask_set(*h0, cards[0]);
    *h0 = mask_set(*h0, cards[1]);
    *h1 = mask_set(*h1, cards[2]);
    *h1 = mask_set(*h1, cards[3]);
    for (int i = 4; i < 8; i++)
        *board4 = mask_set(*board4, cards[i]);
}

static int read_turn_deal_line(const char *line, mask_t *h0, mask_t *h1, mask_t *board4)
{
    int c[8];
    int n = sscanf(line, "%d %d %d %d %d %d %d %d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7]);
    if (n != 8)
        return 0;
    *h0 = MASK_EMPTY;
    *h1 = MASK_EMPTY;
    *board4 = MASK_EMPTY;
    *h0 = mask_set(*h0, c[0]);
    *h0 = mask_set(*h0, c[1]);
    *h1 = mask_set(*h1, c[2]);
    *h1 = mask_set(*h1, c[3]);
    for (int i = 4; i < 8; i++)
        *board4 = mask_set(*board4, c[i]);
    return 1;
}

int main(int argc, char **argv)
{
    EvalConfig cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
    {
        fprintf(stderr, "ctx create failed\n");
        return 1;
    }

    int deals = 50;
    int iters = 1000;
    const char *csv = NULL;
    const char *dealset = NULL;
    unsigned seed = 12345u;
    int bucket_mode = 3;
    int bucket_bins = 8;
    /* FEAT-13 (#190/#192): strength buckets + texture filter abstraction. */
    int buckets_per_street = 0; /* 0 = disabled */
    int texture_filter = 0;     /* pe_texture_filter_level_t, 0 = disabled */
    const char *bucket_thresh = NULL;
    int csv_append = 0;
    int use_dcfr = 0;
    int use_ecfr = 0;
    double a = 1.5, b = 0.0, g = 2.0;
    double ecfr_lambda = 1.0;
    const char *strat_readable = NULL;
    const char *strat_json = NULL;
    const char *bet_sizes = NULL;
    const char *tree_profile = NULL;
    int raise_cap = -1;
    int verbose = 0;
    int progress_interval = 0;
    int trace_cfr = 0;
    int use_mccfvfp = 0;
    double flow_pow = 1.0;
    const char *checkpoint_path = NULL;
    const char *resume_path = NULL;
    int checkpoint_final = 0;
    int checkpoint_interval = 0;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--deals") && i + 1 < argc)
            deals = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--iters") && i + 1 < argc)
            iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc)
            csv = argv[++i];
        else if (!strcmp(argv[i], "--dealset") && i + 1 < argc)
            dealset = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--bucket-mode") && i + 1 < argc)
            bucket_mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bucket-bins") && i + 1 < argc)
            bucket_bins = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bucket-thresholds") && i + 1 < argc)
            bucket_thresh = argv[++i];
        else if (!strcmp(argv[i], "--buckets-per-street") && i + 1 < argc)
            buckets_per_street = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--texture-filter") && i + 1 < argc)
            texture_filter = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dcfr"))
            use_dcfr = 1;
        else if (!strcmp(argv[i], "--ecfr"))
            use_ecfr = 1;
        else if (!strcmp(argv[i], "--dcfr-alpha") && i + 1 < argc)
            a = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--dcfr-beta") && i + 1 < argc)
            b = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--dcfr-gamma") && i + 1 < argc)
            g = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--ecfr-lambda") && i + 1 < argc)
            ecfr_lambda = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--csv-append"))
            csv_append = 1;
        else if (!strcmp(argv[i], "--strat-json") && i + 1 < argc)
            strat_json = argv[++i];
        else if (!strcmp(argv[i], "--strat-readable") && i + 1 < argc)
            strat_readable = argv[++i];
        else if (!strcmp(argv[i], "--bet-sizes") && i + 1 < argc)
            bet_sizes = argv[++i];
        else if (!strcmp(argv[i], "--raise-cap") && i + 1 < argc)
            raise_cap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tree-profile") && i + 1 < argc)
            tree_profile = argv[++i];
        else if (!strcmp(argv[i], "--progress") && i + 1 < argc)
            progress_interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verbose"))
            verbose = 1;
    else if (!strcmp(argv[i], "--trace-cfr"))
        trace_cfr = 1;
        else if (!strcmp(argv[i], "--mccfvfp"))
            use_mccfvfp = 1;
        else if (!strcmp(argv[i], "--flow-pow") && i + 1 < argc)
            flow_pow = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--checkpoint") && i + 1 < argc)
            checkpoint_path = argv[++i];
        else if (!strcmp(argv[i], "--resume") && i + 1 < argc)
            resume_path = argv[++i];
        else if (!strcmp(argv[i], "--checkpoint-final"))
            checkpoint_final = 1;
        else if (!strcmp(argv[i], "--checkpoint-interval") && i + 1 < argc)
            checkpoint_interval = atoi(argv[++i]);
}

    if (!tree_profile || !*tree_profile)
        tree_profile = getenv("CFR_TREE_PROFILE");
    if (!tree_profile || !*tree_profile)
        tree_profile = "tight";

if (!use_mccfvfp)
    use_mccfvfp = getenv("CFR_MCCFVFP") && *getenv("CFR_MCCFVFP");
if (fabs(flow_pow - 1.0) < 1e-9)
{
    const char *env_flow = getenv("CFR_MCCFVFP_FLOW");
    if (env_flow && *env_flow)
        flow_pow = strtod(env_flow, NULL);
}
if (!checkpoint_path)
{
    const char *env_chk = getenv("CFR_CHECKPOINT");
    if (env_chk && *env_chk)
        checkpoint_path = env_chk;
}
if (!resume_path)
{
    const char *env_res = getenv("CFR_RESUME");
    if (env_res && *env_res)
        resume_path = env_res;
}
    if (!checkpoint_final)
    {
        const char *env_final = getenv("CFR_CHECKPOINT_FINAL");
        if (env_final && *env_final && strcmp(env_final, "0") != 0)
            checkpoint_final = 1;
    }
    if (checkpoint_interval == 0)
    {
        const char *env_chkint = getenv("CFR_CHECKPOINT_INTERVAL");
        if (env_chkint && *env_chkint)
            checkpoint_interval = atoi(env_chkint);
    }

    if (tree_profile && *tree_profile && strcmp(tree_profile, "none"))
    {
        const char *profile_bet = NULL;
        int profile_cap = -1;
        if (!strcmp(tree_profile, "tight"))
        {
            profile_bet = "50";
            profile_cap = 1;
        }
        else if (!strcmp(tree_profile, "standard") || !strcmp(tree_profile, "balanced"))
        {
            profile_bet = "33,66";
            profile_cap = 2;
        }
        else if (!strcmp(tree_profile, "wide"))
        {
            profile_bet = "33,66,100";
            profile_cap = 2;
        }
        if (!bet_sizes && profile_bet)
            bet_sizes = profile_bet;
        if (raise_cap < 0 && profile_cap >= 0)
            raise_cap = profile_cap;
    }

    FILE *fcsv = NULL;
    if (csv)
    {
        fcsv = fopen(csv, csv_append ? "a" : "w");
        if (fcsv && !csv_append)
            fprintf(fcsv, "deal_idx,iters,time_sec,proxy,infosets,bucket_mode,bucket_bins,thresh_count\n");
    }
    double total_time = 0.0;
    pe_strength_table_t *stable = NULL;
    FILE *fds = NULL;
    if (dealset)
    {
        fds = fopen(dealset, "r");
        if (!fds)
        {
            fprintf(stderr, "Failed to open dealset %s\n", dealset);
            return 1;
        }
    }
    char line[256];
    for (int d = 0; d < deals; ++d)
    {
        mask_t h0, h1, b4;
        if (fds)
        {
            if (!fgets(line, sizeof(line), fds) || !read_turn_deal_line(line, &h0, &h1, &b4))
            {
                fprintf(stderr, "Invalid dealset line %d\n", d);
                break;
            }
        }
        else
        {
            sample_random_turn(seed + d * 131u, &h0, &h1, &b4);
        }
        cfr_game_t game;
        ht_state_t st;
        ht_build_game_sampled_river(ctx, h0, h1, b4, &game, &st, seed + d * 313u);
        st.river_state.bucket_mode = (unsigned char)bucket_mode;
        st.river_state.bucket_bins = (unsigned char)(bucket_bins > 0 ? (bucket_bins > 16 ? 16 : bucket_bins) : 8);
        if (bucket_thresh && *bucket_thresh)
        {
            st.river_state.bucket_thresh_count = 0;
            const char *p = bucket_thresh;
            while (*p && st.river_state.bucket_thresh_count < 16)
            {
                char *endp = NULL;
                long v = strtol(p, &endp, 10);
                if (endp == p)
                    break;
                if (v < 0)
                    v = 0;
                if (v > 999999)
                    v = 999999;
                st.river_state.bucket_thresh[st.river_state.bucket_thresh_count++] = (uint32_t)v;
                if (*endp == ',')
                    p = endp + 1;
                else
                    p = endp;
            }
            for (int i = 1; i < st.river_state.bucket_thresh_count; i++)
                if (st.river_state.bucket_thresh[i] < st.river_state.bucket_thresh[i - 1])
                    st.river_state.bucket_thresh[i] = st.river_state.bucket_thresh[i - 1];
        }
        /* FEAT-13 (#190/#192): strength buckets (EHS/EHS2 k-means) + board-texture
         * merging. The strength table is trained on the river board (5 cards) of
         * this deal; with --shared-storage different deals that reach
         * texture-equivalent river boards collapse onto shared infosets. */
        stable = NULL;
        if (bucket_mode == 5 || bucket_mode == 7)
        {
            pe_strength_cluster_opts_t sopts;
            memset(&sopts, 0, sizeof(sopts));
            sopts.hole_cards = 2; /* hold'em */
            sopts.seed = seed + d * 313u;
            int k = buckets_per_street > 0 ? buckets_per_street : 30;
            stable = pe_strength_table_train_all(ctx, st.river_state.board, &sopts, &k);
            if (!stable)
                fprintf(stderr, "deal %d: strength clustering failed, falling back to bucket_mode 3\n", d);
            else
                st.river_state.strength_table = stable;
        }
        if (bucket_mode == 6 || bucket_mode == 7)
            st.river_state.texture_level = texture_filter;
        /* apply bet sizes/raise cap if provided */
        if (bet_sizes)
        {
            int n = 0;
            double fr[4] = {0};
            const char *p = bet_sizes;
            while (*p && n < 4)
            {
                char *ep = NULL;
                long v = strtol(p, &ep, 10);
                if (ep == p)
                    break;
                if (v <= 0)
                    v = 1;
                if (v > 1000)
                    v = 1000;
                fr[n++] = ((double)v) / 100.0;
                p = (*ep == ',') ? ep + 1 : ep;
            }
            if (n > 0)
            {
                st.river_state.num_bet_sizes = n;
                for (int i = 0; i < n; i++)
                    st.river_state.bet_fracs[i] = fr[i];
            }
        }
        if (raise_cap >= 0)
        {
            st.river_state.raise_cap = raise_cap;
        }

        /* With --shared-storage a single storage is reused across deals so
         * texture-merging (bucket_mode 6) collapses infosets that arise on
         * texture-equivalent river boards reached from different turn deals. */
        cfr_storage_t *storage = cfr_storage_create();
        cfr_config_t c = {0};
        c.max_iterations = iters;
        c.enable_dcfr = use_dcfr;
        c.dcfr_alpha = a;
        c.dcfr_beta = b;
        c.dcfr_gamma = g;
        c.enable_ecfr = use_ecfr;
        c.ecfr_lambda = ecfr_lambda;
        c.enable_mccfvfp = use_mccfvfp;
        c.mccfvfp_flow_pow = flow_pow;
        c.checkpoint_path = checkpoint_path;
        c.resume_path = resume_path;
        c.checkpoint_final = checkpoint_final;
        c.checkpoint_interval = checkpoint_interval;
        if (progress_interval > 0)
            c.progress_interval = progress_interval;
        else if (verbose)
            c.progress_interval = 100;
        if (trace_cfr)
            c.trace_iterations = 1;
        clock_t s = clock();
        double exploitability = 0.0;
        double proxy = cfr_solve(&game, storage, &c, &exploitability);
        clock_t e = clock();
        double t = (double)(e - s) / CLOCKS_PER_SEC;
        total_time += t;
        size_t infos = cfr_storage_count_infosets(storage);
        if (fcsv)
            fprintf(fcsv, "%d,%d,%.6f,%.6f,%zu,%d,%d,%d\n", d, iters, t, proxy, infos, bucket_mode, bucket_bins, st.river_state.bucket_thresh_count);
        if (deals > 0)
        {
            double progress = (double)(d + 1) / (double)deals;
            double est_total = (progress > 0.0) ? (total_time / progress) : 0.0;
            double eta = est_total - total_time;
            if (eta < 0.0)
                eta = 0.0;
            fprintf(stderr, "[turn ] deal %d/%d  elapsed %.2fs  est %.2fs  eta %.2fs\n",
                    d + 1, deals, total_time, est_total, eta);
            fflush(stderr);
        }

        if (d == 0)
        {
            /* Compute policy values for both players */
            double policy_ev_p0 = cfr_compute_policy_value(&game, storage, 0, game.game_data);
            double policy_ev_p1 = cfr_compute_policy_value(&game, storage, 1, game.game_data);

            if (strat_readable)
            {
                FILE *fr = fopen(strat_readable, "w");
                if (fr)
                {
                    fprintf(fr, "key,player,board_cls,private_cls,coarse_bin,turn_feats,turn_feats_labels,priv_feats,priv_feats_labels,hist_hex,to_call,raises_left,n_actions,actions,mass,entropy,ev_root,ev_local\n");
                    dump_ctx_t ctxd = {fr, bucket_mode, 4, {1.0 / 3.0, 0.5, 0.75, 1.0}, 1, 0.0, 0.0};
                    ctxd.ev_root = policy_ev_p0;  /* Use computed policy value for player 0 */
                    /* iterate with local EV emission using a simple non-nested bridge */
                    cfr_storage_iterate_stats(storage, dump_cb_stats_bridge, &ctxd);
                    fclose(fr);
                }
            }
            if (strat_json)
            {
                FILE *fj = fopen(strat_json, "w");
                if (fj)
                {
                    fprintf(fj, "{\n  \"key_version\": 1, \"bucket_mode\": %d, \"policy_ev_p0\": %.6f, \"policy_ev_p1\": %.6f, \"entries\": [\n",
                            bucket_mode, policy_ev_p0, policy_ev_p1);
                    dump_ctx_t ctx = {fj, bucket_mode, 4, {1.0 / 3.0, 0.5, 0.75, 1.0}, 1, 0.0, 0.0};
                    ctx.ev_root = policy_ev_p0;  /* Use computed policy value for player 0 */
                    cfr_storage_iterate_stats(storage, dump_json_cb_stats_bridge, &ctx);
                    fprintf(fj, "\n  ]\n}\n");
                    fclose(fj);
                }
            }
        }
        cfr_storage_destroy(storage);
        pe_strength_table_free(stable);
        st.river_state.strength_table = NULL;
    }
    if (fds)
        fclose(fds);
    if (fcsv)
        fclose(fcsv);
    if (deals > 0)
    {
        fprintf(stderr, "[turn ] completed in %.2fs\n", total_time);
        fflush(stderr);
    }
    printf("CFR turn HU demo: deals=%d, iters=%d/deal, total_time=%.3f s\n", deals, iters, total_time);
    eval_context_destroy(ctx);
    return 0;
}
