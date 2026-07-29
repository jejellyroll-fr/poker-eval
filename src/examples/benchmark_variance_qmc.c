/*
 * benchmark_variance_qmc.c - Compare variance/error vs exhaustive for sampling policies
 * Scenario: Hold'em HU with flop connu; on complète turn/river et on compare
 * MC, QMC-Halton, QMC-Sobol, IMPORTANCE, STRATIFIED.
 *
 * CSV: policy,samples,repeat,estimate_p1,error,time_sec
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/sampling_policies.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/time_compat.h>

#if defined(_WIN32)
static char* pe_strsep(char** stringp, const char* delim) {
    char* s = stringp ? *stringp : NULL;
    char* p;
    if (!s) return NULL;
    p = s;
    while (*p && !strchr(delim, *p)) {
        ++p;
    }
    if (*p) {
        *p = '\0';
        *stringp = p + 1;
    } else {
        *stringp = NULL;
    }
    return s;
}
#else
#define pe_strsep strsep
#endif

typedef struct {
    const char* name;
    const char* p1_cards[2];
    const char* p2_cards[2];
    const char* board_cards[5];
    int board_n;   /* 0..5 */
    int players;   /* 2..ENUM_MAXPLAYERS */
} scenario_t;

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

static char* strtrim(char* s) {
    if (!s) return s;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static int split_cards_field(const char* field, char* out_cards[], int max_cards) {
    int count = 0;
    if (!field || !*field) return 0;
    char* tmp = strdup(field);
    char* p = tmp;
    char* tok;
    while ((tok = pe_strsep(&p, " ")) != NULL) {
        tok = strtrim(tok);
        if (*tok == '\0') continue;
        if (count < max_cards) {
            out_cards[count++] = strdup(tok);
        }
    }
    free(tmp);
    return count;
}

static scenario_t* load_scenarios_csv(const char* path, int* out_n) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    const int MAX = 512;
    scenario_t* arr = (scenario_t*)calloc(MAX, sizeof(scenario_t));
    int n = 0;
    char line[512];
    /* Read header */
    if (!fgets(line, sizeof(line), f)) { fclose(f); free(arr); return NULL; }
    while (fgets(line, sizeof(line), f)) {
        if (n >= MAX) break;
        char* s = line;
        char* f1 = pe_strsep(&s, ",");
        char* f2 = pe_strsep(&s, ",");
        char* f3 = pe_strsep(&s, ",");
        char* f4 = pe_strsep(&s, ",");
        char* f5 = pe_strsep(&s, ",\n");
        if (!f1 || !f2) continue;
        char *p1=NULL,*p2=NULL,*board=NULL,*scen=NULL; int players=2;
        if (f5) {
            players = atoi(strtrim(f1));
            p1 = strtrim(f2);
            p2 = strtrim(f3);
            board = f4 ? strtrim(f4) : NULL;
            scen = f5 ? strtrim(f5) : NULL;
        } else {
            p1 = strtrim(f1);
            p2 = strtrim(f2);
            board = f3 ? strtrim(f3) : NULL;
            scen = f4 ? strtrim(f4) : NULL;
            players = 2;
        }
        const char* scenario_name = (scen && *scen != '\0') ? scen : "scenario_csv";
        arr[n].name = strdup(scenario_name);
        arr[n].p1_cards[0] = arr[n].p1_cards[1] = NULL;
        arr[n].p2_cards[0] = arr[n].p2_cards[1] = NULL;
        arr[n].board_cards[0] = arr[n].board_cards[1] = arr[n].board_cards[2] = arr[n].board_cards[3] = arr[n].board_cards[4] = NULL;
        arr[n].players = (players >= 2 && players <= ENUM_MAXPLAYERS) ? players : 2;
        char* p1_cards[8] = {0};
        char* p2_cards[8] = {0};
        char* b_cards[8] = {0};
        int n1 = split_cards_field(p1, p1_cards, 2);
        int n2 = split_cards_field(p2, p2_cards, 2);
        int nb = split_cards_field(board, b_cards, 5);
        if (n1 != 2 || n2 != 2) {
            /* invalid row */
            for (int i=0;i<n1;i++) free(p1_cards[i]);
            for (int i=0;i<n2;i++) free(p2_cards[i]);
            for (int i=0;i<nb;i++) free(b_cards[i]);
            continue;
        }
        arr[n].p1_cards[0] = p1_cards[0];
        arr[n].p1_cards[1] = p1_cards[1];
        arr[n].p2_cards[0] = p2_cards[0];
        arr[n].p2_cards[1] = p2_cards[1];
        arr[n].board_n = nb;
        for (int i=0;i<nb && i<5;i++) arr[n].board_cards[i] = b_cards[i];
        n++;
    }
    fclose(f);
    *out_n = n;
    return arr;
}

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* scenario_t defined above */

