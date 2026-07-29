/*
 * bench_cfr_stud_river.c - MCCFR on 7-Stud High HU river (fixed deal)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/stud_river_adapter.h>

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
    fprintf(C->f, "%llu,%u,%s,%s,%u,0x%04X,%u,%u,%d,%s,%.6f,%.6f,%.6f,%.6f\n",
            (unsigned long long)key, p, bname ? bname : "-", pname ? pname : "-", (C->bucket_mode == 3) ? coarse : 0u,
            hist, to_call, raises_left, n, buf, sum, H, C->ev_root, C->ev_local);
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
    fprintf(C->f, "    {\"key\": %llu, \"player\": %u, \"board_cls\": \"%s\", \"private_cls\": \"%s\", \"coarse_bin\": %u, \"hist\": \"0x%04X\", \"to_call\": %u, \"raises_left\": %u, \"mass\": %.6f, \"entropy\": %.6f, \"ev_root\": %.6f, \"ev_local\": %.6f, \"actions\": [",
            (unsigned long long)key, p, bname ? bname : "-", pname ? pname : "-", (C->bucket_mode == 3) ? coarse : 0u,
            hist, to_call, raises_left, sum, H, C->ev_root, C->ev_local);

    for (int i = 0; i < n; i++)
    {
        char abuf[128];
        double pr = sum > 0 ? avg[i] / sum : 1.0 / n;
        if (!to_call)
        {
            if (i == 0)
                snprintf(abuf, sizeof(abuf), "{\"label\":\"check\",\"p\":%.6f}", pr);
            else
            {
                int idx = i - 1;
                if (idx >= 0 && idx < C->n_bet)
                {
                    int frac = (int)(C->fracs[idx] * 100.0 + 0.5);
                    snprintf(abuf, sizeof(abuf), "{\"label\":\"bet_%d%%\",\"p\":%.6f}", frac, pr);
                }
                else
                    snprintf(abuf, sizeof(abuf), "{\"label\":\"bet\",\"p\":%.6f}", pr);
            }
        }
        else
        {
            if (i == 0)
                snprintf(abuf, sizeof(abuf), "{\"label\":\"call\",\"p\":%.6f}", pr);
            else if (i == 1)
                snprintf(abuf, sizeof(abuf), "{\"label\":\"fold\",\"p\":%.6f}", pr);
            else
            {
                int idx = i - 2;
                if (idx >= 0 && idx < C->n_bet)
                {
                    int frac = (int)(C->fracs[idx] * 100.0 + 0.5);
                    snprintf(abuf, sizeof(abuf), "{\"label\":\"raise_%d%%\",\"p\":%.6f}", frac, pr);
                }
                else
                    snprintf(abuf, sizeof(abuf), "{\"label\":\"raise\",\"p\":%.6f}", pr);
            }
        }
        fprintf(C->f, "%s%s", (i > 0) ? "," : "", abuf);
    }
    fprintf(C->f, "]}");
}

static void dump_cb_stats(uint64_t key,
                          int n,
                          const double *regret,
                          const double *avg,
                          double ev_sum,
                          double ev_sq_sum,
                          uint64_t sample_count,
                          void *u)
{
    (void)ev_sq_sum;
    dump_ctx_t ctx2 = *(dump_ctx_t *)u;
    ctx2.ev_root = (sample_count > 0) ? (ev_sum / (double)sample_count) : 0.0;
    dump_cb(key, n, regret, avg, &ctx2);
}

static void dump_json_cb_stats(uint64_t key,
                               int n,
                               const double *regret,
                               const double *avg,
                               double ev_sum,
                               double ev_sq_sum,
                               uint64_t sample_count,
                               void *u)
{
    (void)ev_sq_sum;
    dump_ctx_t ctx2 = *(dump_ctx_t *)u;
    ctx2.ev_root = (sample_count > 0) ? (ev_sum / (double)sample_count) : 0.0;
    dump_json_cb(key, n, regret, avg, &ctx2);
}

static int read_stud_deal_line(const char *line, mask_t *h0, mask_t *h1)
{
    int idx[14];
    int n = 0;
    const char *p = line;
    char *ep = NULL;
    while (*p && n < 14)
    {
        long v = strtol(p, &ep, 10);
        if (ep == p)
            break;
        idx[n++] = (int)v;
        p = (*ep == ',') ? ep + 1 : ep;
    }
    if (n != 14)
        return 0;
    *h0 = MASK_EMPTY;
    *h1 = MASK_EMPTY;
    for (int i = 0; i < 7; i++)
        *h0 = mask_set(*h0, idx[i]);
    for (int i = 7; i < 14; i++)
        *h1 = mask_set(*h1, idx[i]);
    return 1;
}

int main(int argc, char **argv)
{
    EvalConfig cfg = eval_config_stud();
    EvalContext *ctx = eval_context_create(&cfg);
    if (!ctx)
    {
        fprintf(stderr, "ctx create failed\n");
        return 1;
    }

    int deals = 50;
    int iters = 1000;
    const char *csv = NULL;
    unsigned seed = 12345u;
    const char *strat_csv = NULL;
    const char *strat_readable = NULL;
    const char *strat_json = NULL;
    const char *dealset = NULL;
    int bucket_mode = 3;
    int bucket_bins = 8;
    const char *bucket_thresh = NULL;
    int csv_append = 0;
    int use_dcfr = 0;
    int use_ecfr = 0;
    double a = 1.5, b = 0.0, g = 2.0;
    double ecfr_lambda = 1.0;
    const char *bet_sizes = NULL;
    int raise_cap = -1;
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
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--strat-csv") && i + 1 < argc)
            strat_csv = argv[++i];
        else if (!strcmp(argv[i], "--strat-readable") && i + 1 < argc)
            strat_readable = argv[++i];
        else if (!strcmp(argv[i], "--strat-json") && i + 1 < argc)
            strat_json = argv[++i];
        else if (!strcmp(argv[i], "--dealset") && i + 1 < argc)
            dealset = argv[++i];
        else if (!strcmp(argv[i], "--bucket-mode") && i + 1 < argc)
            bucket_mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bucket-bins") && i + 1 < argc)
            bucket_bins = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bucket-thresholds") && i + 1 < argc)
            bucket_thresh = argv[++i];
        else if (!strcmp(argv[i], "--csv-append"))
            csv_append = 1;
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
        else if (!strcmp(argv[i], "--bet-sizes") && i + 1 < argc)
            bet_sizes = argv[++i];
        else if (!strcmp(argv[i], "--raise-cap") && i + 1 < argc)
            raise_cap = atoi(argv[++i]);
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

    FILE *fcsv = NULL;
    if (csv)
    {
        fcsv = fopen(csv, csv_append ? "a" : "w");
        if (fcsv && !csv_append)
            fprintf(fcsv, "deal_idx,iters,time_sec,proxy,infosets,bucket_mode,bucket_bins,thresh_count\n");
    }

    double total_time = 0.0;
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
        mask_t h0, h1;
        h0 = MASK_EMPTY;
        h1 = MASK_EMPTY;

        if (fds)
        {
            if (!fgets(line, sizeof(line), fds) || !read_stud_deal_line(line, &h0, &h1))
            {
                fprintf(stderr, "Invalid dealset line %d\n", d);
                break;
            }
        }
        else
        {
            /* Sample random distinct 14 cards for Stud (7 each) */
            srand(seed + d * 101u);
            int used[52] = {0};
            int arr[14];
            int k = 0;
            while (k < 14)
            {
                int c = rand() % 52;
                if (!used[c])
                {
                    used[c] = 1;
                    arr[k++] = c;
                }
            }
            for (int i = 0; i < 7; i++)
                h0 = mask_set(h0, arr[i]);
            for (int i = 7; i < 14; i++)
                h1 = mask_set(h1, arr[i]);
        }

        cfr_game_t game;
        stud_state_t st;
        stud_build_game(ctx, h0, h1, &game, &st);
        st.bucket_mode = (unsigned char)bucket_mode;
        st.bucket_bins = (unsigned char)(bucket_bins > 0 ? (bucket_bins > 16 ? 16 : bucket_bins) : 8);
        if (bucket_thresh && *bucket_thresh)
        {
            /* parse comma-separated integer thresholds (0..999999) */
            st.bucket_thresh_count = 0;
            const char *p = bucket_thresh;
            while (*p && st.bucket_thresh_count < 16)
            {
                char *endp = NULL;
                long v = strtol(p, &endp, 10);
                if (endp == p)
                    break;
                if (v < 0)
                    v = 0;
                if (v > 999999)
                    v = 999999;
                st.bucket_thresh[st.bucket_thresh_count++] = (uint32_t)v;
                if (*endp == ',')
                    p = endp + 1;
                else
                    p = endp;
            }
            /* ensure thresholds are non-decreasing */
            for (int i = 1; i < st.bucket_thresh_count; i++)
                if (st.bucket_thresh[i] < st.bucket_thresh[i - 1])
                    st.bucket_thresh[i] = st.bucket_thresh[i - 1];
        }
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
        /* apply bet sizes/raise cap if provided */
        if (bet_sizes)
        {
            int n = 0;
            double fracs[4] = {0};
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
                fracs[n++] = ((double)v) / 100.0;
                p = (*ep == ',') ? ep + 1 : ep;
            }
            if (n > 0)
            {
                st.n_bet_sizes = n;
                for (int i = 0; i < n; i++)
                    st.bet_fracs[i] = fracs[i];
            }
        }
        if (raise_cap >= 0)
        {
            st.raise_cap = raise_cap;
        }
        clock_t s = clock();
        double exploitability = 0.0;
        double proxy = cfr_solve(&game, storage, &c, &exploitability);
        clock_t e = clock();
        double t = (double)(e - s) / CLOCKS_PER_SEC;
        total_time += t;
        size_t infos = cfr_storage_count_infosets(storage);
        if (fcsv)
            fprintf(fcsv, "%d,%d,%.6f,%.6f,%zu,%d,%d,%d\n", d, iters, t, proxy, infos, bucket_mode, bucket_bins, st.bucket_thresh_count);

        if (d == 0)
        {
            /* Compute policy values for both players */
            double policy_ev_p0 = cfr_compute_policy_value(&game, storage, 0, game.game_data);
            double policy_ev_p1 = cfr_compute_policy_value(&game, storage, 1, game.game_data);

            if (strat_csv)
            {
                FILE *fs = fopen(strat_csv, "w");
                if (fs)
                {
                    cfr_storage_dump_avg(storage, fs);
                    fclose(fs);
                }
            }
            if (strat_readable)
            {
                FILE *fr = fopen(strat_readable, "w");
                if (fr)
                {
                    fprintf(fr, "key,player,board_cls,private_cls,coarse_bin,hist_hex,to_call,raises_left,n_actions,actions,mass,entropy,ev_root,ev_local\n");
                    dump_ctx_t ctx_dump = {fr, bucket_mode, 4, {1.0 / 3.0, 0.5, 0.75, 1.0}, 1, 0.0, 0.0};
                    ctx_dump.ev_root = policy_ev_p0;
                    cfr_storage_iterate_stats(storage, dump_cb_stats, &ctx_dump);
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
                    dump_ctx_t ctx_dump = {fj, bucket_mode, 4, {1.0 / 3.0, 0.5, 0.75, 1.0}, 1, 0.0, 0.0};
                    ctx_dump.ev_root = policy_ev_p0;
                    cfr_storage_iterate_stats(storage, dump_json_cb_stats, &ctx_dump);
                    fprintf(fj, "\n  ]\n}\n");
                    fclose(fj);
                }
            }
        }
        cfr_storage_destroy(storage);
    }

    if (fds)
        fclose(fds);
    if (fcsv)
        fclose(fcsv);

    printf("CFR 7-Stud High HU river: deals=%d, iters=%d/deal, total_time=%.3f s\n", deals, iters, total_time);
    eval_context_destroy(ctx);
    return 0;
}
