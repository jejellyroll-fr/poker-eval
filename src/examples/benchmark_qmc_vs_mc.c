/*
 * benchmark_qmc_vs_mc.c - Micro-benchmark comparing QMC (Halton) vs MC uniform
 * Outputs CSV on stdout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/sampling_policies.h>

/* Forward decl for runtime weight tuning */
static void maybe_set_strat_weights_from_env(void);

static void mask_from_cards(StdDeck_CardMask* out, const char* const* cards, int n) {
    StdDeck_CardMask_RESET(*out);
    for (int i = 0; i < n; i++) {
        int c = -1;
        /* StdDeck_stringToCard expects non-const */
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%s", cards[i]);
        if (StdDeck_stringToCard(tmp, &c) == 0 && c >= 0) {
            StdDeck_CardMask_SET(*out, c);
        }
    }
}

static double now_sec(void) {
    /* Portable wall-clock timer */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct {
    const char* name;
    const char* p1_cards[2];
    const char* p2_cards[2];
    const char* board_cards[5];
    int nboard;
} scenario_t;

int main(int argc, char** argv) {
    /* Scénarios: preflop, flop, turn */
    const scenario_t scenarios[] = {
        {"preflop", {"As","Ah"}, {"Kd","Kh"}, {NULL,NULL,NULL,NULL,NULL}, 0},
        {"flop",    {"As","Ah"}, {"Kd","Kh"}, {"Ac","2h","3s",NULL,NULL}, 3},
        {"turn",    {"As","Ah"}, {"Kd","Kh"}, {"Ac","2h","3s","4d",NULL}, 4},
    };
    const int nsc = (int)(sizeof(scenarios)/sizeof(scenarios[0]));

    int samples_list[] = { 10000, 50000 };
    int nsamp = (int)(sizeof(samples_list)/sizeof(samples_list[0]));
    int repeats = 5;
    int batch_size = 256; /* match BATCH_SIZE */

    /* CSV header */
    printf("policy,scenario,samples,repeat,batch_size,equity_p1,equity_p2,tie,time_sec\n");

    /* Allow runtime tuning of stratification weights via env */
    maybe_set_strat_weights_from_env();

    for (int s = 0; s < nsc; s++) {
        StdDeck_CardMask p1, p2, board, dead;
        mask_from_cards(&p1, scenarios[s].p1_cards, 2);
        mask_from_cards(&p2, scenarios[s].p2_cards, 2);
        mask_from_cards(&board, scenarios[s].board_cards, scenarios[s].nboard);
        StdDeck_CardMask_RESET(dead);

        for (int k = 0; k < nsamp; k++) {
            int samples = samples_list[k];

            /* MC uniform */
            pe_sampling_set_policy(PE_SAMPLING_MC_UNIFORM);
            for (int r = 0; r < repeats; r++) {
                BatchedMonteCarlo_SetRandomSeed(1234u + (unsigned)r);
                pe_sampling_reset(0);
                double t0 = now_sec();
                enum_result_t E = {0};
                enumSampleBatched(game_holdem, (StdDeck_CardMask[]){p1,p2}, board, dead, 2, StdDeck_numCards(board), samples, 0, &E);
                double t1 = now_sec();
                double eq0 = 0.0, eq1 = 0.0, tie = 0.0;
                {
                    double denom = (E.nsamples > 1 ? (double)E.nsamples : (double)samples);
                    if (denom > 0) {
                        eq0 = E.ev[0] / denom;
                        eq1 = E.ev[1] / denom;
                        tie = 1.0 - eq0 - eq1;
                    }
                }
                printf("MC,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",
                    scenarios[s].name, samples, r, batch_size,
                    eq0, eq1, tie, (t1 - t0));
            }

            /* QMC Halton */
            pe_sampling_set_policy(PE_SAMPLING_QMC_HALTON);
            for (int r = 0; r < repeats; r++) {
                /* Décaler légèrement la séquence pour pseudo-réplications */
                pe_sampling_reset((uint64_t)r * (uint64_t)samples);
                double t0 = now_sec();
                enum_result_t E = {0};
                enumSampleBatched(game_holdem, (StdDeck_CardMask[]){p1,p2}, board, dead, 2, StdDeck_numCards(board), samples, 0, &E);
                double t1 = now_sec();
                double eq0 = 0.0, eq1 = 0.0, tie = 0.0;
                {
                    double denom = (E.nsamples > 1 ? (double)E.nsamples : (double)samples);
                    if (denom > 0) {
                        eq0 = E.ev[0] / denom;
                        eq1 = E.ev[1] / denom;
                        tie = 1.0 - eq0 - eq1;
                    }
                }
                printf("QMC-Halton,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",
                    scenarios[s].name, samples, r, batch_size,
                    eq0, eq1, tie, (t1 - t0));
            }

            /* QMC Sobol (scrambled VDC-like) */
            pe_sampling_set_policy(PE_SAMPLING_QMC_SOBOL);
            for (int r = 0; r < repeats; r++) {
                pe_sampling_reset((uint64_t)r * (uint64_t)samples);
                double t0 = now_sec();
                enum_result_t E = {0};
                enumSampleBatched(game_holdem, (StdDeck_CardMask[]){p1,p2}, board, dead, 2, StdDeck_numCards(board), samples, 0, &E);
                double t1 = now_sec();
                double eq0 = 0.0, eq1 = 0.0, tie = 0.0;
                {
                    double denom = (E.nsamples > 1 ? (double)E.nsamples : (double)samples);
                    if (denom > 0) {
                        eq0 = E.ev[0] / denom;
                        eq1 = E.ev[1] / denom;
                        tie = 1.0 - eq0 - eq1;
                    }
                }
                printf("QMC-Sobol,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",
                    scenarios[s].name, samples, r, batch_size,
                    eq0, eq1, tie, (t1 - t0));
            }

            /* IMPORTANCE (board-aware) */
            pe_sampling_set_policy(PE_SAMPLING_IMPORTANCE_BOARD_AWARE);
            for (int r = 0; r < repeats; r++) {
                pe_sampling_reset((uint64_t)r * (uint64_t)samples);
                double t0 = now_sec();
                BatchedMonteCarloResult R = BatchedMonteCarlo_CalculateEquity(p1, p2, board, dead, samples, batch_size);
                double t1 = now_sec();
                printf("IMPORTANCE,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",
                    scenarios[s].name, samples, r, batch_size,
                    R.equity[0], R.equity[1], R.tie_percentage, (t1 - t0));
            }

            /* STRATIFIED (board-aware, analytic PDF) */
            pe_sampling_set_policy(PE_SAMPLING_STRATIFIED_BOARD_AWARE);
            for (int r = 0; r < repeats; r++) {
                pe_sampling_reset((uint64_t)r * (uint64_t)samples);
                double t0 = now_sec();
                BatchedMonteCarloResult R = BatchedMonteCarlo_CalculateEquity(p1, p2, board, dead, samples, batch_size);
                double t1 = now_sec();
                printf("STRATIFIED,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",
                    scenarios[s].name, samples, r, batch_size,
                    R.equity[0], R.equity[1], R.tie_percentage, (t1 - t0));
            }
        }
    }

    return 0;
}
static void maybe_set_strat_weights_from_env(void) {
    const char* env = getenv("PE_STRAT_WEIGHTS");
    if (!env || !*env) return;
    double w[3] = {1.0, 1.75, 3.0};
    /* Accept formats like "1,1.75,3" or "1 1.75 3" */
    char buf[128]; snprintf(buf, sizeof(buf), "%s", env);
    for (char* p = buf; *p; ++p) { if (*p == ',') *p = ' '; }
    double a=0,b=0,c=0; int n = sscanf(buf, "%lf %lf %lf", &a, &b, &c);
    if (n == 3) { w[0]=a; w[1]=b; w[2]=c; }
    pe_sampling_set_pair_weights(w);
}