static double run_exhaustive(const scenario_t* sc) {
    StdDeck_CardMask p1, p2, board, dead;
    enum_result_t E = {0};
    mask_from_cards(&p1, sc->p1_cards, 2);
    mask_from_cards(&p2, sc->p2_cards, 2);
    mask_from_cards(&board, sc->board_cards, sc->board_n);
    StdDeck_CardMask_RESET(dead);
    
    /* Build pockets for N players (deterministic extras) */
    int players = (sc->players >= 2 && sc->players <= ENUM_MAXPLAYERS) ? sc->players : 2;
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    for (int i=0;i<players;i++) StdDeck_CardMask_RESET(pockets[i]);
    pockets[0]=p1; pockets[1]=p2;
    if (players > 2) {
        StdDeck_CardMask used; StdDeck_CardMask_RESET(used);
        StdDeck_CardMask_OR(used, used, p1);
        StdDeck_CardMask_OR(used, used, p2);
        StdDeck_CardMask_OR(used, used, board);
        unsigned long long st = 1469598103934665603ULL;
        const char* s = sc->name ? sc->name : "";
        while (*s) { st ^= (unsigned char)(*s++); st *= 1099511628211ULL; }
        for (int pi=2; pi<players; ++pi) {
            int c1=-1, c2=-1;
            for (int k=0;k<2;k++) {
                for (int guard=0; guard<2000; ++guard) {
                    st = st*2862933555777941757ULL + 3037000493ULL;
                    int c = (int)((st >> 33) % StdDeck_N_CARDS);
                    if (!StdDeck_CardMask_CARD_IS_SET(used, c)) {
                        if (k==0) c1 = c; else c2 = c;
                        StdDeck_CardMask_SET(used, c);
                        break;
                    }
                }
            }
            if (c1>=0) StdDeck_CardMask_SET(pockets[pi], c1);
            if (c2>=0) StdDeck_CardMask_SET(pockets[pi], c2);
        }
    }
    int err = enumExhaustive_dispatch(game_holdem, pockets, board, dead, players, sc->board_n, 0, &E);
    if (err != 0 || E.nsamples <= 0) return -1.0;
    return E.ev[0] / (double)E.nsamples;
}

