/*
 * paytable_ev.c - Paytable EV / House Edge / variance engine.
 *
 * Standard video poker paytables use the published optimal-strategy outcome
 * probabilities (combinations) from Wizard of Odds:
 *   - 9/6 Jacks or Better          (EV = 0.995439)
 *   - Full Pay Deuces Wild         (EV = 1.007620)
 *   - Joker Poker - Kings or Better( EV = 1.006463)
 */

#include <poker_eval/economics/paytable_ev.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

int pe_paytable_compute_ev(const double *payout_matrix,
                           const double *probability_matrix,
                           int num_outcomes,
                           pe_paytable_result_t *out_result)
{
    if (!payout_matrix || !probability_matrix)
        return -1;
    if (num_outcomes < 1 || num_outcomes > PE_PAYTABLE_MAX_ROWS)
        return -1;

    /*
     * Sanity check: entries must be finite, payouts non-negative and
     * probabilities within [0, 1].  The isfinite() guards are required because
     * NaN compares false against every bound, so a NaN would otherwise slip
     * through both the per-entry and the prob_sum checks below and poison the
     * whole result.
     */
    double prob_sum = 0.0;
    for (int i = 0; i < num_outcomes; ++i) {
        if (!isfinite(payout_matrix[i]) || payout_matrix[i] < 0.0)
            return -1;
        if (!isfinite(probability_matrix[i]) ||
            probability_matrix[i] < 0.0 || probability_matrix[i] > 1.0)
            return -1;
        prob_sum += probability_matrix[i];
    }
    if (prob_sum < 0.999 || prob_sum > 1.001)
        return -1;

    double ev = 0.0;
    double exp_x2 = 0.0;

    for (int i = 0; i < num_outcomes; ++i) {
        double p = probability_matrix[i];
        double x = payout_matrix[i];
        ev += p * x;
        exp_x2 += p * x * x;
    }

    double variance = exp_x2 - ev * ev;
    if (variance < 0.0)
        variance = 0.0; /* guard against tiny floating point negativity */
    double std_dev = sqrt(variance);

    if (out_result) {
        memset(out_result, 0, sizeof(*out_result));
        out_result->num_rows = num_outcomes;
        out_result->total_ev = ev;
        out_result->house_edge = 1.0 - ev;
        out_result->variance = variance;
        out_result->std_dev = std_dev;

        for (int i = 0; i < num_outcomes; ++i) {
            out_result->rows[i].category_id = i;
            out_result->rows[i].category_name = NULL;
            out_result->rows[i].payout_multiplier = payout_matrix[i];
            out_result->rows[i].probability = probability_matrix[i];
            out_result->rows[i].ev_contribution =
                probability_matrix[i] * payout_matrix[i];
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * Standard video poker paytable definitions (optimal-strategy combinations).
 * Probabilities are computed as combinations / total_combinations for maximum
 * precision, so the resulting EV matches the published figures.
 * ------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    double payout;
    long long combinations;
} pe_vp_category_t;

typedef struct {
    const char *game_name;
    long long total_combinations;
    int num_categories;
    pe_vp_category_t categories[PE_PAYTABLE_MAX_ROWS];
} pe_vp_game_def_t;

static const pe_vp_game_def_t VP_GAMES[PE_VIDEO_POKER_COUNT] = {
    {
        .game_name = "jacks_or_better_9_6",
        .total_combinations = 19933230517200LL,
        .num_categories = 10,
        .categories = {
            { "Royal Flush",      800, 493512264LL },
            { "Straight Flush",    50, 2178883296LL },
            { "Four of a Kind",    25, 47093167764LL },
            { "Full House",          9, 229475482596LL },
            { "Flush",              6, 219554786160LL },
            { "Straight",           4, 223837565784LL },
            { "Three of a Kind",    3, 1484003070324LL },
            { "Two Pair",           2, 2576946164148LL },
            { "Jacks or Better",    1, 4277372890968LL },
            { "Nothing",            0, 10872274993896LL },
        },
    },
    {
        .game_name = "deuces_wild_full_pay",
        .total_combinations = 19933230517200LL,
        .num_categories = 11,
        .categories = {
            { "Natural Royal Flush", 800, 440202756LL },
            { "Four Deuces",         200, 4060462824LL },
            { "Wild Royal Flush",     25, 35796957696LL },
            { "Five of a Kind",       15, 63818309856LL },
            { "Straight Flush",        9, 83087969280LL },
            { "Four of a Kind",        5, 1294427430576LL },
            { "Full House",            3, 423165297240LL },
            { "Flush",                2, 334561280724LL },
            { "Straight",             2, 1117664265756LL },
            { "Three of a Kind",       1, 5674784779512LL },
            { "Nothing",              0, 10901423560980LL },
        },
    },
    {
        .game_name = "joker_poker_kings_or_better",
        .total_combinations = 24568865521200LL,
        .num_categories = 12,
        .categories = {
            { "Natural Royal Flush", 800, 596131848LL },
            { "Five of a Kind",      200, 2293355592LL },
            { "Wild Royal Flush",    100, 2556304788LL },
            { "Straight Flush",       50, 14124168708LL },
            { "Four of a Kind",       20, 210195973152LL },
            { "Full House",            7, 385217432424LL },
            { "Flush",                5, 382699900596LL },
            { "Straight",             3, 407718668724LL },
            { "Three of a Kind",      2, 3290682627144LL },
            { "Two Pair",             1, 2724028817400LL },
            { "Kings or Better",      1, 3487746690372LL },
            { "Nothing",              0, 13661005450452LL },
        },
    },
};

int pe_paytable_get_game(pe_video_poker_game_t game,
                          pe_paytable_result_t *out_result)
{
    if (game < 0 || game >= PE_VIDEO_POKER_COUNT || !out_result)
        return -1;

    const pe_vp_game_def_t *def = &VP_GAMES[game];
    const int n = def->num_categories;
    if (n < 1 || n > PE_PAYTABLE_MAX_ROWS)
        return -1;

    /* Zero-initialised: only the first n entries are written below, and GCC
     * cannot prove that from the table, so it warns under -Werror. */
    double payouts[PE_PAYTABLE_MAX_ROWS] = { 0.0 };
    double probs[PE_PAYTABLE_MAX_ROWS] = { 0.0 };

    for (int i = 0; i < n; ++i) {
        payouts[i] = def->categories[i].payout;
        probs[i] = (double)def->categories[i].combinations /
                   (double)def->total_combinations;
    }

    int rc = pe_paytable_compute_ev(payouts, probs, n, out_result);
    if (rc != 0)
        return rc;

    strncpy(out_result->game_name, def->game_name,
            sizeof(out_result->game_name) - 1);
    out_result->game_name[sizeof(out_result->game_name) - 1] = '\0';

    for (int i = 0; i < n; ++i) {
        out_result->rows[i].category_name = def->categories[i].name;
    }

    return 0;
}

int pe_paytable_game_ev(pe_video_poker_game_t game, double *out_ev)
{
    if (!out_ev)
        return -1;
    pe_paytable_result_t res;
    int rc = pe_paytable_get_game(game, &res);
    if (rc != 0)
        return rc;
    *out_ev = res.total_ev;
    return 0;
}

void pe_paytable_print(const pe_paytable_result_t *result)
{
    if (!result) {
        printf("Paytable result: NULL\n");
        return;
    }

    printf("=== Paytable: %s ===\n", result->game_name);
    printf("%-22s %10s %14s %14s\n", "Category", "Payout", "Probability",
           "EV Contrib");
    for (int i = 0; i < result->num_rows; ++i) {
        const char *nm = result->rows[i].category_name
                             ? result->rows[i].category_name
                             : "(unnamed)";
        printf("%-22s %10.0f %14.8f %14.8f\n", nm,
               result->rows[i].payout_multiplier,
               result->rows[i].probability,
               result->rows[i].ev_contribution);
    }
    printf("%-22s %10s %14s %14.8f\n", "TOTAL", "", "", result->total_ev);
    printf("House Edge : %.6f (%.4f%%)\n", result->house_edge,
           result->house_edge * 100.0);
    printf("Variance   : %.6f\n", result->variance);
    printf("Std Dev    : %.6f\n", result->std_dev);
}
