/*
 * strat_tuner.c - Quick tuner for STRATIFIED (board-aware) weights
 * Usage (simple):
 *   strat_tuner <w_distinct> <w_onepair> <w_trips> [scenario] [samples] [repeats]
 *   scenario ∈ {preflop,flop,turn,river,both,all} (default: flop)
 * Usage (options):
 *   strat_tuner --w=1,1.75,3 --scenario=flop --samples=10000 --repeats=3 \
 *               --p1=As,Ah --p2=Kd,Kh --board=Ac,2h,3s[,4d[,5c]] --batch=256
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/sampling_policies.h>

static void mask_from_cards(StdDeck_CardMask* out, const char* const* cards, int n) {
    StdDeck_CardMask_RESET(*out);
    for (int i = 0; i < n; i++) {
        if (!cards[i]) continue;
        int c = -1; char tmp[8];
        snprintf(tmp, sizeof(tmp), "%s", cards[i]);
        if (StdDeck_stringToCard(tmp, &c) == 0 && c >= 0) {
            StdDeck_CardMask_SET(*out, c);
        }
    }
}

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct {
    const char* name;
    const char* p1_cards[2];
    const char* p2_cards[2];
    const char* board_cards[5];
    int nboard;
} scenario_t;

typedef enum {
    POL_MC = 0,
    POL_QMC,
    POL_IMPORTANCE,
    POL_STRATIFIED
} policy_t;

static const char* policy_name(policy_t p) {
    switch (p) {
        case POL_MC: return "MC";
        case POL_QMC: return "QMC-Halton";
        case POL_IMPORTANCE: return "IMPORTANCE";
        case POL_STRATIFIED: return "STRATIFIED";
        default: return "UNKNOWN";
    }
}

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage (simple):\n  %s <w_distinct> <w_onepair> <w_trips> [scenario] [samples] [repeats]\n"
        "  scenario ∈ {preflop,flop,turn,river,both,all} (default: flop)\n\n"
        "Usage (options):\n  %s --w=1,1.75,3 --scenario=flop --samples=10000 --repeats=3 \\\n+     --p1=As,Ah --p2=Kd,Kh --board=Ac,2h,3s[,4d[,5c]] --batch=256\n",
        prog, prog);
}

static int parse_cards_csv(const char* s, const char** out_cards, int max_cards) {
    if (!s) return 0;
    static char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    int count = 0;
    char* token = strtok(buf, ",");
    while (token && count < max_cards) {
        while (*token == ' ' || *token == '\t') token++;
        out_cards[count++] = token;
        token = strtok(NULL, ",");
    }
    return count;
}

static int parse_cards_any(const char* s, const char** out_cards, int max_cards) {
    if (!s) return 0;
    static char buf[256];
    int len = (int)strlen(s);
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, s, (size_t)len); buf[len] = '\0';
    for (int i = 0; buf[i]; ++i) if (buf[i] == ',') buf[i] = ' ';
    int count = 0;
    char* token = strtok(buf, " \t");
    while (token && count < max_cards) {
        out_cards[count++] = token;
        token = strtok(NULL, " \t");
    }
    return count;
}

static int parse_policy_list(const char* s, policy_t* out, int max_out) {
    if (!s) return 0;
    char buf[128]; snprintf(buf, sizeof(buf), "%s", s);
    int n = 0;
    for (char* p = buf; *p; ++p) if (*p == ';') *p = ',';
    char* tok = strtok(buf, ",");
    while (tok && n < max_out) {
        for (char* q = tok; *q; ++q) *q = (char)toupper((unsigned char)*q);
        if (strcmp(tok, "MC") == 0) out[n++] = POL_MC;
        else if (strcmp(tok, "QMC") == 0 || strcmp(tok, "QMC-HALTON") == 0) out[n++] = POL_QMC;
        else if (strcmp(tok, "IMPORTANCE") == 0) out[n++] = POL_IMPORTANCE;
        else if (strcmp(tok, "STRATIFIED") == 0) out[n++] = POL_STRATIFIED;
        else if (strcmp(tok, "ALL") == 0) { out[0]=POL_MC; out[1]=POL_QMC; out[2]=POL_IMPORTANCE; out[3]=POL_STRATIFIED; return 4; }
        tok = strtok(NULL, ",");
    }
    return n;
}

static void run_scenario(const scenario_t* sc, int samples, int repeats, int batch_size,
                         double w0, double w1, double w2,
                         const policy_t* policies, int npolicies) {
    StdDeck_CardMask p1, p2, board, dead;
    mask_from_cards(&p1, sc->p1_cards, 2);
    mask_from_cards(&p2, sc->p2_cards, 2);
    mask_from_cards(&board, sc->board_cards, sc->nboard);
    StdDeck_CardMask_RESET(dead);

    for (int ip = 0; ip < npolicies; ++ip) {
        policy_t pol = policies[ip];
        switch (pol) {
            case POL_MC:
                pe_sampling_set_policy(PE_SAMPLING_MC_UNIFORM);
                break;
            case POL_QMC:
                pe_sampling_set_policy(PE_SAMPLING_QMC_HALTON);
                break;
            case POL_IMPORTANCE:
                pe_sampling_set_policy(PE_SAMPLING_IMPORTANCE_BOARD_AWARE);
                break;
            case POL_STRATIFIED: {
                pe_sampling_set_policy(PE_SAMPLING_STRATIFIED_BOARD_AWARE);
                double weights[3] = { w0, w1, w2 };
                pe_sampling_set_pair_weights(weights);
                break;
            }
            default:
                pe_sampling_set_policy(PE_SAMPLING_MC_UNIFORM);
                break;
        }

        for (int r = 0; r < repeats; r++) {
            pe_sampling_reset((uint64_t)r * (uint64_t)samples);
            BatchedMonteCarlo_SetRandomSeed(1234u + (unsigned)r);
            double t0 = now_sec();
            BatchedMonteCarloResult R = BatchedMonteCarlo_CalculateEquity(p1, p2, board, dead, samples, batch_size);
            double t1 = now_sec();
            printf("%s,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,%.3f\n",
                   policy_name(pol), sc->name, samples, r, batch_size,
                   R.equity[0], R.equity[1], R.tie_percentage, (t1 - t0), w0, w1, w2);
        }
    }
}

static int process_input_csv(const char* path, int samples, int repeats, int batch_size,
                             double w0, double w1, double w2,
                             const policy_t* policies, int npolicies) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[error] cannot open %s\n", path); return 1; }
    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char* p = line; while (*p==' '||*p=='\t') ++p; if (*p=='#' || *p=='\n' || *p=='\0') continue;
        for (char* q=p; *q; ++q) { if (*q=='\r' || *q=='\n') { *q='\0'; break; } }
        /* columns: p1,p2,board[,scenario] */
        char* tok[8] = {0}; int ntok=0; char* cur = strtok(p, ",");
        while (cur && ntok<8) { tok[ntok++] = cur; cur = strtok(NULL, ","); }
        if (ntok < 3) { fprintf(stderr, "[warn] line %d: expected p1,p2,board[,scenario]\n", lineno); continue; }
        const char* p1v[2]={0,0}; const char* p2v[2]={0,0}; const char* bcv[5]={0};
        int n1 = parse_cards_any(tok[0], p1v, 2);
        int n2 = parse_cards_any(tok[1], p2v, 2);
        int nb = parse_cards_any(tok[2], bcv, 5);
        if (n1!=2 || n2!=2) { fprintf(stderr, "[warn] line %d: invalid p1/p2\n", lineno); continue; }
        scenario_t sc = {0}; sc.name = (ntok>=4?tok[3]:"case");
        sc.p1_cards[0]=p1v[0]; sc.p1_cards[1]=p1v[1];
        sc.p2_cards[0]=p2v[0]; sc.p2_cards[1]=p2v[1];
        for (int i=0;i<nb && i<5;i++) sc.board_cards[i]=bcv[i]; sc.nboard=nb;
        run_scenario(&sc, samples, repeats, batch_size, w0,w1,w2, policies, npolicies);
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    double w0 = 1.0, w1 = 1.75, w2 = 3.0;
    const char* scenario = "flop";
    int samples = 10000, repeats = 3, batch_size = 256;
    int weights_set = 0, custom = 0;
    const char* in_path = NULL;
    policy_t pols_sel[4]; int npols_sel = 0;
    const char* p1_arg = NULL; const char* p2_arg = NULL; const char* board_arg = NULL;

    /* Backward compatible positional parsing for first 3 args if numeric */
    int pos = 1;
    if (argc > pos && strchr(argv[pos], '-') == NULL && strchr(argv[pos], '=') == NULL) { w0 = atof(argv[pos++]); weights_set = 1; }
    if (argc > pos && strchr(argv[pos], '-') == NULL && strchr(argv[pos], '=') == NULL) { w1 = atof(argv[pos++]); weights_set = 1; }
    if (argc > pos && strchr(argv[pos], '-') == NULL && strchr(argv[pos], '=') == NULL) { w2 = atof(argv[pos++]); weights_set = 1; }
    if (argc > pos && argv[pos][0] && argv[pos][0] != '-') scenario = argv[pos++];
    if (argc > pos && argv[pos][0] && argv[pos][0] != '-') samples = atoi(argv[pos++]);
    if (argc > pos && argv[pos][0] && argv[pos][0] != '-') repeats = atoi(argv[pos++]);

    /* Options parsing */
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (strncmp(a, "--w=", 4) == 0) {
            double a0=0,a1=0,a2=0; if (sscanf(a+4, "%lf,%lf,%lf", &a0,&a1,&a2) == 3) { w0=a0; w1=a1; w2=a2; weights_set=1; }
        } else if (strncmp(a, "--scenario=", 11) == 0) {
            scenario = a+11;
        } else if (strncmp(a, "--samples=", 10) == 0) {
            samples = atoi(a+10);
        } else if (strncmp(a, "--repeats=", 10) == 0) {
            repeats = atoi(a+10);
        } else if (strncmp(a, "--batch=", 8) == 0) {
            batch_size = atoi(a+8);
        } else if (strncmp(a, "--p1=", 5) == 0) {
            p1_arg = a+5; custom = 1;
        } else if (strncmp(a, "--p2=", 5) == 0) {
            p2_arg = a+5; custom = 1;
        } else if (strncmp(a, "--board=", 8) == 0) {
            board_arg = a+8; custom = 1;
        } else if (strncmp(a, "--in=", 5) == 0) {
            in_path = a+5;
        } else if (strncmp(a, "--policy=", 9) == 0 || strncmp(a, "--policies=", 11) == 0) {
            const char* v = (a[2]=='p' && a[3]=='o' && a[4]=='l' && a[5]=='i' && a[6]=='c' && a[7]=='y' && a[8]=='=') ? (a+9) : (a+11);
            npols_sel = parse_policy_list(v, pols_sel, 4);
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!weights_set) {
        /* If still not set, keep defaults but print hint */
        fprintf(stderr, "[info] using default weights: %.3f,%.3f,%.3f\n", w0,w1,w2);
    }

    const scenario_t preflop = {"preflop", {"As","Ah"}, {"Kd","Kh"}, {NULL,NULL,NULL,NULL,NULL}, 0};
    const scenario_t flop    = {"flop",    {"As","Ah"}, {"Kd","Kh"}, {"Ac","2h","3s",NULL,NULL}, 3};
    const scenario_t turn    = {"turn",    {"As","Ah"}, {"Kd","Kh"}, {"Ac","2h","3s","4d",NULL}, 4};
    const scenario_t river   = {"river",   {"As","Ah"}, {"Kd","Kh"}, {"Ac","2h","3s","4d","5c"}, 5};

    if (npols_sel == 0) { pols_sel[0] = POL_STRATIFIED; npols_sel = 1; }

    /* CSV header */
    printf("policy,scenario,samples,repeat,batch_size,equity_p1,equity_p2,tie,time_sec,w_distinct,w_onepair,w_trips\n");
    if (in_path) {
        return process_input_csv(in_path, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
    }

    if (custom) {
        scenario_t sc = {0};
        sc.name = "custom";
        const char* tmp_p1[2] = {NULL,NULL};
        const char* tmp_p2[2] = {NULL,NULL};
        const char* tmp_board[5] = {NULL,NULL,NULL,NULL,NULL};
        int n1 = p1_arg ? parse_cards_any(p1_arg, tmp_p1, 2) : 0;
        int n2 = p2_arg ? parse_cards_any(p2_arg, tmp_p2, 2) : 0;
        int nb = board_arg ? parse_cards_any(board_arg, tmp_board, 5) : 0;
        if (n1 != 2 || n2 != 2) {
            fprintf(stderr, "[error] --p1 and --p2 must provide exactly 2 cards each.\n");
            return 2;
        }
        char p1buf[2][8] = {{0}}; char p2buf[2][8] = {{0}}; char bbuf[5][8] = {{0}};
        for (int i=0;i<2;i++) if (tmp_p1[i]) { strncpy(p1buf[i], tmp_p1[i], sizeof(p1buf[i])-1); }
        for (int i=0;i<2;i++) if (tmp_p2[i]) { strncpy(p2buf[i], tmp_p2[i], sizeof(p2buf[i])-1); }
        for (int i=0;i<nb && i<5;i++) if (tmp_board[i]) { strncpy(bbuf[i], tmp_board[i], sizeof(bbuf[i])-1); }
        sc.p1_cards[0] = p1buf[0]; sc.p1_cards[1] = p1buf[1];
        sc.p2_cards[0] = p2buf[0]; sc.p2_cards[1] = p2buf[1];
        for (int i = 0; i < nb && i < 5; ++i) sc.board_cards[i] = bbuf[i];
        sc.nboard = nb;
        run_scenario(&sc, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
        return 0;
    }

    if (strcmp(scenario, "all") == 0) {
        run_scenario(&preflop, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
        run_scenario(&flop,    samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
        run_scenario(&turn,    samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
        run_scenario(&river,   samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
    } else if (strcmp(scenario, "both") == 0) {
        run_scenario(&flop, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
        run_scenario(&turn, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
    } else if (strcmp(scenario, "preflop") == 0) {
        run_scenario(&preflop, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
    } else if (strcmp(scenario, "turn") == 0) {
        run_scenario(&turn, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
    } else if (strcmp(scenario, "river") == 0) {
        run_scenario(&river, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
    } else if (strcmp(scenario, "flop") == 0) {
        run_scenario(&flop, samples, repeats, batch_size, w0, w1, w2, pols_sel, npols_sel);
    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