static double run_policy_samples(pe_sampling_policy_t pol, int samples, const scenario_t* sc) {
    StdDeck_CardMask p1, p2, board, dead;
    mask_from_cards(&p1, sc->p1_cards, 2);
    mask_from_cards(&p2, sc->p2_cards, 2);
    mask_from_cards(&board, sc->board_cards, sc->board_n);
    StdDeck_CardMask_RESET(dead);
    
    if (pol == PE_SAMPLING_IMPORTANCE_BOARD_AWARE || pol == PE_SAMPLING_QMC_SOBOL || pol == PE_SAMPLING_QMC_HALTON || pol == PE_SAMPLING_STRATIFIED_BOARD_AWARE)
        pe_sampling_set_policy(pol);
    else
        pe_sampling_set_policy(PE_SAMPLING_MC_UNIFORM);

    int players = (sc->players >= 2 && sc->players <= ENUM_MAXPLAYERS) ? sc->players : 2;
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
    for (int i=0;i<players;i++) StdDeck_CardMask_RESET(pockets[i]);
    pockets[0]=p1; pockets[1]=p2;
    if (players > 2) {
        StdDeck_CardMask used; StdDeck_CardMask_RESET(used);
        StdDeck_CardMask_OR(used, used, p1);
        StdDeck_CardMask_OR(used, used, p2);
        StdDeck_CardMask_OR(used, used, board);
        unsigned long long st = 1469598103934665603ULL;
        const char* s = sc->name ? sc->name : "";
        while (*s) { st ^= (unsigned char)(*s++); st *= 1099511628211ULL; }
        for (int pi=2; pi<players; ++pi) {
            int c1=-1, c2=-1;
            for (int k=0;k<2;k++) {
                for (int guard=0; guard<2000; ++guard) {
                    st = st*2862933555777941757ULL + 3037000493ULL;
                    int c = (int)((st >> 33) % StdDeck_N_CARDS);
                    if (!StdDeck_CardMask_CARD_IS_SET(used, c)) {
                        if (k==0) c1 = c; else c2 = c;
                        StdDeck_CardMask_SET(used, c);
                        break;
                    }
                }
            }
            if (c1>=0) StdDeck_CardMask_SET(pockets[pi], c1);
            if (c2>=0) StdDeck_CardMask_SET(pockets[pi], c2);
        }
    }
    enum_result_t E = {0};
    int err = enumSampleBatched(game_holdem, pockets, board, dead, players, sc->board_n, samples, 0, &E);
    if (err != 0 || E.nsamples <= 0) return -1.0;
    return E.ev[0] / (double)E.nsamples;
}

static const char* pol_name(pe_sampling_policy_t p) {
    switch (p) {
        case PE_SAMPLING_MC_UNIFORM: return "MC";
        case PE_SAMPLING_QMC_HALTON: return "QMC-Halton";
        case PE_SAMPLING_QMC_SOBOL: return "QMC-Sobol";
        case PE_SAMPLING_IMPORTANCE_BOARD_AWARE: return "IMPORTANCE";
        case PE_SAMPLING_STRATIFIED_BOARD_AWARE: return "STRATIFIED";
        default: return "UNKNOWN";
    }
}

