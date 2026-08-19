/*
 * paytable_ev.h - Paytable Expected Value, House Edge & variance engine
 *                 for Casino / Video Poker games.
 *
 * Implements the mathematics from Bollman, *Intermediate Poker Mathematics*
 * (Chapters 7 & 8): for a fixed paytable with payout multipliers X_i and
 * outcome probabilities P_i (under optimal hold strategy),
 *
 *     EV       = E[X] = Sum_i P_i * X_i
 *     Var(X)   = E[X^2] - (E[X])^2 = Sum_i P_i * X_i^2 - EV^2
 *     sigma(X) = sqrt(Var(X))
 *     HouseEdge = 1.0 - EV
 */

#ifndef POKER_EVAL_PAYTABLE_EV_H
#define POKER_EVAL_PAYTABLE_EV_H

#include <poker_eval/utils/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single paytable outcome (hand category). */
typedef struct {
    int category_id;             /* Index of the category (0-based) */
    const char *category_name;   /* Human readable category name (may be NULL) */
    double payout_multiplier;    /* Payout multiplier X_i */
    double probability;          /* Outcome probability P_i */
    double ev_contribution;      /* P_i * X_i */
} pe_paytable_row_t;

/* Result of a paytable EV analysis. */
typedef struct {
    char game_name[32];          /* Game / paytable identifier */
    pe_paytable_row_t rows[16];  /* Per-category breakdown */
    int num_rows;                /* Number of categories (<= 16) */
    double total_ev;             /* Expected return per unit wager */
    double house_edge;           /* 1.0 - total_ev */
    double variance;             /* Var(X) */
    double std_dev;              /* sigma(X) = sqrt(Var(X)) */
} pe_paytable_result_t;

/*
 * Compute EV / house edge / variance / std-dev from raw payout and
 * probability matrices.
 *
 * @param payout_matrix      Array of payout multipliers X_i (length num_outcomes)
 * @param probability_matrix Array of outcome probabilities P_i (length num_outcomes)
 * @param num_outcomes       Number of outcomes (must be in [1, 16])
 * @param out_result         Output result structure (may be NULL if only the
 *                           return code is needed)
 * @return 0 on success, -1 on invalid input
 */
POKEREVAL_EXPORT int pe_paytable_compute_ev(
    const double *payout_matrix,
    const double *probability_matrix,
    int num_outcomes,
    pe_paytable_result_t *out_result
);

/* Standard video poker game identifiers. */
typedef enum {
    PE_VIDEO_POKER_JACKS_OR_BETTER_9_6 = 0,
    PE_VIDEO_POKER_DEUCES_WILD_FULL_PAY,
    PE_VIDEO_POKER_JOKER_POKER_KINGS_OR_BETTER,
    PE_VIDEO_POKER_COUNT
} pe_video_poker_game_t;

/*
 * Fill out_result with a standard, named video poker paytable using its
 * published optimal-strategy outcome probabilities.
 *
 * @param game        One of PE_VIDEO_POKER_*
 * @param out_result  Output result structure
 * @return 0 on success, -1 on invalid game id
 */
POKEREVAL_EXPORT int pe_paytable_get_game(
    pe_video_poker_game_t game,
    pe_paytable_result_t *out_result
);

/*
 * Convenience: compute EV for a named game directly.
 * Equivalent to pe_paytable_get_game(... ) then reading total_ev.
 */
POKEREVAL_EXPORT int pe_paytable_game_ev(
    pe_video_poker_game_t game,
    double *out_ev
);

/* Pretty-print a paytable result to stdout (debug / CLI). */
POKEREVAL_EXPORT void pe_paytable_print(const pe_paytable_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PAYTABLE_EV_H */