int main(int argc, char** argv) {
    const char* cases_csv = NULL;
    int repeats = 20;
    int samples_list[] = { 200, 500, 1000 };
    int nsamp = (int)(sizeof(samples_list)/sizeof(samples_list[0]));
    /* quick CLI parsing */
    for (int ai=1; ai<argc; ++ai) {
        if (strcmp(argv[ai], "--cases") == 0 && ai+1 < argc) {
            cases_csv = argv[++ai];
        } else if (strcmp(argv[ai], "--repeats") == 0 && ai+1 < argc) {
            repeats = atoi(argv[++ai]);
            if (repeats < 1) repeats = 1;
        }
    }
    /* Multi‑scénarios non symétriques */
    const scenario_t scenarios_builtin[] = {
        /* Dry flops (haute carte + low kickers) */
        {"dry_A72_rainbow_AKo_vs_77", {"Ad","Ks"}, {"7d","7c"}, {"As","7h","2c", NULL, NULL}, 3, 2},
        {"dry_K82_rainbow_KQs_vs_99", {"Kd","Qs"}, {"9d","9c"}, {"Kh","8c","2d", NULL, NULL}, 3, 2},
        /* Très connectés / flush-heavy */
        {"wet_T98_monotone_AKo_vs_9c9d", {"As","Kh"}, {"9c","9d"}, {"Tc","9s","8c", NULL, NULL}, 3, 2},
        {"wet_JT9_twotone_QhJh_vs_Td9d", {"Qh","Jh"}, {"Td","9d"}, {"Js","Ts","9c", NULL, NULL}, 3, 2},
        /* Paired & drawy */
        {"paired_KK2_QsQh_vs_KdTd", {"Qs","Qh"}, {"Kd","Td"}, {"Kc","Ks","2d", NULL, NULL}, 3, 2},
        /* Cas initiaux */
        {"flop_QhJd_vs_9c9d", {"Qh","Jd"}, {"9c","9d"}, {"9s","Ts","2c", NULL, NULL}, 3, 2},
        {"flop_AdKc_vs_7d7c", {"Ad","Kc"}, {"7d","7c"}, {"9h","2c","4s", NULL, NULL}, 3, 2},
        {"flop_AsAh_vs_KdKh", {"As","Ah"}, {"Kd","Kh"}, {"8h","2d","3c", NULL, NULL}, 3, 2},
        {"flop_QsQh_vs_JdTd", {"Qs","Qh"}, {"Jd","Td"}, {"9c","8c","2d", NULL, NULL}, 3, 2},
        /* Flush draw très lourd (nut FD + overcards) */
        {"fd_heavy_AhQh_vs_KdKs_on_Th9h2c", {"Ah","Qh"}, {"Kd","Ks"}, {"Th","9h","2c", NULL, NULL}, 3, 2},
        /* OESD + nut FD vs overpair */
        {"fd_heavy_KhQh_vs_AsAd_on_JhTh2s", {"Kh","Qh"}, {"As","Ad"}, {"Jh","Th","2s", NULL, NULL}, 3, 2},
        /* Two pair floppé vs overcards AK */
        {"twopair_QcJd_vs_AsKd_on_QhJh2c", {"Qc","Jd"}, {"As","Kd"}, {"Qh","Jh","2c", NULL, NULL}, 3, 2},
        /* Trips vs backdoor (straight + flush) */
        {"trips_99_vs_AcKc_backdoor_on_9c2d3s", {"9h","9s"}, {"Ac","Kc"}, {"9c","2d","3s", NULL, NULL}, 3, 2},
    };
    const int nsc_builtin = (int)(sizeof(scenarios_builtin)/sizeof(scenarios_builtin[0]));
    const scenario_t* scenarios = scenarios_builtin;
    int nsc = nsc_builtin;
    scenario_t* scenarios_csv = NULL;
    if (cases_csv) {
        int ncsv = 0;
        scenarios_csv = load_scenarios_csv(cases_csv, &ncsv);
        if (scenarios_csv && ncsv > 0) {
            scenarios = scenarios_csv;
            nsc = ncsv;
        }
    }

    printf("policy,scenario,samples,repeat,estimate_p1,error,time_sec\n");
    pe_sampling_policy_t policies[] = {
        PE_SAMPLING_MC_UNIFORM,
        PE_SAMPLING_QMC_HALTON,
        PE_SAMPLING_QMC_SOBOL,
        PE_SAMPLING_IMPORTANCE_BOARD_AWARE,
        PE_SAMPLING_STRATIFIED_BOARD_AWARE
    };
    int np = (int)(sizeof(policies)/sizeof(policies[0]));

    for (int s = 0; s < nsc; ++s) {
        double ex_ev = run_exhaustive(&scenarios[s]);
        if (ex_ev < 0.0) {
            fprintf(stderr, "[error] exhaustive failed for %s\n", scenarios[s].name);
            continue;
        }
        for (int ip = 0; ip < np; ++ip) {
            for (int k = 0; k < nsamp; ++k) {
                int samples = samples_list[k];
                for (int r = 0; r < repeats; ++r) {
                    /* Décaler séquence + reseeder le RNG pour MC/IMPORTANCE */
                    pe_sampling_reset((uint64_t)r * (uint64_t)samples + (uint64_t)(s+1)*9973ULL);
                    unsigned seed = (unsigned)(r + samples * 1315423911u + (unsigned)(s*2654435761u) + (unsigned)policies[ip]*486187739u);
                    srand(seed);
                    double t0 = now_sec();
                    double est = run_policy_samples(policies[ip], samples, &scenarios[s]);
                    double t1 = now_sec();
                    double err = (est >= 0.0) ? fabs(est - ex_ev) : -1.0;
                    printf("%s,%s,%d,%d,%.6f,%.6f,%.6f\n",
                           pol_name(policies[ip]), scenarios[s].name, samples, r,
                           est, err, (t1 - t0));
                }
            }
        }
    }

    free(scenarios_csv);
    return 0;
}
